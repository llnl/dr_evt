# C++ Streaming API Reference

## Overview

The DR_EVT simulator provides a streaming API that allows external code (e.g., gRPC servers - see [gRPC Client/Server Guide and API](../CLIENT_SERVER_GUIDE.md), workflow managers) to feed jobs dynamically and control simulation time advancement. This enables online/incremental simulation where jobs arrive over time rather than all at once.

Batch mode loads a trace and completes it with [`run()`][cpp-simulation-run]. Streaming mode adds
new jobs with [`append_job()`][cpp-simulation-append-job] or [`append_jobs()`][cpp-simulation-append-jobs] and lets the caller advance
simulation time. [`advance_to(t)`][cpp-simulation-advance-to] includes events at `t`;
[`run_until_exclusive(t)`][cpp-simulation-run-until-exclusive] excludes them.

## C++ Development API Reference

These are direct methods on [`dr_evt::Simulation`][cpp-simulation]; they do not require a
server or gRPC. The gRPC service maps its request messages onto these methods
where applicable; see the [Client/Server Guide](../CLIENT_SERVER_GUIDE.md) for
the wire protocol.

:::{only} doxygen
See the [complete generated C++ Development API Reference](CPP_API.md).
:::

:::{only} not doxygen
The complete generated C++ Development API Reference is unavailable because Doxygen was not
run for this build.
:::

For live jobs, [`append_job()`][cpp-simulation-append-job] (or [`append_jobs()`][cpp-simulation-append-jobs]) creates a new
job and enqueues it atomically for scheduling.

### [`initialize_trace(max_jobs = 0)`][cpp-simulation-initialize-trace]

Loads trace data and prepares it for either batch or streaming use: sorts jobs by submit time and determines actual durations (simulation mode only).

```cpp
num_jobs_t initialize_trace(num_jobs_t max_jobs = 0);
```

**Parameters:**
- `max_jobs`: Maximum number of jobs to load (0 = no limit)

**Returns:** number of jobs actually loaded

Call this before [`advance_to()`][cpp-simulation-advance-to]. Calling [`get_trace()`][cpp-simulation-get-trace].[`load_data()`][cpp-trace-load-data] directly
skips sorting and duration determination. The method is idempotent and clears
loaded data before reinitializing it.

**Example:**
```cpp
Simulation sim(params);
num_jobs_t num_jobs = sim.initialize_trace();
std::cout << "Loaded " << num_jobs << " jobs\n";
```

### [`append_job(submit_time, num_nodes, queue, limit_time, actual_run_time)`][cpp-simulation-append-job]

Adds a genuinely new job - one the trace has never seen before - to the
job store and immediately enqueues it for scheduling. This is how a job the
caller learns about live (for example, from a network event) enters a
streaming simulation.

```cpp
job_no_t append_job(sim_time_t submit_time, num_nodes_t num_nodes,
                     const std::string& queue, tdiff_t limit_time,
                     std::optional<tdiff_t> actual_run_time = std::nullopt);
```

**Parameters:**
- `submit_time`: When the job is submitted (must be >= current_time and >=
  the submit time of every job appended before it; appends are released in
  insertion order, so an earlier time would otherwise be held back silently)
- `num_nodes`: Number of nodes the job requests
- `queue`: Numeric queue ID (for example, `"1"`) in the default build, or a
  queue name (for example, `"pbatch"`) with `DR_EVT_LEGACY_QUEUE_INPUT`
- `limit_time`: Positive whole-number time limit, in seconds. Fractional,
  non-finite, zero, and negative values are rejected.
- `actual_run_time`: Optional known execution duration. It must be positive
  and no greater than `limit_time`. When omitted, streaming jobs retain the
  existing behavior of running for `limit_time`.

**Returns:** the new job's `job_no`

**Example:**
```cpp
job_no_t j = sim.append_job(10.0, 20, "1", 200.0);
sim.advance_to(10.0);
```

### [`append_jobs(requests)`][cpp-simulation-append-jobs]

