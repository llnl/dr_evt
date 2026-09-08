# C++ Streaming API Reference

## Overview

The DR_EVT simulator provides a streaming API that allows external code (e.g., gRPC servers - see [gRPC Client/Server Guide](../CLIENT_SERVER_GUIDE.md), workflow managers) to feed jobs dynamically and control simulation time advancement. This enables online/incremental simulation where jobs arrive over time rather than all at once.

## Core Concepts

### Batch Mode vs Streaming Mode

**Batch Mode** (default):
- All jobs loaded from trace file upfront
- Simulation runs from start to end in one call to `run()`
- Simple but inflexible

**Streaming Mode** (via API):
- Genuinely new jobs (the trace has never seen before) are added and enqueued incrementally via `append_job()`/`append_jobs()`
- Caller controls time advancement via `advance_to()`/`run_until_exclusive()`
- Enables interactive/online simulation scenarios

### Time Advancement

The streaming API provides two time advancement modes:

1. **Inclusive** (`advance_to(t)`): Advances to time `t` and processes all events AT time `t`
2. **Exclusive** (`run_until_exclusive(t)`): Advances to just before time `t`, excluding events at `t`

## C++ API Reference

These are direct methods on `dr_evt::Simulation`; they do not require a
server or gRPC. The gRPC service maps its request messages onto these methods
where applicable; see the [Client/Server Guide](../CLIENT_SERVER_GUIDE.md) for
the wire protocol.

:::{only} doxygen
See the [complete generated C++ API reference](CPP_API.md).
:::

:::{only} not doxygen
The complete generated C++ API reference is unavailable because Doxygen was not
run for this build.
:::

For live jobs, `append_job()` (or `append_jobs()`) creates a previously unseen
job and enqueues it atomically for scheduling.

### API Methods

### `initialize_trace(max_jobs = 0)`

Loads trace data and prepares it for either batch or streaming use: sorts jobs by submit time and determines actual durations (simulation mode only).

```cpp
num_jobs_t initialize_trace(num_jobs_t max_jobs = 0);
```

**Parameters:**
- `max_jobs`: Maximum number of jobs to load (0 = no limit)

**Returns:** number of jobs actually loaded

**Must be called before `advance_to()`** - calling `get_trace().load_data()` directly instead skips the sort and duration-determination steps, silently producing wrong scheduling decisions and wrong statistics. This method is idempotent (safe to call more than once; it clears any previously-loaded data first).

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

## Usage Patterns

### Pattern 1: Submit All, Then Run

```cpp
Simulation sim(params);
sim.initialize_trace();

sim.run();
```

### Pattern 2: Incremental Job Submission (genuinely new jobs)

```cpp
// External system feeds genuinely new jobs over time - the trace
// never knew about them in advance, so append_job() (not just
// submit_job() on a preloaded index) is what makes this real streaming.
while (external_system.has_more_jobs()) {
    Job job = external_system.get_next_job();

    // Append and enqueue the job (the trace has never seen it before)
    job_no_t job_no = sim.append_job(job.submit_time, job.num_nodes,
                                      job.queue, job.limit_time);
    // Advance to job's submit time
    sim.advance_to(job.submit_time);

    // Check resource state
    std::cout << "Nodes in use: " << sim.get_nodes_in_use() << std::endl;
}
```

### Pattern 3: Time-Stepped Simulation

```cpp
// Advance in fixed time steps
for (sim_time_t t = 0; t <= 1000.0; t += 10.0) {
    // Append and submit any genuinely new jobs arriving in this window
    for (auto& job : jobs_arriving_at(t)) {
        job_no_t job_no = sim.append_job(t, job.num_nodes, job.queue, job.limit_time);
    }

    // Advance to next time step
    sim.advance_to(t);

    // Record metrics
    metrics.record(t, sim.get_nodes_in_use());
}
```

### Pattern 4: Event-Driven Simulation

```cpp
// Advance only when events occur
std::queue<Event> event_queue = build_event_queue();

while (!event_queue.empty()) {
    Event evt = event_queue.front();
    event_queue.pop();

    if (evt.type == Event::JOB_ARRIVAL) {
        job_no_t job_no = sim.append_job(evt.time, evt.num_nodes,
                                          evt.queue, evt.limit_time);
        sim.advance_to(evt.time);
    } else if (evt.type == Event::CHECKPOINT) {
        sim.advance_to(evt.time);
        save_checkpoint(sim);
    }
}
```

## Complete Example

```cpp
#include "sim/sim.hpp"
#include <iostream>

int main() {
    // Configure simulation
    Sim_Params params;
    params.m_infile = "jobs.csv";
    params.m_total_nodes = 100;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_run_time_mode = RunTimeMode::LIMIT;
    params.m_backfill_policy = BackfillPolicy::EASY;

    // Create simulator
    Simulation sim(params);

    // Load trace
    num_jobs_t num_jobs = sim.initialize_trace();
    std::cout << "Loaded " << num_jobs << " jobs\n";

    // Batch-loaded traces are run as a unit.
    sim.run();
    std::cout << "t=" << sim.get_current_time()
                  << ": " << sim.get_nodes_in_use()
                  << " nodes in use\n";

    std::cout << "Simulation complete!\n";
    return 0;
}
```

