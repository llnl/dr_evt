# Terminology

**Reference implementation** or **Python reference** means
`scripts/python_reference_scheduler.py`, the separately written EASY
backfilling implementation used for consistency checks. Its generated output
is not itself an independently proven result.

**Expected output** or **reference output** means a committed validation file,
normally `.expected_output.csv` or `.expected_resources.csv`. Do not use
"reference implementation" to mean one of these files.

**Run-time mode** controls how a simulation obtains a job's observed execution
length. The scheduler still uses `time_limit` for planning. Modes and input
column aliases are defined in
[Command-Line Options](../user-guide/command-line.md#simulation-mode-options)
and [Input Trace Files](../user-guide/trace-formats.md).

Scheduler fixture names and file roles are documented in the
[Scheduler Correctness Fixtures README](https://github.com/LLNL/dr_evt/blob/main/tests/test_traces/scheduler_correctness/README.md).
