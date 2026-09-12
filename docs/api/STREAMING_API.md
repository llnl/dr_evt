# C++ Streaming API Reference

## Overview

The DR_EVT simulator provides a streaming API that allows external code (e.g., gRPC servers - see [gRPC Client/Server Guide and API](../CLIENT_SERVER_GUIDE.md), workflow managers) to feed jobs dynamically and control simulation time advancement. This enables online/incremental simulation where jobs arrive over time rather than all at once.

Batch mode loads a trace and completes it with `run()`. Streaming mode adds
new jobs with `append_job()` or `append_jobs()` and lets the caller advance
simulation time. `advance_to(t)` includes events at `t`;
`run_until_exclusive(t)` excludes them.

## C++ Development API Reference

These are direct methods on `dr_evt::Simulation`; they do not require a
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

For live jobs, `append_job()` (or `append_jobs()`) creates a new
job and enqueues it atomically for scheduling.

### `initialize_trace(max_jobs = 0)`

Loads trace data and prepares it for either batch or streaming use: sorts jobs by submit time and determines actual durations (simulation mode only).

```cpp
num_jobs_t initialize_trace(num_jobs_t max_jobs = 0);
```

**Parameters:**
- `max_jobs`: Maximum number of jobs to load (0 = no limit)

**Returns:** number of jobs actually loaded

Call this before `advance_to()`. Calling `get_trace().load_data()` directly
skips sorting and duration determination. The method is idempotent and clears
loaded data before reinitializing it.

**Example:**
```cpp
Simulation sim(params);
num_jobs_t num_jobs = sim.initialize_trace();
std::cout << "Loaded " << num_jobs << " jobs\n";
```

### `append_job(submit_time, num_nodes, queue, limit_time)`

Adds a genuinely new job - one the trace has never seen before - to the
job store and immediately enqueues it for scheduling. This is how a job the
caller learns about live (for example, from a network event) enters a
streaming simulation.

```cpp
job_no_t append_job(sim_time_t submit_time, num_nodes_t num_nodes,
                     const std::string& queue, tdiff_t limit_time);
```

**Parameters:**
- `submit_time`: When the job is submitted (must be >= current_time)
- `num_nodes`: Number of nodes the job requests
- `queue`: Which queue the job belongs to (e.g. `"pbatch"`)
- `limit_time`: User-estimated time limit, in seconds

**Returns:** the new job's `job_no`

**Example:**
```cpp
job_no_t j = sim.append_job(10.0, 20, "pbatch", 200.0);
sim.advance_to(10.0);
```

### `append_jobs(requests)`

The batch counterpart to `append_job()` - several new jobs in one call,
each as a `Job_Append_Request` (the same four fields `append_job()`
takes, grouped). Resolves job-store capacity once for the whole batch
rather than once per job, so it's the more efficient choice when several
jobs are already known together (e.g. several arrivals collected in one
polling interval), not just a loop over `append_job()`. All-or-nothing:
requests must already be sorted by `submit_time` (non-decreasing), and
either the whole batch is appended or, on any failure (unsorted input,
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
    {10.0, 20, "pbatch", 200.0},
    {15.0, 10, "pbatch", 100.0},
};
auto job_nos = sim.append_jobs(batch);
```

### `advance_to(target_time)`

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
- `current_time` becomes `target_time` after call

**Example:**
```cpp
sim.append_job(0.0, 10, "pbatch", 100.0);
sim.advance_to(0.0);  // Process job 0's START event
// Job 0 is now running

sim.advance_to(100.0);  // Process job 0's END event at t=100
// Job 0 has completed
```

### `flush_completed_jobs()`

Writes and reclaims the completed contiguous front prefix at the simulation's
current time. This provides an explicit output/checkpoint boundary for a long
streaming session without closing the schedule file. Jobs that are unfinished,
still have pending events, or sit behind such a job remain resident.

```cpp
sim.advance_to(checkpoint_time);
sim.flush_completed_jobs();
```

### `run_until_exclusive(target_time)`

Advances simulation to just before `target_time`, excluding events at that exact time.

```cpp
void run_until_exclusive(sim_time_t target_time);
```

**Parameters:**
- `target_time`: Time to advance toward (must be > current_time)

**Behavior:**
- Advances through events BEFORE `target_time`
- Events exactly at `target_time` are NOT processed
- Useful for stopping just before a known event
- `current_time` becomes the last event time < `target_time`

**Example:**
```cpp
sim.append_job(0.0, 10, "pbatch", 100.0);
sim.run_until_exclusive(0.0);  // Does NOT process START event at t=0
// Job 0 is still queued, not running

sim.advance_to(0.0);  // Now process START event
// Job 0 is running
```

### Monitoring Methods

**Get current simulation time:**
```cpp
sim_time_t get_current_time() const;
```

**Get nodes currently in use / available:**
```cpp
num_nodes_t get_nodes_in_use() const;
num_nodes_t get_available_nodes() const;
```

**Get count of jobs waiting to be scheduled:**
```cpp
size_t get_active_job_count() const;
```

**Get the FCFS-head shadow time:**
```cpp
sim_time_t get_fcfs_head_shadow_time() const;
```

This returns the earliest time at which the current FCFS queue head is
expected to start, based on the scheduler's time-limit reservation model. It
returns `-1` when no job is waiting. It is meaningful for the FCFS/EASY
reservation model.

**Get resource-change times and the matching reservation snapshot:**
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
    sim_time_t shadow_time;               // FCFS-head start, or -1 if no head waits
    std::vector<Resource_Release> releases; // Ordered projected releases
};
```

The snapshot contains `current_time`, immediately `available_nodes`, the
same FCFS-head `shadow_time` (`-1` if the queue is empty), and chronologically
ordered resource-change events in `releases`. Each event gives the simulation
`time` at which capacity changes and the summed `nodes_released` then. Events
use time-limit estimates and extend through the reservation; simultaneous
releases are combined. This is an in-process API; it does not require gRPC.

```cpp
auto window = sim.get_backfill_window();
for (const auto& change : window.releases) {
    std::cout << change.time << ": +" << change.nodes_released << " nodes\n";
}
```

**Get scheduling statistics** (wait times, turnaround, utilization):
```cpp
Simulation::Statistics get_statistics() const;
```

**Access trace data:**
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

For a batch-loaded trace, call `initialize_trace()` followed by `run()`.
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
