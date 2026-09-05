# Resource-History Tracking: Streaming Support (Proposed)

**Status:** Proposed - not yet implemented. This documents a plan agreed on
2026-09-05, to be picked up as follow-on work.

## Background

`Trace::Context::m_resource_history` (added alongside the replay-mode fix -
see [Simulation vs Replay Modes](SIMULATION_VS_REPLAY_MODES.md)) records a
`(time, allocated_nodes)` sample every time `process_events_until()` or
`process_single_event()` changes occupancy - roughly 2 entries per job
(one arrival, one departure). It's currently a plain `std::vector`, read
once at the end of a run by `write_resource_trace()`.

## Problem

A `std::vector` that only ever grows is the right choice for today's batch
use (all jobs known upfront, one write at the end - contiguous memory,
no reallocation surprises). It's the wrong choice for streaming use: a
long-running session that keeps submitting jobs incrementally would grow
this vector without bound, with no way to reclaim memory for history
that's already been written out.

## Proposed Design

Replace `m_resource_history`'s `std::vector` with `boost::circular_buffer`
instead of adding a second, switchable implementation. This reuses an
existing project dependency and pattern rather than introducing a new
container: `CircularBufferFCFSScheduler` (`scheduler_circular_fcfs.hpp`)
already wraps `boost::circular_buffer` for the wait queue, with a
`--circular_capacity`/`--circular_overflow {abort|grow}` CLI pattern
(`sim_params.hpp`).

- **Batch use (today's behavior, unchanged in practice):** size the buffer
  to the job count up front (mirroring `m_circular_capacity(0) == size of
  job trace, never overflows` for the wait queue), so it never grows or
  drops entries - equivalent to today's vector for the dominant use case.
- **Streaming use (new):** add a `flush(ostream&)` operation that writes
  the buffer's current contents in order, then clears it, reclaiming the
  space. A long-running streaming session calls `flush()` periodically
  instead of holding the entire history in memory.

## Open Questions (to resolve before implementing)

1. What triggers a flush in streaming use - a job-count threshold, a
   time interval, or an explicit caller-driven call? `tracer`/`Trace` has
   no incremental/streaming entry point today (unlike `Simulation`, which
   has `advance_to()`); this would need one, or would need to hook into
   `Simulation`'s existing streaming API instead.
2. Does `write_resource_trace()`'s one-shot API change shape, or does it
   become "flush what's left" at the end of a streaming session, called
   the same way batch mode calls it today?
3. Capacity default in streaming use, when total job count isn't known
   upfront (unlike batch): follow `m_circular_capacity` default of 0 =
   grow, or set an explicit bound.

## Out of Scope for This Doc

This does not affect anything fixed in the 2026-09-05 replay-mode session
(`simulator` auto-detection, `tracer --resource_trace`, `run_replay_tests.sh`)
- those are complete and independent of this proposal.
