# Trace as a self-contained, streaming-ready state container

**Status:** Not yet implemented.

`Trace` owns all session state: job records (`m_data`) and simulation
context (`m_ctx` - pending-completion event queue and resource-history,
today split out as `Simulation::m_replay_ctx`), both backed by
`boost::circular_buffer` instead of `std::vector`/plain containers -
reusing the pattern `CircularBufferFCFSScheduler`
(`scheduler_circular_fcfs.hpp`) already uses for the wait queue.
`Simulation` holds a plain `Trace&` and owns none of this state itself.

**Batch vs streaming** differ only in how jobs enter `m_data`: batch
preloads the whole file via `load_data()` before processing starts;
streaming appends one job at a time as it genuinely arrives, `m_data`
starting empty. `submit_job(job_idx, ...)` as it exists today assumes the
former - a fixed-size, already-loaded set, referenced by index. Streaming
needs a real append operation instead, since there's currently no way to
hand `Trace` a job it didn't already know about.

**Eviction ("fossil collection," in discrete-event-sim terms):** an entry
in either buffer is safe to reclaim once local simulated time has passed
its `end_time` - sound by event-ordering alone, since the departure event
that reads it necessarily fires no later than that time. `start_time` and
`end_time` are written together in one mutation at job-start (duration is
already known then, from `time_limit`/`run_time_mode`), not written a
second time at actual completion. This is a capacity-driven operation, not
one required at every `advance_to()` call: eviction only needs to happen
when the buffer is actually full and space must be reclaimed for new
entries. A buffer sized generously enough for the session may flush once,
rarely, or never - there's no requirement to flush on every advancement of
simulated time.

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
