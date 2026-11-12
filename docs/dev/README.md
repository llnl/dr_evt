# Developer Documentation

This section contains implementation notes, architectural rationale, and
maintainer-facing build guidance. It is separate from the user documentation.

## Developer guides

- [Wait Queues](WAIT_QUEUES.md) — Queue selection, end-to-end performance,
  and the circular and block queue guides.
- [Simulation Pipeline and Job Lifecycle](JOB_LIFECYCLE.md) — How input moves
  through the job store, scheduler, event loop, resource accounting, and output.
- [Performance Analysis](PERFORMANCE_ANALYSIS.md) — Current simulator timings, reproduction instructions,
  and a short optimization history.
- [Compile-time Resource Model Plan](RESOURCE_MODEL_PLAN.md) — Standalone count
  model, correctness hooks, overhead measurements, and future resource models.
- [Scheduling Predictions and Reservations](SCHEDULING_PREDICTIONS.md) —
  Reservation reporting, lookahead, and queue-drain estimates, including
  [performance and accuracy results](SCHEDULING_PREDICTIONS.md#performance-and-accuracy-comparison).
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
for the Doxygen comments on [`append_job`][cpp-simulation-append-job], [`submit_job`][cpp-simulation-submit-job], and
[`SchedulerBase::insert_job()`][cpp-schedulerbase-insert-job] / [`Trace::insert_job()`][cpp-trace-insert-job].

[cpp-schedulerbase-insert-job]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/scheduling-backfill.html#_CPPv4N6dr_evt13SchedulerBase10insert_jobE8job_no_t10sim_time_t7tdiff_t11num_nodes_t
[cpp-simulation-append-job]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation10append_jobE10sim_time_t11num_nodes_tRKNSt6stringE7tdiff_tNSt8optionalI7tdiff_tEE
[cpp-simulation-submit-job]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation10submit_jobE8job_no_t10sim_time_t10sim_time_t
[cpp-trace-insert-job]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/trace-replay.html#_CPPv4N6dr_evt10BasicTrace10insert_jobE8job_no_t10sim_time_t
