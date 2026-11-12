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
[`Simulation(params, job_cost_function, backfill_selector)`][cpp-simulation-simulation]. The cost callback
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
| `run_time_mode` | [`RunTimeMode`][cpp-runtimemode] |
| `backfill_policy` | [`BackfillPolicy`][cpp-backfillpolicy] |
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
| `priority_policy` | [`PriorityPolicy`][cpp-prioritypolicy] |
| `node_release_delay` | `float`; seconds an ended job's nodes stay unavailable (0 disables) |
| `node_release_delay_model` | [`ReleaseDelayModel`][cpp-releasedelaymodel] |
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

Method links open the corresponding C++ API descriptions; the signatures below
are Python signatures. `get_trace_size()` is a Python-only convenience wrapper.

| Method | Result |
|---|---|
| [`run()`][cpp-simulation-run] | Run a configured batch trace to completion. |
| [`initialize_trace(max_jobs=0)`][cpp-simulation-initialize-trace] | Load the configured trace and return the number loaded. |
| [`append_job(submit_time, num_nodes, queue, limit_time, actual_run_time=None)`][cpp-simulation-append-job] | Append and enqueue one live job; return its ID. Jobs must be appended in non-decreasing `submit_time` order; the limit must be a positive whole number of seconds; a known runtime must be positive and no greater than the limit. |
| [`append_jobs(requests)`][cpp-simulation-append-jobs] | Atomically append and enqueue ordered `JobAppendRequest` values; return their IDs. |
| [`get_job_statuses(job_idxs)`][cpp-simulation-get-job-statuses] | Return lifecycle and timing snapshots for appended job IDs. |
| [`get_start_times_by_est_caches()`][cpp-simulation-get-start-times-by-est-caches] | Read the latest live-planner predictions and their planning timestamp, without running a forecast. |
| [`advance_to(target_time)`][cpp-simulation-advance-to] | Process events at or before the target. |
| [`run_until_exclusive(target_time)`][cpp-simulation-run-until-exclusive] | Process events strictly before the target. |
| [`save_checkpoint(filename)`][cpp-simulation-save-checkpoint] | Save complete simulation state to a same-build binary checkpoint. |
| [`load_checkpoint(filename)`][cpp-simulation-load-checkpoint] | Replace current state from a compatible checkpoint. |
| [`get_current_time()`][cpp-simulation-get-current-time] | Return current simulation time. |
| [`get_nodes_in_use()`][cpp-simulation-get-nodes-in-use] | Return allocated nodes. |
| [`get_current_utilization()`][cpp-simulation-get-current-utilization] | Return instantaneous usage of effective scheduled capacity. |
| [`get_resource_area()`][cpp-simulation-get-resource-area] | Return Custom-FCFS allocated-node area in node-seconds; unavailable for standard schedulers. |
| [`get_available_nodes()`][cpp-simulation-get-available-nodes] | Return nodes a new job could use now; excludes releasing nodes. |
| [`get_nodes_releasing()`][cpp-simulation-get-nodes-releasing] | Return nodes whose job has ended but which are not yet reusable (node release delay). |
| [`get_active_job_count()`][cpp-simulation-get-active-job-count] | Return waiting jobs. |
| [`get_fcfs_head_shadow_time()`][cpp-simulation-get-fcfs-head-shadow-time] | Return the FCFS-head reservation time, or `-1` when no head waits or no projected release or scheduled capacity change lets it start. |
| [`get_backfill_window()`][cpp-simulation-get-backfill-window] | Return the current FCFS/EASY reservation snapshot, including all running-job releases. |
| [`estimate_queue_horizon(utilization)`][cpp-simulation-estimate-queue-horizon] | On demand, estimate the supported FCFS/EASY waiting-queue drain time from the shadow time. |
| [`get_start_times_by_queue_drain(utilization, estimation_depth=0)`][cpp-simulation-get-start-times-by-queue-drain] | Return queue-order resource-area start approximations; protected reservations are exact, other results are optimistic. |
| [`get_statistics()`][cpp-simulation-get-statistics] | Return a [`Statistics`][cpp-simulation-statistics] snapshot. |
| [`write_simulated_trace()`][cpp-simulation-write-simulated-trace] | Write the configured job-schedule output. |
| [`print_stats()`][cpp-simulation-print-stats] | Print summary statistics. |
| `get_trace_size()` | Return the number of records currently in the job store. |

Detailed time-advancement and job-submission contracts are defined in the
[Streaming API](STREAMING_API.md).

