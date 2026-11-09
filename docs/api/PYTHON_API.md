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

For experimental external backfill selection, set
`params.num_max_candidates`, then call
`Simulation(params, job_cost_function, backfill_selector)`. The cost callback
receives `(job_id, submit_time, runtime_estimate, nodes_requested)`. The
selector receives a list of `(job_id, cost)` pairs and returns one offered ID
or `None`.

## Configuration

`SimParams` currently exposes these mutable attributes:

| Attribute | Type |
|---|---|
| `infile` | `str` |
| `redis_uri` | `str` |
| `redis_key_prefix` | `str` |
| `checkpoint_file` | `str` |
| `checkpoint_interval_jobs` | `int` |
| `total_nodes` | `int` |
| `capacity_schedule` | `str` |
| `sim_start_time` | `float` |
| `trace_format` | `str` |
| `timestamp_format` | `str` |
| `run_time_mode` | `RunTimeMode` |
| `backfill_policy` | `BackfillPolicy` |
| `num_max_candidates` | `int` |
| `reservation_depth` | `int` |
| `backfill_queue_depth` | `int`; waiting jobs examined per backfill pass (0 is unlimited) |
| `per_pass_accounting` | `bool`; count whole event-driven placement passes, reset Q/R per pass, and guard reservation-only retries (default `False`) |
| `dispatch_interval` | `float`; aligned ticks run both FCFS and backfill (0 disables) |
| `dispatch_queue_depth` | `int`; full candidate prefix per dispatch wake, including immediate starts (0 is unlimited) |
| `dispatch_on_events` | `bool`; also dispatch on job arrival batches without resetting ticks; requires positive `dispatch_interval` (default `False`) |
| `backfill_interval` | `float`; seconds between periodic backfill passes (0 backfills at every event) |
| `planning_time_resolution` | `float`; grid planned run times are rounded up to (0 is exact) |
| `planning_horizon` | `float`; reservation look-ahead of event-driven and periodic backfill (default unlimited) |
| `priority_policy` | `PriorityPolicy` |
| `node_release_delay` | `float`; seconds an ended job's nodes stay unavailable (0 disables) |
| `node_release_delay_model` | `ReleaseDelayModel` |
| `node_release_delay_spread` | `float`; `UNIFORM` half-width |
| `verbose` | `bool` |

The scheduling attributes mirror the
`--backfill_queue_depth`, `--per_pass_accounting`, `--backfill_interval`, `--dispatch_interval`,
`--dispatch_queue_depth`, `--dispatch_on_events`, `--planning_time_resolution`, and `--planning_horizon` command-line options
described in [Command-Line Options](../user-guide/command-line.md). A positive
`backfill_interval` keeps FCFS dispatch event-driven and runs backfill only at
ticks; it needs an enabled `backfill_policy` and FCFS ordering, and the
circular wait queue the binding uses by default.

`sim_start_time` is the global simulation boundary; it is distinct from each
job's historical `begin_time`. A positive value enables replay-based warm start
for replay input, while zero preserves ordinary full replay.

Set both Redis fields to direct the simulated-job schedule to the searchable
Redis schema and the resource history to `<redis_key_prefix>:resources:csv`, as
documented in [Redis Output](../user-guide/redis-output.md). The extension must
be built with `DR_EVT_WITH_REDIS=ON`.

Other C++/CLI configuration fields are not exposed by the binding. Use the
`simulator` executable when one of those settings is required; its options
are documented in [Command-Line Options](../user-guide/command-line.md).

The module exports `legacy_queue_input`, a Boolean indicating whether queue
arguments use legacy names or numeric IDs.

## Enumerations

- `RunTimeMode.ACTUAL`, `RunTimeMode.DISTRIBUTION`,
  `RunTimeMode.LIMIT`
- `BackfillPolicy.NONE`, `BackfillPolicy.EASY`,
  `BackfillPolicy.CONSERVATIVE`, `BackfillPolicy.HYBRID`
