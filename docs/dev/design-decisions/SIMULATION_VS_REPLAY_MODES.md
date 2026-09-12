# Simulation and Replay Modes

DR_EVT distinguishes scheduler-driven simulation from replay of a recorded
schedule. User-facing input requirements are defined in
[Input Trace Files](../../user-guide/trace-formats.md), and runtime controls in
[Command-Line Options](../../user-guide/command-line.md).

## Mode selection

The simple CSV parser selects the mode from its columns:

- Without `begin_time` and `end_time`, the scheduler computes job start and
  finish times.
- With both columns, DR_EVT replays the recorded intervals without making
  scheduling decisions.
- Supplying only one of the two columns is rejected.

This keeps historical schedule data separate from inputs for what-if
simulation.

## Runtime model

In simulation mode, `time_limit` is the scheduler's planning estimate. The
execution duration comes from one of three models:

- `actual` reads an observed duration from the input;
- `limit` uses `time_limit`; and
- `distribution` samples a configured distribution.

The scheduler therefore does not use a future computed finish time when making
a reservation. Once a job starts, its selected duration determines the finish
event.

In replay mode, `begin_time` and `end_time` are authoritative. Their
difference supplies the duration, and the resource-accounting path processes
those recorded intervals directly.

## Record representation

A job record stores submission time, requested nodes, time limit, selected
actual duration, and its eventual start and finish times. Simulation fills the
last two fields when scheduling the job; replay loads them from the trace.

Generated schedule and resource columns are documented in
[Output Trace Files](../../user-guide/output-traces.md).

## Invariants

For accepted jobs:

- finish time is not earlier than start time;
- start time is not earlier than submission time; and
- runtime is positive.

Simulation additionally requires the recorded finish to equal the computed
start plus the selected runtime. Replay preserves the supplied start and finish
times.

## Implementation

Mode detection is implemented by `Data_Columns`. Duration selection and
scheduler dispatch are implemented by `Simulation`, while replay event
processing is owned by `Trace`.

Tests and fixtures are listed in the
[replay tests section](https://github.com/LLNL/dr_evt/blob/main/tests/README.md#replay-tests).
