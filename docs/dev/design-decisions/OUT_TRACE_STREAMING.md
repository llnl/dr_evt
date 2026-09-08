# Trace as a self-contained, streaming-ready state container

**Status:** Both circular buffers are implemented. Resource-history
(`m_ctx.m_resource_history`) - see `Trace::record_resource_sample()`/
`resolve_resource_history_capacity()`/`flush_resource_history()`. Job
records (`m_data`) - see `Trace::job_at()`/`resolve_job_store_capacity()`/
`reclaim_front_jobs()`/`write_job_line()`, all in `trace.cpp`. The
streaming append operation is also now built -
`Trace::append_job()`/`Simulation::append_job()` for a single job,
`Trace::append_jobs()`/`Simulation::append_jobs()` for several in one
call, both exposed over gRPC (`AppendJobRequest`/`AppendJobsRequest`) -
see "Batch vs streaming" below for what they do and
`tests/test_append_job_api.cpp`/`tests/test_grpc_streaming_api.cpp` for their
tests.

Progressive/multi-file loading (`--infile_list`, discussed at length
below under capacity sizing) is also now built -
`Trace::load_next_file()`/`Simulation::run_progressive()` - see
`tests/test_progressive_load.cpp`/`tests/run_progressive_load_tests.sh`
for its tests. It does *not* go through `append_jobs()`/
`Job_Append_Request` in the end, despite that being the original plan
(see this doc's earlier draft, and `append_jobs()`'s own doc comment in
`trace.hpp`, both predating this): `load()` (the free function
`load_data()` already uses) produces fully-formed `Job_Record`s
directly, including `actual_run_time` - routing those through
`Job_Append_Request`'s narrower, network-facing 4-field shape and back
would silently drop that column back to 0.0, wrong for
`run_time_mode=actual`/`distribution`. `load_next_file()` takes
`Job_Record`s directly instead; the batch-capacity logic itself
(reclaim, then grow-or-abort) was pulled out of `append_jobs()` into a
shared `Trace::ensure_batch_capacity()` helper so neither caller
duplicates it.

An actual memory-pressure check (`--check_memory_pressure FRACTION`/
`Trace::check_memory_pressure()`, discussed at length below under
capacity sizing) is also now built - called from
`ensure_batch_capacity()`, so it covers `append_jobs()` too, not just
`load_next_file()`. Disabled unless given an explicit fraction; no
baked-in default, since what's safe headroom genuinely differs by
environment.

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
streaming appends one job (or several) at a time as they genuinely
arrive, `m_data` starting empty (or already holding whatever batch
preloaded, if any - the two aren't mutually exclusive). `submit_job(job_idx, ...)`
assumes a job already exists in `m_data`, referenced by index - it never
`push_back()`s. `Trace::append_job()` is the actual append operation
that does: given a new job's own data (submit_time, num_nodes, queue,
limit_time - not an index into anything preloaded), it constructs a new
`Job_Record` and adds it to `m_data`, following the same
check-`full()`-then-reclaim-then-grow order established below, and
returns the new job's `job_no` for a subsequent `submit_job()` call.
`Simulation::append_job()` is the public wrapper; `AppendJobRequest` is
the gRPC-level one.

`Trace::append_jobs()`/`Simulation::append_jobs()` are the batch
counterpart - several new jobs (a `std::vector<Job_Append_Request>`) in
one call, for the same never-seen-before case. Unlike `append_job()`'s
per-job check-`full()`-then-reclaim-then-grow, this resolves capacity
for the *whole* batch up front: `reclaim_front_jobs()` is asked for
enough room for the entire batch in one call (its `min_free` parameter,
generalized from the single-job callers' implicit "just 1" - see its
own doc comment), then, only if that wasn't enough, the buffer grows
once directly to a size that fits the batch (still by doubling, the
same convention `append_job()`/`load_data()` use per-request, just
resolved in one step) - not a per-request loop resizing the buffer
once per job in a batch of possibly many. Only once capacity is
confirmed sufficient does every request get copied in, with no further
checks interleaved between them.

This makes the whole call fully all-or-nothing, not just on input:
*input validation* (every request's `submit_time >= current_time`, and
the whole batch already sorted by `submit_time`, non-decreasing - an
invariant silently and implicitly assumed across separate `append_job()`
calls, checked explicitly here) happens first, before `m_data` is
touched at all. *Capacity exhaustion*
(`--job_store_overflow=abort`) is now equally atomic, as a direct
consequence of resolving capacity for the whole batch up front rather
than discovering it request-by-request: the abort fires before the
batch's first `push_back()`, so `m_data` is left exactly as it was,
never partially filled. (An earlier version of this design checked
capacity per-request instead, which could only discover exhaustion
mid-loop; kept here as a note since it's an easy design to reach for
by analogy with `append_job()`, and the wrong one for a batch call.)