See [Checkpoint/Restart](../user-guide/checkpoint-restart.md) for build
requirements, matching `SimParams`, callback compatibility, supported trace
modes, progressive loading, and output recovery.

## Supporting types

[`get_start_times_by_est_caches()`][cpp-simulation-get-start-times-by-est-caches] returns `EstimatedJobStarts` with optional `estimated_at`
and a list of `EstimatedJobStart` values. Each contains `job_idx`, optional
`expected_start_time`, and `start_time_kind`. Eligible pending jobs are sorted
by expected start, with unknowns last. Predictions are saved during live
scheduling, so queue limits and early exits can leave jobs without estimates.
Out-of-horizon estimates can be present without protection. A periodic plan
can be older than the current time; no cached plan survives checkpoint restore.

```python
plan = sim.get_start_times_by_est_caches()
for job in plan.jobs:
    print(plan.estimated_at, job.job_idx, job.expected_start_time)
```

This follows Slurm's `squeue --start` reporting model. [`get_job_statuses()`][cpp-simulation-get-job-statuses]
reuses cached starts by default and forecasts missing starts. See
{ref}`Public interface behavior <scheduler-public-interface-behavior>`
for the comparison with Slurm `sbatch --test-only` and exact lookahead.

`JobAppendRequest(submit_time, num_nodes, queue, limit_time,
actual_run_time=None)` represents one entry passed to `append_jobs()`. A known
actual runtime lets `RunTimeMode.ACTUAL` complete a streamed job before its
wall-time limit without any external per-job bookkeeping.

[`get_job_statuses()`][cpp-simulation-get-job-statuses] preserves the requested ID order (including duplicates).
It first uses live cached starts within the requested queue depth, retaining
their `RESERVED` or `ESTIMATED` kind. With default reuse, a cache-only request skips forecasting.
Cached starts before the current time are recomputed; other cached starts may
remain stale between periodic passes. Use
[`get_job_statuses(job_idxs, use_cached_predictions=False)`][cpp-simulation-get-job-statuses] for the
forecast without backfill described below (cache reuse disabled).
Set `use_cached_unprotected_predictions=False` with cache reuse enabled to
preserve compatible cached reservation windows and forecast unprotected jobs.
The default reuses both protected and unprotected cached starts. Setting
`use_cached_predictions=False` recomputes all pending starts, including
reservations, and ignores the second cache flag. Recomputed reservations may
match saved reservations: future backfill can fill gaps without delaying them.
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
[`get_job_statuses()`][cpp-simulation-get-job-statuses] to limit temporary planning and pending timing to a queue
prefix; zero is the default and considers the complete waiting queue. This
changes neither the scheduler nor its reservation plan. Running and completed jobs
have `start_time` and `end_time`; rejected jobs have no timing. The compact
timing record remains available after completed trace rows are flushed.

Pass an empty `job_idxs` list to enumerate waiting jobs in scheduler queue
order; `estimation_depth` then also limits the returned queue prefix.

Cache-first reporting is a reasonable default when duration estimates are
uncertain: it reuses live planning work and cheaply extends coverage. The
validation workload showed that full replay with overestimated durations
cost more without improving start-time accuracy. Replay became substantially
more accurate when execution durations matched time limits. Prefer replay
when duration estimates are reliable or future policy behavior is the focus;
the benchmark does not establish an accuracy threshold for that choice. See
{ref}`the measured comparison <choosing-how-much-computation-to-spend>`.

[`get_start_times_by_queue_drain(utilization, estimation_depth=0)`][cpp-simulation-get-start-times-by-queue-drain] is a separate
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

[`Statistics`][cpp-simulation-statistics] exposes:

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
[`get_current_utilization()`][cpp-simulation-get-current-utilization], intervals that are far apart therefore carry
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