- `PriorityPolicy.FCFS`, `PriorityPolicy.FCFS_CONSERVATIVE`,
  `PriorityPolicy.FCFS_1S_EVT_GROUP`, `PriorityPolicy.SJF`, `PriorityPolicy.LJF`
- `ReleaseDelayModel.CONSTANT`, `ReleaseDelayModel.EXPONENTIAL`,
  `ReleaseDelayModel.UNIFORM` (see `--node_release_delay_model` in
  [Command-Line Options](../user-guide/command-line.md))
- `JobState.PENDING`, `JobState.RUNNING`, `JobState.COMPLETED`,
  `JobState.REJECTED` (returned by the job status query)

Their scheduling semantics are documented in
[Command-Line Options](../user-guide/command-line.md) and
[Backfilling Algorithms](../BACKFILLING_ALGORITHMS.md).

## Simulation methods

| Method | Result |
|---|---|
| `run()` | Run a configured batch trace to completion. |
| `initialize_trace(max_jobs=0)` | Load the configured trace and return the number loaded. |
| `append_job(submit_time, num_nodes, queue, limit_time, actual_run_time=None)` | Append and enqueue one live job; return its ID. Jobs must be appended in non-decreasing `submit_time` order; the limit must be a positive whole number of seconds; a known runtime must be positive and no greater than the limit. |
| `append_jobs(requests)` | Atomically append and enqueue ordered `JobAppendRequest` values; return their IDs. |
| `get_job_statuses(job_idxs)` | Return lifecycle and timing snapshots for appended job IDs. |
| `get_planned_job_starts()` | Read the latest live-planner predictions and their planning timestamp, without running a forecast. |
| `advance_to(target_time)` | Process events at or before the target. |
| `run_until_exclusive(target_time)` | Process events strictly before the target. |
| `save_checkpoint(filename)` | Save complete simulation state to a same-build binary checkpoint. |
| `load_checkpoint(filename)` | Replace current state from a compatible checkpoint. |
| `get_current_time()` | Return current simulation time. |
| `get_nodes_in_use()` | Return allocated nodes. |
| `get_current_utilization()` | Return instantaneous usage of effective scheduled capacity. |
| `get_resource_area()` | Return Custom-FCFS allocated-node area in node-seconds; unavailable for standard schedulers. |
| `get_available_nodes()` | Return nodes a new job could use now; excludes releasing nodes. |
| `get_nodes_releasing()` | Return nodes whose job has ended but which are not yet reusable (node release delay). |
| `get_active_job_count()` | Return waiting jobs. |
| `get_fcfs_head_shadow_time()` | Return the FCFS-head reservation time, or `-1` when no head waits or no projected release or scheduled capacity change lets it start. |
| `get_backfill_window()` | Return the current FCFS/EASY reservation snapshot, including all running-job releases. |
| `get_prediction_horizon(utilization)` | On demand, estimate the supported FCFS/EASY waiting-queue drain time from the shadow time. |
| `get_queue_drain_start_times(utilization, estimation_depth=0)` | Return queue-order resource-area start approximations; protected reservations are exact, other results are optimistic. |
| `get_statistics()` | Return a `Statistics` snapshot. |
| `write_simulated_trace()` | Write the configured job-schedule output. |
| `print_stats()` | Print summary statistics. |
| `get_trace_size()` | Return the number of records currently in the job store. |

Detailed time-advancement and job-submission contracts are defined in the
[Streaming API](STREAMING_API.md).

See [Checkpoint/Restart](../user-guide/checkpoint-restart.md) for build
requirements, matching `SimParams`, callback compatibility, supported trace
modes, progressive loading, and output recovery.

## Supporting types

`get_planned_job_starts()` returns `PlannedJobStarts` with optional `planned_at`
and a list of `PlannedJobStart` values. Each contains `job_idx`, optional
`expected_start_time`, and `start_time_kind`. Eligible pending jobs are sorted
by expected start, with unknowns last. Predictions are saved during live
scheduling, so queue limits and early exits can leave jobs without estimates.
Out-of-horizon estimates can be present without protection. A periodic plan
can be older than the current time; no cached plan survives checkpoint restore.

