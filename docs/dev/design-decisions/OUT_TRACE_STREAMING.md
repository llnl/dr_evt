# Trace as a self-contained, streaming-ready state container

**Status:** Both circular buffers are implemented. Resource-history
(`m_ctx.m_resource_history`) - see `Trace::record_resource_sample()`/
`resolve_resource_history_capacity()`/`flush_resource_history()`. Job
records (`m_data`) - see `Trace::job_at()`/`resolve_job_store_capacity()`/
`reclaim_front_jobs()`/`write_job_line()`, all in `trace.cpp`. The
genuine streaming append operation is also now built -
`Trace::append_job()`/`Simulation::append_job()`, exposed over gRPC via
`AppendJobRequest` - see "Batch vs streaming" below for what it does and
`tests/test_append_job_api.cpp`/`tests/test_append_job_grpc.cpp` for its
tests. Chunked loading (also mentioned below) is not.

`Trace` owns all session state: job records (`m_data`) and simulation
context (`m_ctx` - pending-completion event queue and resource-history,
today split out as `Simulation::m_replay_ctx`), both backed by
`boost::circular_buffer` instead of `std::vector`/plain containers -
reusing the pattern `CircularBufferFCFSScheduler`
(`scheduler_circular_fcfs.hpp`) already uses for the wait queue.
`Simulation` still holds an owned `Trace` (`Trace m_trace;`), not a
reference - the plain-reference ownership model below remains the target,
not yet how the code is today.

**Batch vs streaming** differ only in how jobs enter `m_data`: batch
preloads the whole file via `load_data()` before processing starts;
streaming appends one job at a time as it genuinely arrives, `m_data`
starting empty (or already holding whatever batch preloaded, if any -
the two aren't mutually exclusive). `submit_job(job_idx, ...)` assumes a
job already exists in `m_data`, referenced by index - it never
`push_back()`s. `Trace::append_job()` is the actual append operation
that does: given a new job's own data (submit_time, num_nodes, queue,
limit_time - not an index into anything preloaded), it constructs a new
`Job_Record` and adds it to `m_data`, following the same
check-`full()`-then-reclaim-then-grow order established below, and
returns the new job's `job_no` for a subsequent `submit_job()` call.
`Simulation::append_job()` is the public wrapper; `AppendJobRequest` is
the gRPC-level one.

A third, distinct case - **chunked loading** (reading a known, whole
trace file in pieces rather than all at once, purely to respect a
memory limit) - is discussed below under capacity sizing; not yet
built - `append_job()` above solves genuine streaming (jobs not known
in advance at all), not this.

**Why a job's slot becomes reclaimable at `end_time`, not before:** a
job's record is only safe to discard once nothing will read it again.
The last thing that reads it is its own departure event - freeing the
nodes it held, contributing a resource-history sample - which fires at
`end_time`, not at arrival. So "job finished" is shorthand for "the last
reader has already read it," not an arbitrary choice of timing.
Resource-history's entries are simpler: nothing reads a sample again
after it's written, so every entry there is safe to reclaim immediately,
with no equivalent wait.

**In batch mode, a small requested capacity doesn't do what it's for.**
`load_data()` reads the whole file into memory, then sizes `m_data` to
fit the whole trace either way: directly, for the default (`0`)
capacity, or by growing (doubling) during the transfer if
`--job_store_capacity` explicitly requested something smaller.
Confirmed directly: requesting `--job_store_capacity 2` on a 10,000-job
trace still ends up with a post-load capacity of 16384.

A smaller requested capacity isn't a mistake to grow past - a user
setting it is signaling a physical memory limit, not asking for a
smaller allocation that still ends up holding the whole trace anyway.
Honoring that properly means reading the trace file in chunks as the
simulation progresses, rather than loading it all upfront - **future
work, out of scope for this PR, to take up once it's merged.**