[cpp-backfillpolicy]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/scheduling-backfill.html#_CPPv414BackfillPolicy
[cpp-prioritypolicy]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/scheduling-backfill.html#_CPPv414PriorityPolicy
[cpp-releasedelaymodel]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/scheduling-backfill.html#_CPPv417ReleaseDelayModel
[cpp-runtimemode]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/scheduling-backfill.html#_CPPv411RunTimeMode
[cpp-simulation-advance-to]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation10advance_toE10sim_time_t
[cpp-simulation-append-job]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation10append_jobE10sim_time_t11num_nodes_tRKNSt6stringE7tdiff_tNSt8optionalI7tdiff_tEE
[cpp-simulation-append-jobs]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation11append_jobsERKNSt6vectorI18Job_Append_RequestEE
[cpp-simulation-estimate-queue-horizon]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation22estimate_queue_horizonEd
[cpp-simulation-get-active-job-count]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation20get_active_job_countEv
[cpp-simulation-get-available-nodes]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation19get_available_nodesEv
[cpp-simulation-get-backfill-window]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation19get_backfill_windowEv
[cpp-simulation-get-current-time]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation16get_current_timeEv
[cpp-simulation-get-current-utilization]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation23get_current_utilizationEv
[cpp-simulation-get-fcfs-head-shadow-time]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation25get_fcfs_head_shadow_timeEv
[cpp-simulation-get-job-statuses]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation16get_job_statusesERKNSt6vectorI8job_no_tEE6size_tbb
[cpp-simulation-get-nodes-in-use]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation16get_nodes_in_useEv
[cpp-simulation-get-nodes-releasing]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation19get_nodes_releasingEv
[cpp-simulation-get-resource-area]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation17get_resource_areaEv
[cpp-simulation-get-start-times-by-est-caches]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation29get_start_times_by_est_cachesEv
[cpp-simulation-get-start-times-by-queue-drain]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation30get_start_times_by_queue_drainEd6size_t
[cpp-simulation-get-statistics]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation14get_statisticsEv
[cpp-simulation-initialize-trace]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation16initialize_traceE10num_jobs_t
[cpp-simulation-load-checkpoint]: https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4N6dr_evt15BasicSimulation15load_checkpointERKNSt6stringE
[cpp-simulation-print-stats]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation11print_statsERNSt7ostreamE
[cpp-simulation-run]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation3runEv
[cpp-simulation-run-until-exclusive]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation19run_until_exclusiveE10sim_time_t
[cpp-simulation-save-checkpoint]: https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4N6dr_evt15BasicSimulation15save_checkpointERKNSt6stringE
[cpp-simulation-simulation]: https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4N6dr_evt15BasicSimulation15BasicSimulationE10Sim_Params19job_cost_function_t19backfill_selector_t
[cpp-simulation-statistics]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation10StatisticsE
[cpp-simulation-write-simulated-trace]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation21write_simulated_traceEv

## Resource model diagnostics

Set `params.resource_model_diagnostics = True` before constructing
`dr_evt.Simulation(params)` to select per-simulation operation counters.
`sim.get_resource_diagnostics()` returns a `ResourceDiagnostics` snapshot, or
`None` when disabled. Its `calls` and `quantities` arrays are ordered as
`fit_now`, `find_fit`, `reserve`, `projected_start`, `allocate`, `release`, and
`hold`; `failed_fits` counts failed immediate and future fit queries. Snapshots
own their values, so advancing or destroying the simulation does not change them.
See [Resource diagnostics](../dev/RESOURCE_DIAGNOSTICS.md) for coverage and lifetime.

## Exact placement resources

Select `resource_model = "placement"` in SimParams (Python uses
`params.resource_model`) or `resource_model: "placement"` in gRPC InitRequest.
`placement_order` defaults to `"low"`, `placement_optimizations` to `"all"`, and
`placement_coarse_filter` to false. The CLI/protobuf/Python options select the
same simulator implementation. See
[Resource Models](../dev/RESOURCE_MODELS.md) for supported configurations.

JobStatus.node_ids contains exact live or reserved node IDs. A pending status
reports only the latest live reservation, with kind RESERVED; jobs without a
reservation have no pending estimate. The query does not substitute an aggregate
forecast by default. Disabling either cache-reuse flag selects an aggregate
approximation, which omits node identities. Use exact lookahead for a fresh
placement forecast when Ser20 is enabled. Query depth limits pending
reporting, and queries preserve requested order and duplicates.

Exact allocations survive END through delayed RELEASE and checkpoint restart.
Released IDs are not retained in completed statuses. Reservation reporting
snapshots, like existing estimate caches, are rebuilt by scheduling after
checkpoint restore. Resource CSV output continues to report aggregate counts.

Aggregate queue-drain and capacity-area horizon forecasts remain available in
placement mode as approximations. Their results omit identity constraints;
policy protection in an aggregate forecast does not establish an exact node
reservation. Backfill-window summaries contain counts and the actual head start,
but cannot establish an exact placement fit.
