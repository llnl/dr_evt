# Developer Documentation

This section contains implementation notes, architectural rationale, and
maintainer-facing build guidance. It is separate from the user documentation.

## Developer guides

- [Wait Queues](WAIT_QUEUES.md) — Queue selection, end-to-end performance,
  and the circular and block queue guides.
- [Job Lifecycle](JOB_LIFECYCLE.md) — How a job moves through the trace,
  scheduler, and event queue.
- [Simulation vs. Replay Modes](design-decisions/SIMULATION_VS_REPLAY_MODES.md)
  — The two execution models and their trade-offs.
- [Timezone Offset Support](design-decisions/TIMEZONE_SUPPORT.md) — ISO 8601
  timestamp offsets and their current implementation status.

## What Goes Here

### [Design decisions](design-decisions/README.md)

Important decisions worth preserving in the repository:

- Why certain approaches were chosen
- Breaking changes and migration guides
- Terminology changes and rationale

### [Job lifecycle](JOB_LIFECYCLE.md)
How a new job moves through the Trace job store, scheduler wait queue, and
scheduled start/end event queue. This is the companion explanation for the
Doxygen comments on `append_job`, `submit_job`, and the two `insert_job`
methods.