**Sketch of that future chunked-loading scheme, for whoever picks it
up:** each time more needs to be read, check how much of `m_data` is
still occupied (not yet reclaimed) and how much is being newly read in,
then size capacity to accommodate both - not a fixed chunk size decided
once upfront. How much to read in at a time should itself be adaptive,
based on both how much is still remaining in `m_data` and current
memory pressure, not a constant. This is consistent with lazy reclaim
and reclaim-before-grow (both above): the loader would check occupancy
before each read, reclaim what it can, and read only as much new data
as the resulting headroom (plus memory pressure) actually justifies.

**Reclaim at the point of need, not proactively - including at
insertion, not just at departure.** Lazy reclaim (above) means checking
whenever something might need the room, not on a schedule of its own -
but `load_data()`'s transfer loop doesn't actually need this check: at
load time nothing has run or been rejected yet, so nothing is ever
reclaimable there regardless of order, and adding a
`reclaim_front_jobs()` call there would just be dead code in a
one-time, bulk-load function that will never exercise it.

**Where this actually matters is `Trace::append_job()`** (not
`submit_job()`, which never inserts a new entry; it only mutates a job
already sitting in a preloaded `m_data`; and not `load_data()` either,
for the reason just given - it's a fundamentally different, one-time
bulk-load operation, not a repeated per-arrival insertion point).
`append_job()` follows the order this section establishes: check
`full()`, then try `reclaim_front_jobs()`, and only fall back to growing
if nothing was reclaimable - reclaiming a completed job's slot right at
the insertion that needs it, not growing first and reclaiming never.
Verified directly, not just by construction: forcing
`--job_store_capacity 1` and appending a second job after the first
finished reclaims the first's slot (`capacity` stays 1, `num_reclaimed`
becomes 1) rather than growing - see
`test_append_reclaims_before_growing()` in
`tests/test_append_job_api.cpp`.

Regardless of requested capacity, batch mode's actual reclaim behavior
today is: at most one slot reclaimed for the whole run. Once `size()`
drops below `capacity()` (whichever job first becomes reclaimable -
rejected or normally completed, see below), the buffer never becomes
`full()` again, since batch mode never inserts anything new after
loading finishes.

This means `reclaim_front_jobs()`/`job_at()` exist for batch mode's
correctness (a rejected job must not stall the sweep forever) and to
prepare for both streaming and chunked loading - where reclaiming would
actually matter repeatedly: with no preload phase to size capacity
against upfront, a newly-arriving job that finds the buffer full has to
either reclaim a completed job's slot from the front, or grow. Batch
mode itself doesn't benefit from repeated reclaiming; its memory
footprint ends up the same as the original unbounded `std::vector`
design either way.

**`job_no` vs. physical position:** callers (`Simulation`, the scheduler)
always address a job by its permanent `job_no` - the same identifier
since the trace was sorted at load time, never renumbered. `m_data`'s
physical layout shifts as slots get reclaimed, so every `job_no`-based
lookup goes through `Trace::job_at(job_no)`, the one translation choke
point: `m_data[job_no - m_num_reclaimed]`. This works because reclaiming
is front-only with **no compaction and no removed flag** (unlike the
wait queue's mark-and-compact, built for a different problem: jobs
dispatched from a queue can leave in any order, so it needs per-entry
removal tracking and a periodic GC pass). For `m_data`, front-only
reclaiming plus `m_data`'s existing submit-time sort means nothing can
reach the front out of submission order - a job that completes
out-of-order (backfilled, finishes before an earlier-submitted job) just
sits in its slot until everything ahead of it has also been reclaimed. A
single global offset (`m_num_reclaimed`) is only valid because of this -
compaction would break it, since jobs before vs. after a removed middle
entry would need different corrections.

The scheduler needed the same fix: it held a raw
`const std::vector<Job_Record>*` and indexed it directly by `job_no`,
which silently assumed a stable layout that never changed. It now holds
`const Trace*` and calls `job_at()` too - not part of the original
design discussion, found only once the build actually broke on it
(`scheduler_base.hpp` and every scheduler subclass).