A third, distinct case - **progressive/multi-file loading**
(`--infile_list`, reading a known trace as several separate files
instead of one, purely to respect a memory limit) - is now built,
differently from how this section originally sketched it. The design
that shipped is simpler than "chunked reading of one big file": the
*user* pre-splits and pre-sorts their trace into several files
themselves and hands `dr_evt` the sequence (a list file, one path per
line, via `--infile_list`) - there's no adaptive chunk-sizing or
memory-pressure estimation to get right, and no persistent, resumable
file handle to manage; each file is opened, fully read via `load()`
(the same free function `load_data()` already uses), and closed, same
as `load_data()`'s own single-file read. `Trace::load_next_file()` is
the per-file entry point; `Simulation::run_progressive()` is the
driving loop (`Simulation::run()` calls it in place of the single-file
batch path when `--infile_list` is given): load a file, submit its
jobs one at a time in `submit_time` order, `advance_to()` to the last
one's own `submit_time`, then load the next - not the single-file
path's "submit everything upfront, one `advance_to(infinity)` at the
end," which would leave every file's jobs already known to `m_data`
before reclaiming ever got a chance to run even once. `advance_to()`
itself needed no changes at all for this - it's called exactly the way
it's always been, just with a real target instead of infinity, once
per file instead of once for the whole run.

Two validity checks, both all-or-nothing before `m_data` is touched at
all (same shape `append_jobs()` already established): each file's own
rows must already be sorted by `submit_time` (`load_data()` tolerates
unsorted input by sorting everything it read in memory first -
`load_next_file()` can't, since it never holds more than one file's
rows at a time), and each file's earliest `submit_time` must be `>=`
the previous file's latest (continuity across the sequence).

Still deliberately deferred, for whoever picks it up next: **an actual
memory-pressure estimate**, rather than today's simpler rule (grow to
fit whatever's left unreclaimed plus the next file, unconditionally).
The formula proposed for that: `(jobs unflushed in the job store) * 2 +
(jobs in the next file)`, and this should stay under `0.8 * (total
available memory) / (size of one job record)` - the reader would use
this to decide how large a file it can safely take on next (or refuse/
wait), rather than growing unconditionally as `load_next_file()` does
today via the shared `ensure_batch_capacity()` helper.

**Update: this is now built**, refined from the rough formula sketched
above once it became clear a fixed multiplier can badly underestimate
the real peak - `--check_memory_pressure FRACTION`/
`Trace::check_memory_pressure()`, called from `ensure_batch_capacity()`
(so it covers `append_jobs()` too, not just `load_next_file()`) right
after reclaiming, before deciding whether to grow.

Rather than a fixed multiplier on `m_data.size()`, the check mirrors
`ensure_batch_capacity()`'s own doubling grow loop exactly to get the
real target capacity - however many doublings a large batch needs, not
an approximation that's only right for a single doubling. If
`old_cap = m_data.capacity()` and `new_cap` (from that mirrored loop)
exceeds it, a grow would happen: `boost::circular_buffer::set_capacity()`
allocates the new buffer and copies every existing entry into it before
freeing the old one, so both are resident simultaneously mid-copy -
projected peak is `old_cap + new_cap + batch_size`, that last term for
`load_next_file()`'s own temporary `std::vector<Job_Record>` (this
batch's rows, read from file before this call, not yet moved into
`m_data`) still being resident at the exact moment this check runs.
(`append_jobs()` has no such vector - its `Job_Append_Request` is a
different, smaller struct until pushed - but counting it uniformly for
both callers only makes the check more conservative for `append_jobs()`,
never less.) If no grow would happen, peak is just `old_cap + batch_size`.