The batch counterpart to [`append_job()`][cpp-simulation-append-job] - several new jobs in one call,
each as a [`Job_Append_Request`][cpp-job-append-request] (the same fields [`append_job()`][cpp-simulation-append-job]
takes, grouped). Resolves job-store capacity once for the whole batch
rather than once per job, so it's the more efficient choice when several
jobs are already known together (e.g. several arrivals collected in one
polling interval), not just a loop over [`append_job()`][cpp-simulation-append-job]. All-or-nothing:
requests must already be sorted by `submit_time` (non-decreasing), and
either the whole batch is appended or, on any failure (unsorted input,
an invalid or fractional `limit_time`,
`--job_store_overflow=abort` with no room even after reclaiming, or
`--check_memory_pressure` refusing the batch under real memory
pressure - see [Command-Line Options](../user-guide/command-line.md)),
none of it is - `m_data` is never left partially filled.

```cpp
std::vector<job_no_t> append_jobs(const std::vector<Job_Append_Request>& requests);
```

**Parameters:**
- `requests`: the new jobs' own data, in `submit_time` order

**Returns:** each new job's `job_no`, in the same order as `requests`.
All returned jobs are already enqueued for scheduling.

**Example:**
```cpp
std::vector<Simulation::Job_Append_Request> batch = {
    {10.0, 20, "1", 200.0},
    {15.0, 10, "1", 100.0},
};
auto job_nos = sim.append_jobs(batch);
```

### [`get_job_statuses(job_nos)`][cpp-simulation-get-job-statuses]

Returns a status snapshot for each ID returned by [`append_job()`][cpp-simulation-append-job] or
[`append_jobs()`][cpp-simulation-append-jobs], preserving request order and duplicate IDs. Pending jobs
include an `expected_start_time` from the live cache where available, with
on-demand forecasting for missing starts. With default reuse, cache-only
requests avoid forecasting;
set the third C++ argument, `use_cached_predictions`, to `false` to request
a forecast without backfill that recomputes all pending starts, including
reservations. Set `use_cached_unprotected_predictions=false` (the fourth C++ argument)
with cache reuse enabled to retain compatible protected windows and forecast
unprotected jobs. Both cache flags default to `true`; disabling cache reuse
ignores the second flag. Once scheduled, a job
instead includes its simulated `start_time` and `end_time` and is reported as
running or completed according to the current simulation time. Rejected jobs
have no timing. Status remains queryable after completed trace records are
flushed; an ID not created by the append APIs raises `std::out_of_range`.
This query uses simulation state and does not require a Redis-enabled build or
a Redis server.

`expected_start_time` is paired with `start_time_kind`. `reserved` identifies
a protected EASY, hybrid, or conservative reservation; `estimated` identifies
a reservation-respecting earliest fit and is not a start-time guarantee.
Every projected placement consumes its requested time window before the next
queue job is placed. Estimates use requested limits for running and protected
work. A policy-exact answer
requires the separate full-lookahead operation. The query builds temporary
capacity state and does not change the scheduler.
`estimation_depth` limits temporary planning and returned pending timing to a
queue prefix; zero, the default, considers the complete waiting queue. It does
not change scheduler configuration or policy.
An empty `job_idxs` list returns that waiting-queue prefix in scheduler order.

### [`get_start_times_by_est_caches()`][cpp-simulation-get-start-times-by-est-caches]

Reads the start predictions saved by the latest live planning pass, following
Slurm's `squeue --start` reporting model. It does not compute another forecast.

```cpp
auto plan = sim.get_start_times_by_est_caches();
// plan.estimated_at: optional simulation timestamp of the live planning pass
for (const auto &job : plan.jobs) {
    // job.job_idx, job.expected_start_time, job.start_time_kind
}
```

The result lists currently eligible pending jobs sorted by expected start,
with unknown starts last. Unexamined jobs have no estimate. A projection
beyond the planning horizon can remain an estimate without protection.
`estimated_at` is absent before planning and after checkpoint restore; with
periodic backfill it may precede current simulation time. Neither new arrivals
nor a status query refresh the cache on their own.

