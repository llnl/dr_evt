# Trace as a self-contained, streaming-ready state container

**Status:** Resource-history (`m_ctx.m_resource_history`) is implemented -
see `Trace::record_resource_sample()`/`resolve_resource_history_capacity()`/
`flush_resource_history()` in `trace.cpp`. Job records (`m_data`) are not
yet converted.

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
hand `Trace` a job it didn't already know about.

**Eviction ("fossil collection," in discrete-event-sim terms):** the two
buffers turned out to need different safety rules, not the single shared
one originally envisioned here. For `m_data`, an entry is safe to reclaim
once local simulated time has passed its `end_time` - sound by
event-ordering alone, since the departure event that reads it necessarily
fires no later than that time; `start_time` and `end_time` are written
together in one mutation at job-start (duration is already known then,
from `time_limit`/`run_time_mode`), not written a second time at actual
completion. For resource-history, no such check is needed or possible -
it's a strictly time-ordered append log with no equivalent of `end_time`
to wait for, so every entry is unconditionally safe to evict the moment
it's recorded (see `Trace::m_resource_history`'s comment in `trace.hpp`);
this is also why resource-history has no abort/grow overflow policy at
all (`--resource_history_capacity` only, no `--resource_history_overflow`)
- eviction on capacity always succeeds, so that fallback would be dead
code. `m_data`'s eventual conversion will need it, since an entry can
still be in-flight when the buffer fills.

Both are capacity-driven operations, not required at every `advance_to()`
call: eviction only needs to happen when the buffer is actually full and
space must be reclaimed for new entries. A buffer sized generously enough
for the session may flush once, rarely, or never - there's no requirement
to flush on every advancement of simulated time. For resource-history,
"sized generously enough" defaults to twice the job count (each job
contributes at most 2 events, each recording one sample), floored at 4096
regardless, since the job count itself may still be tiny (or 0, early in
a streaming session) at the moment the very first sample is recorded.

**Kept abstracted for possible future optimistic parallelism (not built
now):**
- the eviction-safe check, so a GVT-based horizon can later replace
  today's single-process "already processed locally" check without
  restructuring the buffers themselves
- the start-time mutation, kept as one isolated write so it can later be
  wrapped for rollback/undo
- `Trace&` (not shared/weak ownership) for now - a single caller drives
  the engine synchronously, so there's no lifetime risk to guard against;
  upgrading ownership only matters if fossil collection ever runs
  concurrently with the engine