```python
plan = sim.get_planned_job_starts()
for job in plan.jobs:
    print(plan.planned_at, job.job_idx, job.expected_start_time)
```

This follows Slurm's `squeue --start` reporting model. `get_job_statuses()`
instead calculates fresh pending forecasts when called. See
[Scheduling Predictions](../dev/SCHEDULING_PREDICTIONS.md#slurm-comparison)
for the comparison with Slurm `sbatch --test-only` and exact lookahead.

`JobAppendRequest(submit_time, num_nodes, queue, limit_time,
actual_run_time=None)` represents one entry passed to `append_jobs()`. A known
actual runtime lets `RunTimeMode.ACTUAL` complete a streamed job before its
wall-time limit without any external per-job bookkeeping.

`get_job_statuses()` preserves the requested ID order (including duplicates).
A pending `JobStatus` has `state == JobState.PENDING` and an
`expected_start_time` projected from current capacity, running-job limit-time
releases, and the scheduler's current queue order. Each pending job is placed
at its earliest fit for its whole time limit after the jobs ahead of it are
reserved, so a short job behind a blocked head may be projected before the
head. `JobStatus.start_time_kind` is `StartTimeKind.RESERVED` for a protected
scheduler reservation and `StartTimeKind.ESTIMATED` for a non-binding fit
that respects protected reservations. Every projected placement consumes its
requested time window before the next queue job is placed; estimates are still
non-binding forecasts based on requested time limits, not start guarantees.
Pass `estimation_depth` to
`get_job_statuses()` to limit temporary planning and pending timing to a queue
prefix; zero is the default and considers the complete waiting queue. This
changes neither the scheduler nor its reservation plan. Running and completed jobs
have `start_time` and `end_time`; rejected jobs have no timing. The compact
timing record remains available after completed trace rows are flushed.

Pass an empty `job_idxs` list to enumerate waiting jobs in scheduler queue
order; `estimation_depth` then also limits the returned queue prefix.

`get_queue_drain_start_times(utilization, estimation_depth=0)` is a separate
fast resource-area approximation. It returns queue-order estimates without
per-job first-fit placement; it is available through C++ and Python, not gRPC.

With `PriorityPolicy.FCFS_1S_EVT_GROUP`, both pending-status and queue-drain
queries use inclusive one-second forecast event groups. Each query selects
its compiled forecast implementation once before scanning the waiting queue.
`if constexpr` removes grouping checks and grouping-event storage from the
ordinary query implementation. Actual arrival and completion events keep
their original times.

`BackfillWindow` exposes `current_time`, `available_nodes`,
`shadow_time`, and an ordered list of `ResourceRelease` values. Each release
contains `time` and `nodes_released`.

`Statistics` exposes:

- `jobs_submitted` (jobs accepted into the wait queue), `jobs_started`,
  `jobs_completed`, `jobs_running`, and `jobs_waiting`;
- `current_time`, `total_nodes`, `nodes_in_use`, and
  `nodes_available`; and
- `resource_area`, `utilization`, `avg_wait_time`, `avg_run_time`,
  `avg_turnaround_time`, `avg_bounded_slowdown`, and `makespan`.

`avg_bounded_slowdown` is the mean of
`max(1, turnaround / max(run_time, 10 seconds))` over completed jobs.

For simulations constructed with Custom-FCFS callbacks, `resource_area` is
accumulated as `nodes_in_use * interval` between scheduling timestamps, using
occupancy after all events and scheduling decisions at each timestamp.
`utilization` divides that area by integrated effective capacity over the
accounting horizon (the snapshot time while work is running, or the last
resource event after it becomes idle). During non-preemptive draining,
effective capacity is at least the running allocation. Unlike
`get_current_utilization()`, intervals that are far apart therefore carry
proportionally more weight. Standard schedulers retain the post-hoc
scheduled-workload calculation and do not perform live area bookkeeping.

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