This is compared against `FRACTION * (available memory) /
sizeof(Job_Record)` - `FRACTION` is a required, user-supplied argument
(`0.0 < FRACTION <= 1.0`; there's no baked-in default, since what's
"safe" headroom genuinely differs by environment - see below), not a
bare on/off flag. "Refuse" is the only option implemented, not "wait":
`dr_evt` is a batch/simulation tool, not a live process that could
plausibly wait for memory to free up externally between calls - the
only way memory frees up here is reclaiming already-completed jobs,
which `ensure_batch_capacity()` already attempts before this check runs.
Available memory is queried from `/proc/meminfo`'s `MemAvailable` on
Linux (`src/utils/system_memory.hpp`'s `get_available_memory_bytes()`);
0 (meaning "unknown, nothing to enforce against") on any other platform
or if that file is unreadable/lacks the line - this is a no-op there,
not a hard failure. Disabled unless given an explicit fraction: unlike
`--job_store_capacity` (bounding a buffer size the caller explicitly
chose), this queries the actual machine's memory, which not every
caller wants tied to at all, and even those who do want it need a
different threshold depending on environment - a containerized or
memory-cgroup'd process, where `/proc/meminfo` reports host-level
availability rather than the effective cgroup limit, needs a much
tighter fraction (or none at all) than a dedicated bare-metal node
would. See `tests/test_progressive_load.cpp`/`tests/test_append_job_api.cpp`
for its tests, both using a `DR_EVT_TEST_AVAILABLE_MEMORY_BYTES`
environment-variable test seam to force deterministic low/plentiful-
memory conditions rather than depending on the test machine's actual
state - including a test confirming the fraction itself is what's
compared against (the same forced memory refuses at a tight fraction
but succeeds at a loose one), not a fixed threshold with a
configurable-looking argument that doesn't actually change anything.


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
`append_jobs()` already does the "reclaim, then size capacity for this
batch" half of this for whatever chunk it's handed - what it does not
do is decide how large that chunk should be; that adaptive sizing
decision, and what to do with any of a chunk that still doesn't fit
(see "Batch vs streaming" above), are what's left for this future work.

**Reclaim at the point of need, not proactively - including at
insertion, not just at departure.** Lazy reclaim (above) means checking
whenever something might need the room, not on a schedule of its own -
but `load_data()`'s transfer loop doesn't actually need this check: at
load time nothing has run or been rejected yet, so nothing is ever
reclaimable there regardless of order, and adding a
`reclaim_front_jobs()` call there would just be dead code in a
one-time, bulk-load function that will never exercise it.

**Where this actually matters is `Trace::append_job()`/`append_jobs()`**
(not `submit_job()`, which never inserts a new entry; it only mutates a
job already sitting in a preloaded `m_data`; and not `load_data()`
either, for the reason just given - it's a fundamentally different,
one-time bulk-load operation, not a repeated per-arrival insertion
point). `append_job()` follows the order this section establishes:
check `full()`, then try `reclaim_front_jobs()`, and only fall back to
growing if nothing was reclaimable - reclaiming a completed job's slot
right at the insertion that needs it, not growing first and reclaiming
never. `append_jobs()` follows the same order, generalized from "is
there room for one more" to "is there room for this whole batch" -
`reclaim_front_jobs()` takes a `min_free` parameter for exactly this
(defaulting to 1, matching every single-job caller unchanged; see its
own doc comment), so a batch call can ask it to reclaim as much as the
whole batch needs in one pass rather than one slot at a time.

`reclaim_front_jobs()` also takes a `handle_overflow` parameter
(default true) - whether *it* applies `m_job_store_overflow`
(grow-or-abort) if still `full()` after reclaiming. `append_job()`
relies on this default, having no capacity check of its own afterward.
`append_jobs()` passes false: it already has its own batch-aware
capacity check right after this call, sized against the batch's actual
count rather than just "full or not" - letting `reclaim_front_jobs()`'s
fallback fire too risked either an unnecessarily-small intermediate
grow, or (for abort) a misleading single-job-shaped message, in the
case where reclaiming frees some slots but not enough for the whole
batch (caught only by testing directly with a batch bigger than the
capacity's first doubling - the original tests happened to use numbers
where both paths landed on the same final capacity, hiding the bug).

Verified directly, not just by construction: forcing
`--job_store_capacity 1` and appending a second job after the first
finished reclaims the first's slot (`capacity` stays 1, `num_reclaimed`
becomes 1) rather than growing - see
`test_append_reclaims_before_growing()` (single-job) and
`test_append_jobs_reclaims_before_growing()` (batch) in
`tests/test_append_job_api.cpp`.

Regardless of requested capacity, batch mode's actual reclaim behavior
today is: at most one slot reclaimed for the whole run. Once `size()`
drops below `capacity()` (whichever job first becomes reclaimable -
rejected or normally completed, see below), the buffer never becomes
`full()` again, since batch mode never inserts anything new after
loading finishes.

This means `reclaim_front_jobs()`/`job_at()` exist for batch mode's
correctness (a rejected job must not stall the sweep forever) and to
prepare for both streaming and progressive/multi-file loading - where
reclaiming would
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