Unlike [`get_job_statuses()`][cpp-simulation-get-job-statuses], this interface reports only the live planner's
saved work and leaves missing starts unknown rather than forecasting them.
The gRPC equivalents are `GetStartTimesByEstCachesRequest` and
`GetStartTimesByEstCachesResponse`. See
{ref}`Public interface behavior <scheduler-public-interface-behavior>`
for the comparison with Slurm `squeue --start` and `sbatch --test-only`.

### [`advance_to(target_time)`][cpp-simulation-advance-to]

Advances simulation to `target_time` and processes all events at that time.

```cpp
void advance_to(sim_time_t target_time);
```

**Parameters:**
- `target_time`: Time to advance to (must be >= current_time)

**Precondition:** the caller guarantees no job will be submitted with `submit_time < target_time` after this call - either all jobs have already been submitted, or the caller knows the next arrival is at `>= target_time`.

**Behavior:**
- Advances through all events up to AND INCLUDING `target_time`
- Scheduler makes decisions at each event
- Jobs may start/end during advancement
- `current_time` becomes a finite `target_time` after the call. Passing the
  maximum representable value drains the simulation and leaves `current_time`
  at the last real event rather than exposing the sentinel as a timestamp.

**Example:**
```cpp
sim.append_job(0.0, 10, "1", 100.0);
sim.advance_to(0.0);  // Process job 0's START event
// Job 0 is now running

sim.advance_to(100.0);  // Process job 0's END event at t=100
// Job 0 has completed
```

### [`flush_completed_jobs()`][cpp-simulation-flush-completed-jobs]

Writes and reclaims the completed contiguous front prefix at the simulation's
current time. This provides an explicit output/checkpoint boundary for a long
streaming session without closing the schedule file. Jobs that are unfinished,
still have pending events, or sit behind such a job remain resident.

```cpp
sim.advance_to(checkpoint_time);
sim.flush_completed_jobs();
```

The `job_flush_interval` setting invokes the same operation periodically after
processed departures. Setting it to `1` attempts a flush after every completed
job. A later job that completes through backfill remains buffered behind any
earlier unfinished job so permanent job-ID order is preserved.

This output flush is not a restart checkpoint. Use [`save_checkpoint()`][cpp-simulation-save-checkpoint] when
the simulation itself must resume later.

### [`save_checkpoint()`][cpp-simulation-save-checkpoint] / [`load_checkpoint()`][cpp-simulation-load-checkpoint]

Save and restore simulation state with matching configuration. See
[Checkpoint/Restart](../user-guide/checkpoint-restart.md) for compatibility,
save/load examples, automatic checkpoints, and output recovery.

### [`run_until_exclusive(target_time)`][cpp-simulation-run-until-exclusive]

Advances simulation to just before `target_time`, excluding events at that exact time.

```cpp
void run_until_exclusive(sim_time_t target_time);
```

**Parameters:**
- `target_time`: Time to advance toward (must be > current_time)

**Behavior:**
- Advances through events BEFORE `target_time`
- Events exactly at `target_time` are NOT processed
- With `--backfill_interval`, a backfill tick before `target_time` counts as
  an event while a job is waiting
- Useful for stopping just before a known event
- `current_time` becomes the last event time < `target_time`

**Example:**
```cpp
sim.append_job(0.0, 10, "1", 100.0);
sim.run_until_exclusive(0.0);  // Does NOT process START event at t=0
// Job 0 is still queued, not running

sim.advance_to(0.0);  // Now process START event
// Job 0 is running
```

### Monitoring Methods

For callback-driven EASY backfilling, construct the simulation with the
[`custom circular-buffer constructor`][cpp-simulation-simulation]:

```cpp
BasicSimulation(const Sim_Params& params, job_cost_function_t cost_function,
                backfill_selector_t selector);
```

