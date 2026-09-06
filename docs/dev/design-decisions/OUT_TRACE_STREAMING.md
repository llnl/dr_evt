# Trace as a self-contained, streaming-ready state container

**Status:** Both circular buffers are implemented. Resource-history
(`m_ctx.m_resource_history`) - see `Trace::record_resource_sample()`/
`resolve_resource_history_capacity()`/`flush_resource_history()`. Job
records (`m_data`) - see `Trace::job_at()`/`resolve_job_store_capacity()`/
`evict_front_jobs()`/`write_job_line()`, all in `trace.cpp`.

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
starting empty. `submit_job(job_idx, ...)` as it exists today assumes the
former - a fixed-size, already-loaded set, referenced by index. Streaming
needs a real append operation instead, since there's currently no way to
hand `Trace` a job it didn't already know about - **still not built**;
this document's `m_data` conversion is the batch-mode case only.

**`job_no` vs. physical position:** callers (`Simulation`, the scheduler)
always address a job by its permanent `job_no` - the same identifier
since the trace was sorted at load time, never renumbered. `m_data`'s
physical layout shifts as eviction advances, so every `job_no`-based
lookup goes through `Trace::job_at(job_no)`, the one translation choke
point: `m_data[job_no - m_num_evicted]`. This works because eviction is
front-only with **no compaction and no removed flag** (unlike the wait
queue's mark-and-compact, built for a different problem: jobs dispatched
from a queue can leave in any order, so it needs per-entry removal
tracking and a periodic GC pass). For `m_data`, front-only eviction plus
`m_data`'s existing submit-time sort means nothing can reach the front
out of submission order - a job that completes out-of-order (backfilled,
finishes before an earlier-submitted job) just sits in its slot until
everything ahead of it has also been evicted. A single global offset
(`m_num_evicted`) is only valid because of this - compaction would break
it, since jobs before vs. after a removed middle entry would need
different corrections.

The scheduler needed the same fix: it held a raw
`const std::vector<Job_Record>*` and indexed it directly by `job_no`,
which silently assumed a stable, never-evicted layout. It now holds
`const Trace*` and calls `job_at()` too - not part of the original
design discussion, found only once the build actually broke on it
(`scheduler_base.hpp` and every scheduler subclass).

**Eviction ("fossil collection," in discrete-event-sim terms):** the two
buffers need different safety rules. For `m_data`, an entry is safe to
reclaim once local simulated time has passed its `end_time` - sound by
event-ordering alone, since the departure event that reads it necessarily
fires no later than that time; `start_time` and `end_time` are written
together in one mutation at job-start (duration is already known then,
from `time_limit`/`run_time_mode`), not written a second time at actual
completion. A rejected job (requests more nodes than exist - permanently
unschedulable) is a second, distinct case: its `end_time` never resolves
either way, so it would otherwise stall the sweep forever. It's marked at
rejection time by setting `submit_time` to `Job_Record::unscheduled_sentinel()`
(a field that's otherwise always real and load-time-fixed for every job,
so this is a genuinely new signal, not a collision with anything else's
default) - the full eligibility check is
`submit_time == sentinel || end_time <= current_time`. For
resource-history, no such check is needed or possible - it's a strictly
time-ordered append log with no equivalent of `end_time` to wait for, so
every entry is unconditionally safe to evict the moment it's recorded
(see `Trace::m_resource_history`'s comment in `trace.hpp`); this is also
why resource-history has no abort/grow overflow policy at all
(`--resource_history_capacity` only, no `--resource_history_overflow`) -
eviction on capacity always succeeds, so that fallback would be dead
code. `m_data` does need one (`--job_store_overflow {abort|grow}`),
since a still-running front entry can leave the buffer full with nothing
currently reclaimable.

Both are capacity-driven operations, not required at every `advance_to()`
call: eviction only needs to happen when the buffer is actually full and
space must be reclaimed for new entries - checked explicitly
(`m_data.full()`) before ever sweeping, not attempted proactively the
moment something becomes evictable (an early version of
`evict_front_jobs()` got this wrong: it swept unconditionally on every
completion, which discarded jobs that were still needed by
`write_simulated_trace()`/stats, well before the buffer was ever
actually full). A buffer sized generously enough for the session may
flush once, rarely, or never - there's no requirement to flush on every
advancement of simulated time. For resource-history, "sized generously
enough" defaults to twice the job count (each job contributes at most 2
events, each recording one sample); for `m_data`, one slot per job.
Both floor at 4096 regardless, since the job count itself may still be
tiny (or 0, early in a streaming session) at the moment the first entry
is recorded/loaded.

**Reading a job's data out before its slot is reused:** `m_data`'s
eviction discards a job's record, so anything that still needs it -
the output file, running statistics - has to consume it at eviction
time, not in a final pass over `m_data` after the run (that pass, by
then, has already lost whatever was evicted mid-run). `write_job_line()`
is the single choke point both `evict_front_jobs()` and
`write_simulated_trace()`'s end-of-run flush go through, so every
`is_scheduled()` job is written/counted exactly once regardless of when
its slot was reclaimed. It also accumulates the running sum+count stats
`print_stats()` needs (completed count, wait/turnaround sums, makespan) -
trivial to maintain incrementally, and the only correct option once
iterating `m_data` directly can no longer see everything that ran.

**Kept abstracted for possible future optimistic parallelism (not built
now):**
- the eviction-safe check, so a GVT-based horizon can later replace
  today's single-process "already processed locally" check without
  restructuring the buffers themselves - `m_data`'s check currently
  compares against `current_time` directly (deliberately, not
  `current_time - gvt_window`, to keep it simple until that's actually
  needed), but the comparison is isolated to `is_front_evictable()` so
  swapping the horizon later touches one function, not the buffer design
- the start-time mutation, kept as one isolated write so it can later be
  wrapped for rollback/undo
- `Trace&` (not shared/weak ownership) for now - a single caller drives
  the engine synchronously, so there's no lifetime risk to guard against;
  upgrading ownership only matters if fossil collection ever runs
  concurrently with the engine
