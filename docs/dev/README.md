# Developer Documentation

This section contains implementation notes, architectural rationale, and
maintainer-facing build guidance. It is separate from the user documentation.

## Developer guides

- [Wait Queues](WAIT_QUEUES.md) — Queue selection, end-to-end performance,
  and the circular and block queue guides.
- [Simulation Pipeline and Job Lifecycle](JOB_LIFECYCLE.md) — How input moves
  through the job store, scheduler, event loop, resource accounting, and output.
- [Scheduling Loops and DR_EVT Configurations](SCHEDULER_LOOPS.md) — FCFS and
  backfill basics, production trigger categories, and current option mappings.
- [Fluxion Validation Plan](FLUX_VALIDATION_PLAN.md) — Small paired workloads,
  emulator synchronization prerequisites, and comparison criteria.
- [Realism and Known Gaps](REALISM_AND_GAPS.md) — Confirmed differences in
  triggers and backfill evaluation budgets, excluded scope, and validation limits.
- [Performance Analysis](PERFORMANCE_ANALYSIS.md) — Linux `perf` breakdown of
  the circular FCFS scheduler on the June 2022 RIKEN trace and its primary
  optimization targets.
- [Scheduling Predictions and Reservations](SCHEDULING_PREDICTIONS.md) —
  Design for reservation reporting, output-free lookahead, and low-cost
  queue-drain estimates.
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