**Reclaiming ("fossil collection," in discrete-event-sim terms):** the
two buffers need different safety rules. For `m_data`, an entry is safe
to reclaim once local simulated time has passed its `end_time` - sound
by event-ordering alone, since the departure event that reads it
necessarily fires no later than that time; `start_time` and `end_time`
are written together in one mutation at job-start (duration is already
known then, from `time_limit`/`run_time_mode`), not written a second
time at actual completion. A rejected job (requests more nodes than
exist - permanently unschedulable) is a second, distinct case: its
`end_time` never resolves either way, so it would otherwise stall the
sweep forever. It's marked at rejection time by setting `submit_time` to
`Job_Record::unscheduled_sentinel()` (a field that's otherwise always
real and load-time-fixed for every job, so this is a genuinely new
signal, not a collision with anything else's default) - the full
eligibility check is `submit_time == sentinel || end_time <=
current_time`. For resource-history, no such check is needed or
possible - it's a strictly time-ordered append log with no equivalent of
`end_time` to wait for, so every entry is unconditionally safe to
reclaim the moment it's recorded (see `Trace::m_resource_history`'s
comment in `trace.hpp`); this is also why resource-history has no
abort/grow overflow policy at all (`--resource_history_capacity` only,
no `--resource_history_overflow`) - reclaiming when full always
succeeds, so that fallback would be dead code. `m_data` does need one
(`--job_store_overflow {abort|grow}`), since a still-running front entry
can leave the buffer full with nothing currently reclaimable.

**Lazy reclaim:** both buffers reclaim on demand, not proactively -
capacity-driven, not required at every `advance_to()` call. Reclaiming
only needs to happen when the buffer is actually full and space must be
made for new entries - checked explicitly (`m_data.full()`) before ever
sweeping, not attempted the moment something merely becomes reclaimable
(an early version of `reclaim_front_jobs()` got this wrong: it swept
unconditionally on every completion, which discarded jobs that were
still needed by `write_simulated_trace()`/stats, well before the buffer
was ever actually full - see also the insertion-time case above). For
`m_data` in batch mode this means at most one reclaim for the whole run
(see above) - resource-history, which genuinely accumulates
incrementally rather than being preloaded, can flush repeatedly if its
capacity is set smaller than the total event count, or never if sized
generously. For resource-history, "sized generously enough" defaults to
twice the job count (each job contributes at most 2 events, each
recording one sample); for `m_data`, one slot per job. Both floor at
4096 regardless, since the job count itself may still be tiny (or 0,
early in a streaming session) at the moment the first entry is
recorded/loaded.

**Reading a job's data out before its slot is reclaimed:** `m_data`'s
reclaiming discards a job's record, so anything that still needs it -
the output file, running statistics - has to consume it at reclaim
time, not in a final pass over `m_data` after the run (that pass, by
then, has already lost whatever was reclaimed mid-run). `write_job_line()`
is the single choke point both `reclaim_front_jobs()` and
`write_simulated_trace()`'s end-of-run flush go through, so every
`is_scheduled()` job is written/counted exactly once regardless of when
its slot was reclaimed. It also accumulates the running sum+count stats
`print_stats()` needs (completed count, wait/turnaround sums, makespan) -
trivial to maintain incrementally, and the only correct option once
iterating `m_data` directly can no longer see everything that ran.

**Kept abstracted for possible future optimistic parallelism (not built
now):**
- the reclaim-safe check, so a GVT-based horizon can later replace
  today's single-process "already processed locally" check without
  restructuring the buffers themselves - `m_data`'s check currently
  compares against `current_time` directly (deliberately, not
  `current_time - gvt_window`, to keep it simple until that's actually
  needed), but the comparison is isolated to `is_front_reclaimable()` so
  swapping the horizon later touches one function, not the buffer design
- the start-time mutation, kept as one isolated write so it can later be
  wrapped for rollback/undo
- `Trace&` (not shared/weak ownership) for now - a single caller drives
  the engine synchronously, so there's no lifetime risk to guard against;
  upgrading ownership only matters if fossil collection ever runs
  concurrently with the engine