## Implementation Details

### Scheduling Decisions

The scheduler is invoked automatically at:
- Job arrivals (when `advance_to()`/`run_until_exclusive()` reaches a submit time)
- Job completions (when END events are processed)

The EASY backfilling policy ensures:
- Jobs start as soon as resources are available
- Small jobs can backfill if they don't delay the queue head
- Reservation is made for the first queued job

### Event Processing

Internally, the simulator maintains an event queue with:
- **START events**: Job begins execution, allocates nodes
- **END events**: Job completes, releases nodes

The `advance_to()` method:
1. Processes events in chronological order
2. Calls scheduler after END events (resources freed)
3. Creates START/END events for newly scheduled jobs
4. Advances `current_time` to `target_time`

### Critical Fix

**Bug (fixed)**: Early versions of `advance_to()` would extend `target_time` when jobs would complete after it:

```cpp
// BUGGY CODE (removed):
if (job_end > target_time) {
    target_time = job_end;  // DON'T MODIFY target_time!
}
```

This caused `advance_to(50)` to continue advancing to `t=150` and beyond. The fix: **never modify the target_time parameter** - the caller controls advancement.

## Testing

Test programs verify the streaming API:

### test_append_job_api

Functional tests of the streaming API: `append_job()`/`append_jobs()`
(genuine insertion of jobs the trace never saw before) together with
`append_job()`/`advance_to()`/`run_until_exclusive()`'s general
correctness - consolidated into one file since the latter's coverage
never actually depended on a preloaded trace.

```bash
./build/test_append_job_api
```

**Tests:**
- `append_job()`/`append_jobs()`: basic insertion, batch validation (sorting, atomicity), reclaim-before-grow, `--job_store_overflow=abort`
- Basic atomic `append_job()` and `advance_to()`/`run_until_exclusive()` operations
- Exclusive vs inclusive time advancement semantics
- Online scheduling simulation
- Resource leak detection

### test_batch_vs_streaming

Comprehensive validation comparing batch mode vs streaming mode with large workloads.

```bash
./build/test_batch_vs_streaming tests/test_traces/scale/huge_2000jobs.csv
```

**Validates:**
- Job traces match (scheduling decisions)
- Resource traces match (resource accounting over time)
- Tested with 2000+ job traces

### test_mpi_streaming (requires MPI)

Tests MPI-coordinated streaming with multiple ranks feeding jobs independently
(in-process: each rank runs its own `Simulation` object within one MPI
program, not separate processes - contrast with the gRPC client/server's
own, separate [MPI multi-client/multi-server harness](../CLIENT_SERVER_GUIDE.md), where
each rank is a distinct process talking over the network).

```bash
mpirun -np 4 ./build/test_mpi_streaming tests/test_traces/scale/large_200jobs.csv
```

**Confirmed currently failing** (verified directly: built and ran it against
`tests/test_traces/scale/large_200jobs.csv`, 4 ranks): different ranks
produce different output, and MPI streaming output differs from batch
mode - the opposite of what the test intends to confirm. This is a
pre-existing issue in this test and/or the code path it exercises, not
something introduced by making the target reachable - it had simply never
actually been exercised before (see below), so this had gone
undetected.

**Why it was never caught**: this target's build condition
(`if(MPI_CXX_FOUND)` in `CMakeLists.txt`) depends on `find_package(MPI)`
having been called somewhere - which, before the gRPC client/server's own
MPI harness added one, never happened anywhere in this project. That
means this target was never actually buildable at all until building with
`-DDR_EVT_ENABLE_GRPC=ON` (which is what pulls in `find_package(MPI)`
today) incidentally made it reachable as a side effect - not something
either the gRPC work or this test was designed to depend on.

This needs its own, separate investigation before being relied on for
anything - treat its output as unverified until that happens.

## Limitations

1. **No job cancellation**: Once submitted, jobs cannot be cancelled
2. **Time must advance forward**: Cannot go back in time
3. **Single scheduler instance**: No support for multi-scheduler coordination in-process (the gRPC client/server's [MPI multi-client/multi-server harness](../CLIENT_SERVER_GUIDE.md) coordinates across separate, independent `Simulation` instances instead, each in its own process)

## See Also

- `src/sim/sim.hpp` - API declarations
- `src/sim/sim.cpp` - Implementation
- `tests/test_append_job_api.cpp` - Usage examples
- [gRPC Client/Server Guide](../CLIENT_SERVER_GUIDE.md) - Network-exposed streaming API, MPI multi-client/multi-server harness
- [Progressive/Multi-File Loading](../dev/design-decisions/OUT_TRACE_STREAMING.md) - `--infile_list`, a related but distinct capability: bounding job-store memory across a trace the caller already knows in full (split across files), rather than jobs arriving live
