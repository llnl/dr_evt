# Python API

<div class="api-search" role="search">
  <label for="python-api-search">Search Python API</label>
  <input id="python-api-search" data-api-search="python-api-content" type="search" placeholder="e.g., SimParams, append_job, statistics" autocomplete="off">
  <span class="api-search-status" aria-live="polite"></span>
</div>

<div id="python-api-content" class="api-search-content">

The `dr_evt` extension exposes batch and incremental simulation through
pybind11. Build and import instructions are in
[Installation](../getting-started/installation.md#cmake-configuration-options).

## Example

```python
import dr_evt

params = dr_evt.SimParams()
params.infile = "jobs.csv"
params.total_nodes = 100
params.trace_format = "simple"
params.timestamp_format = "epoch"
params.run_time_mode = dr_evt.RunTimeMode.LIMIT

sim = dr_evt.Simulation(params)
queue = "pbatch" if dr_evt.legacy_queue_input else "1"
sim.append_job(0.0, 10, queue, 100.0)
sim.advance_to(0.0)

stats = sim.get_statistics()
print(stats.jobs_running, stats.nodes_in_use)
```

A complete runnable example is
[`python/example_streaming.py`](https://github.com/LLNL/dr_evt/blob/main/python/example_streaming.py).

## Configuration

`SimParams` currently exposes these mutable attributes:

| Attribute | Type |
|---|---|
| `infile` | `str` |
| `total_nodes` | `int` |
| `trace_format` | `str` |
| `timestamp_format` | `str` |
| `run_time_mode` | `RunTimeMode` |
| `backfill_policy` | `BackfillPolicy` |
| `priority_policy` | `PriorityPolicy` |
| `verbose` | `bool` |

Other C++/CLI configuration fields are not exposed by the binding. Use the
`simulator` executable when one of those settings is required; its options
are documented in [Command-Line Options](../user-guide/command-line.md).

The module exports `legacy_queue_input`, a Boolean indicating whether queue
arguments use legacy names or numeric IDs.

## Enumerations

- `RunTimeMode.ACTUAL`, `RunTimeMode.DISTRIBUTION`,
  `RunTimeMode.LIMIT`
- `BackfillPolicy.NONE`, `BackfillPolicy.EASY`,
  `BackfillPolicy.CONSERVATIVE`
- `PriorityPolicy.FCFS`, `PriorityPolicy.FCFS_CONSERVATIVE`,
  `PriorityPolicy.SJF`, `PriorityPolicy.LJF`

Their scheduling semantics are documented in
[Command-Line Options](../user-guide/command-line.md) and
[Backfilling Algorithms](../BACKFILLING_ALGORITHMS.md).

## Simulation methods

| Method | Result |
|---|---|
| `run()` | Run a configured batch trace to completion. |
| `initialize_trace(max_jobs=0)` | Load the configured trace and return the number loaded. |
| `append_job(submit_time, num_nodes, queue, limit_time)` | Append and enqueue one live job; return its ID. |
| `append_jobs(requests)` | Atomically append and enqueue ordered `JobAppendRequest` values; return their IDs. |
| `advance_to(target_time)` | Process events at or before the target. |
| `run_until_exclusive(target_time)` | Process events strictly before the target. |
| `get_current_time()` | Return current simulation time. |
| `get_nodes_in_use()` | Return allocated nodes. |
| `get_available_nodes()` | Return free nodes. |
| `get_active_job_count()` | Return waiting jobs. |
| `get_fcfs_head_shadow_time()` | Return the FCFS-head reservation time, or `-1`. |
| `get_backfill_window()` | Return the current FCFS/EASY reservation snapshot. |
| `get_statistics()` | Return a `Statistics` snapshot. |
| `write_simulated_trace()` | Write the configured job-schedule output. |
| `print_stats()` | Print summary statistics. |
| `get_trace_size()` | Return the number of records currently in the job store. |

Detailed time-advancement and job-submission contracts are defined in the
[Streaming API](STREAMING_API.md).

## Supporting types

`JobAppendRequest(submit_time, num_nodes, queue, limit_time)` represents one
entry passed to `append_jobs()`.

`BackfillWindow` exposes `current_time`, `available_nodes`,
`shadow_time`, and an ordered list of `ResourceRelease` values. Each release
contains `time` and `nodes_released`.

`Statistics` exposes:

- `jobs_submitted`, `jobs_completed`, `jobs_running`, and
  `jobs_waiting`;
- `current_time`, `total_nodes`, `nodes_in_use`, and
  `nodes_available`; and
- `utilization`, `avg_wait_time`, `avg_turnaround_time`, and
  `makespan`.

Metric definitions are in
[Output Trace Files](../user-guide/output-traces.md#cli-summary).

## Testing and source

The binding is defined in
[`python/dr_evt_bindings.cpp`](https://github.com/LLNL/dr_evt/blob/main/python/dr_evt_bindings.cpp).
Python test commands and status are maintained in the
[Python API tests section](https://github.com/LLNL/dr_evt/blob/main/tests/README.md#python-api-tests)
of the Test Suite README. The test implementation is
[`tests/test_python_api.py`](https://github.com/LLNL/dr_evt/blob/main/tests/test_python_api.py).

</div>