Set `params.m_num_max_candidates` before construction. The cost function is
invoked as each job enters the wait queue. The selection function receives up
to that many feasible `(job_id, cost)` pairs and returns one of those IDs, or
`std::nullopt` to decline a backfill.

**[`get_current_time()`][cpp-simulation-get-current-time] — Current simulation time:**
```cpp
sim_time_t get_current_time() const;
```

**[`get_nodes_in_use()`][cpp-simulation-get-nodes-in-use] / [`get_available_nodes()`][cpp-simulation-get-available-nodes] — Allocated and available nodes:**
```cpp
num_nodes_t get_nodes_in_use() const;
num_nodes_t get_available_nodes() const;
double get_current_utilization() const;
tdiff_t get_resource_area() const; // Custom-FCFS simulations only
```

[`get_current_utilization()`][cpp-simulation-get-current-utilization] is the point-in-time ratio of allocated nodes to
effective scheduled capacity. During a non-preemptive reduction below live
occupancy, the running allocation is treated as effective capacity until it
drains, keeping utilization bounded by one. For a simulation created with the Custom-FCFS callback
constructor, [`get_resource_area()`][cpp-simulation-get-resource-area] is the area under the allocated-node curve:
the time integral of allocated nodes, updated after all events and scheduling
decisions at each timestamp have been applied, and reported in node-seconds.
Standard schedulers do not perform this live bookkeeping, and calling [`get_resource_area()`][cpp-simulation-get-resource-area] for one throws
`std::logic_error`.

For Custom FCFS, [`Statistics::utilization`][cpp-simulation-statistics-utilization] divides this area by the integrated
effective capacity over the accounting horizon. Standard schedulers use the
same capacity-aware denominator with their post-hoc completed-workload area.
Without a capacity schedule, this is equivalent to configured nodes times the
elapsed horizon.

**[`get_active_job_count()`][cpp-simulation-get-active-job-count] — Waiting-job count:**
```cpp
size_t get_active_job_count() const;
```

**[`get_fcfs_head_shadow_time()`][cpp-simulation-get-fcfs-head-shadow-time] — FCFS-head shadow time:**
```cpp
sim_time_t get_fcfs_head_shadow_time() const;
```

This returns the earliest time at which the current FCFS queue head is
expected to start, based on the scheduler's time-limit reservation model. It
returns `-1` when no job is waiting. It is meaningful for the FCFS/EASY
reservation model.

**[`get_backfill_window()`][cpp-simulation-get-backfill-window] — Resource changes and reservation snapshot:**
```cpp
Simulation::Backfill_Window get_backfill_window() const;
```

```cpp
struct Simulation::Backfill_Window {
    struct Resource_Release {
        sim_time_t time;            // Absolute release time
        num_nodes_t nodes_released; // Nodes becoming free at time
    };

    sim_time_t current_time;              // Snapshot time
    num_nodes_t available_nodes;          // Nodes free immediately
    sim_time_t shadow_time;               // FCFS-head start, or -1 if none
    std::vector<Resource_Release> releases; // Ordered projected releases
};
```

The snapshot contains `current_time`, immediately `available_nodes`, the
same FCFS-head `shadow_time` (`-1` if the queue is empty or the head has no
projected start), and chronologically
ordered resource-change events in `releases`. Each event gives the simulation
`time` at which capacity changes and the summed `nodes_released` then. Events
use time-limit estimates and include every currently running job, even when no
FCFS head is waiting; simultaneous releases are combined. This is an
in-process API; it does not require gRPC. The gRPC request returns the same
full projection, which is useful when estimating when a prospective job could
acquire enough nodes.

```cpp
auto window = sim.get_backfill_window();
for (const auto& change : window.releases) {
    std::cout << change.time << ": +" << change.nodes_released << " nodes\n";
}
```

**[`estimate_queue_horizon()`][cpp-simulation-estimate-queue-horizon] — Waiting-queue horizon:**

```cpp
tdiff_t horizon = sim.estimate_queue_horizon(utilization);
```

