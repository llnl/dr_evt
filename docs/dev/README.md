# Developer Documentation

This section contains implementation notes, architectural rationale, and
maintainer-facing build guidance. It is separate from the user documentation.

## Developer guides

- [Wait Queues](WAIT_QUEUES.md) — Queue selection, end-to-end performance,
  and the circular and block queue guides.
- [Simulation Pipeline and Job Lifecycle](JOB_LIFECYCLE.md) — How input moves
  through the job store, scheduler, event loop, resource accounting, and output.
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

### [Simulation pipeline and job lifecycle](JOB_LIFECYCLE.md)

How batch, progressive, and live inputs move through the job store, scheduler,
event loop, resource accounting, and output. This is the companion explanation
for the Doxygen comments on `append_job`, `submit_job`, and the two
`insert_job` methods.