This method is available for the standard and Custom FCFS implementations with
EASY backfilling. Call it after the current backfilling cycle completes. At
that point, the waiting queue contains only jobs that could not start, and the
running set includes jobs dispatched by the cycle. The method holds those sets
fixed: future arrivals are excluded and no additional waiting jobs are admitted
during its forward replay. Waiting resource-time is scanned from existing queue
records only when this method is called; normal scheduling maintains no extra
prediction state.

Queued demand is `A_Q = sum(requested_nodes * estimated_runtime)`. Starting at
the FCFS head's shadow time, the method integrates available nodes over each
complete interval between predicted running-job completions and scales that
service area by `utilization`. This factor models capacity loss from
fragmentation and scheduling constraints and is not the instantaneous
utilization at the current scheduling time. Values in `(0, 1]` are used
directly; zero selects the fallback factor `U = 1`.

The method returns the first completion-event offset at which accumulated
usable area covers `A_Q`; it does not interpolate within an intermediate
interval. If the final currently running job completes before the threshold
is reached, the remaining area is converted to time using
`utilization * total_nodes`. An empty queue returns zero. The gRPC
`EstimateQueueHorizonRequest` exposes the same calculation.

**[`get_start_times_by_queue_drain()`][cpp-simulation-get-start-times-by-queue-drain] — Queue-drain start estimates:**

```cpp
auto estimates = sim.get_start_times_by_queue_drain(utilization);
```

This on-demand approximation sweeps cumulative queued node-time against the
utilization-scaled capacity profile after protected reservations are removed.
`estimation_depth` limits the returned queue prefix; zero covers all waiting
jobs. `RESERVED` entries are protected scheduler reservations. Other entries
are optimistic area estimates, not contiguous first-fit placements or start
guarantees.
It is a C++/Python API and is not currently exposed through the gRPC service.


**[`get_statistics()`][cpp-simulation-get-statistics] — Scheduling statistics: wait times, turnaround, and utilization:**

The returned statistics also include mean execution time and mean bounded
slowdown. Bounded slowdown is computed per completed job as
`max(1, turnaround / max(run_time, 10 seconds))`.
```cpp
Simulation::Statistics get_statistics() const;
```

**[`get_trace()`][cpp-simulation-get-trace] — Trace data:**
```cpp
Trace& get_trace();
const Trace& get_trace() const;
```

## Streaming example

```cpp
while (external_system.has_more_jobs()) {
    Job job = external_system.get_next_job();

    sim.append_job(job.submit_time, job.num_nodes,
                   job.queue, job.limit_time);
    sim.advance_to(job.submit_time);
    std::cout << "Nodes in use: " << sim.get_nodes_in_use() << std::endl;
}
```

For a batch-loaded trace, call [`initialize_trace()`][cpp-simulation-initialize-trace] followed by [`run()`][cpp-simulation-run].
Scheduling semantics are documented in
[Scheduling Policies](../BACKFILLING_ALGORITHMS.md), and internal event and
storage ownership in [Job Lifecycle](../dev/JOB_LIFECYCLE.md).

## Testing

The Test Suite README has exact commands for
[streaming and batch-equivalence tests](https://github.com/LLNL/dr_evt/blob/main/tests/README.md#streaming-api-tests)
and
[distributed gRPC and MPI tests](https://github.com/LLNL/dr_evt/blob/main/tests/README.md#distributed-clientserver-tests).
See the runnable
[`test_append_job_api.cpp`](https://github.com/LLNL/dr_evt/blob/main/tests/test_append_job_api.cpp)
and
[`test_batch_vs_streaming.cpp`](https://github.com/LLNL/dr_evt/blob/main/tests/test_batch_vs_streaming.cpp)
for complete C++ call sequences.

## Limitations

1. **No job cancellation**: Once submitted, jobs cannot be cancelled
2. **Time must advance forward**: Cannot go back in time
3. **Single scheduler instance**: In-process coordination of multiple
   schedulers is not supported. Distributed alternatives are described in
   [Client/Server Use Cases](../user-guide/client-server-use-cases.md).

## See Also

- [`src/sim/sim.hpp`](https://github.com/LLNL/dr_evt/blob/main/src/sim/sim.hpp) - API declarations
- [`src/sim/sim.cpp`](https://github.com/LLNL/dr_evt/blob/main/src/sim/sim.cpp) - implementation
- [`python/example_streaming.py`](https://github.com/LLNL/dr_evt/blob/main/python/example_streaming.py) - runnable Python example of the same in-process API
- [`tests/test_append_job_api.cpp`](https://github.com/LLNL/dr_evt/blob/main/tests/test_append_job_api.cpp) - C++ usage examples
- [gRPC Client/Server Guide and API](../CLIENT_SERVER_GUIDE.md) - Network-exposed streaming API, MPI multi-client/multi-server harness
- [Progressive/Multi-File Loading](../dev/OUTPUT_TRACE_BUFFERS.md) - `--infile_list`, a related but distinct capability: bounding job-store memory across a trace the caller already knows in full (split across files), rather than jobs arriving live

[cpp-job-append-request]: https://dr-evt.readthedocs.io/en/latest/api/cpp/source/trace.html#_CPPv4N6dr_evt18Job_Append_RequestE
[cpp-simulation]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4I0EN6dr_evt15BasicSimulationE
[cpp-simulation-advance-to]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation10advance_toE10sim_time_t
[cpp-simulation-append-job]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation10append_jobE10sim_time_t11num_nodes_tRKNSt6stringE7tdiff_tNSt8optionalI7tdiff_tEE
[cpp-simulation-append-jobs]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation11append_jobsERKNSt6vectorI18Job_Append_RequestEE
[cpp-simulation-estimate-queue-horizon]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation22estimate_queue_horizonEd
[cpp-simulation-flush-completed-jobs]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation20flush_completed_jobsEv
[cpp-simulation-get-active-job-count]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation20get_active_job_countEv
[cpp-simulation-get-available-nodes]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation19get_available_nodesEv
[cpp-simulation-get-backfill-window]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation19get_backfill_windowEv
[cpp-simulation-get-current-time]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation16get_current_timeEv
[cpp-simulation-get-current-utilization]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation23get_current_utilizationEv
[cpp-simulation-get-fcfs-head-shadow-time]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation25get_fcfs_head_shadow_timeEv
[cpp-simulation-get-job-statuses]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation16get_job_statusesERKNSt6vectorI8job_no_tEE6size_tbb
[cpp-simulation-get-nodes-in-use]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation16get_nodes_in_useEv
[cpp-simulation-get-resource-area]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation17get_resource_areaEv
[cpp-simulation-get-start-times-by-est-caches]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation29get_start_times_by_est_cachesEv
[cpp-simulation-get-start-times-by-queue-drain]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation30get_start_times_by_queue_drainEd6size_t
[cpp-simulation-get-statistics]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation14get_statisticsEv
[cpp-simulation-get-trace]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation9get_traceEv
[cpp-simulation-initialize-trace]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation16initialize_traceE10num_jobs_t
[cpp-simulation-load-checkpoint]: https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4N6dr_evt15BasicSimulation15load_checkpointERKNSt6stringE
[cpp-simulation-run]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation3runEv
[cpp-simulation-run-until-exclusive]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation19run_until_exclusiveE10sim_time_t
[cpp-simulation-save-checkpoint]: https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4N6dr_evt15BasicSimulation15save_checkpointERKNSt6stringE
[cpp-simulation-simulation]: https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4N6dr_evt15BasicSimulation15BasicSimulationE10Sim_Params19job_cost_function_t19backfill_selector_t
[cpp-simulation-statistics-utilization]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation10Statistics11utilizationE
[cpp-trace-load-data]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/trace-replay.html#_CPPv4N6dr_evt10BasicTrace9load_dataE10num_jobs_t

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
