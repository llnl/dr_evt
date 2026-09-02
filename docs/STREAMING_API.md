# Streaming API Documentation

## Overview

The DR_EVT simulator provides a streaming API that allows external code (e.g., gRPC servers - see [gRPC Client/Server Guide](CLIENT_SERVER_GUIDE.md), workflow managers) to feed jobs dynamically and control simulation time advancement. This enables online/incremental simulation where jobs arrive over time rather than all at once.

## Core Concepts

### Batch Mode vs Streaming Mode

**Batch Mode** (default):
- All jobs loaded from trace file upfront
- Simulation runs from start to end in one call to `run()`
- Simple but inflexible

**Streaming Mode** (via API):
- Jobs submitted incrementally via `submit_job()`
- Caller controls time advancement via `advance_to()`/`run_until_exclusive()`
- Enables interactive/online simulation scenarios

### Time Advancement

The streaming API provides two time advancement modes:

1. **Inclusive** (`advance_to(t)`): Advances to time `t` and processes all events AT time `t`
2. **Exclusive** (`run_until_exclusive(t)`): Advances to just before time `t`, excluding events at `t`

## API Methods

### `initialize_trace(max_jobs = 0)`

Loads trace data and prepares it for either batch or streaming use: sorts jobs by submit time and determines actual durations (simulation mode only).

```cpp
num_jobs_t initialize_trace(num_jobs_t max_jobs = 0);
```

**Parameters:**
- `max_jobs`: Maximum number of jobs to load (0 = no limit)

**Returns:** number of jobs actually loaded

**Must be called before `submit_job()`/`advance_to()`** - calling `get_trace().load_data()` directly instead skips the sort and duration-determination steps, silently producing wrong scheduling decisions and wrong statistics. This method is idempotent (safe to call more than once; it clears any previously-loaded data first).

**Example:**
```cpp
Simulation sim(params);
num_jobs_t num_jobs = sim.initialize_trace();
std::cout << "Loaded " << num_jobs << " jobs\n";
```

### `submit_job(job_idx, submit_time)`

Submits a job to the scheduler's waiting queue.

```cpp
void submit_job(job_no_t job_idx, sim_time_t submit_time);
```

**Parameters:**
- `job_idx`: Index of job in the loaded trace (0-based)
- `submit_time`: When the job is submitted (must be >= current_time)

**Behavior:**
- Adds job to waiting queue
- Does NOT advance time or make scheduling decisions
- Call `advance_to()`/`run_until_exclusive()` afterward to let scheduler process

**Example:**
```cpp
sim.submit_job(0, 0.0);    // Submit job 0 at t=0
sim.submit_job(1, 50.0);   // Submit job 1 at t=50
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
sim.submit_job(0, 0.0);
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
sim.submit_job(0, 0.0);
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

// Submit all jobs at their submit times
for (size_t i = 0; i < sim.get_trace().data().size(); i++) {
    const auto& job = sim.get_trace().data()[i];
    sim_time_t submit = job.get_submit_time().first;
    sim.submit_job(i, submit);
}

// Run entire simulation
sim.advance_to(MAX_TIME);
```

### Pattern 2: Incremental Job Submission

```cpp
// External system feeds jobs over time
while (external_system.has_more_jobs()) {
    Job job = external_system.get_next_job();

    // Submit job
    sim.submit_job(job.idx, job.submit_time);

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
    // Submit any jobs arriving in this window
    for (auto& job : jobs_arriving_at(t)) {
        sim.submit_job(job.idx, t);
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
        sim.submit_job(evt.job_idx, evt.time);
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
    params.m_duration_mode = DurationMode::EXACT;
    params.m_backfill_policy = BackfillPolicy::EASY;

    // Create simulator
    Simulation sim(params);

    // Load trace
    num_jobs_t num_jobs = sim.initialize_trace();
    std::cout << "Loaded " << num_jobs << " jobs\n";

    // Submit and run jobs incrementally
    for (size_t i = 0; i < sim.get_trace().data().size(); i++) {
        const auto& job = sim.get_trace().data()[i];
        sim_time_t submit = job.get_submit_time().first;

        // Submit job
        sim.submit_job(i, submit);

        // Advance to submit time
        sim.advance_to(submit);

        // Monitor
        std::cout << "t=" << sim.get_current_time()
                  << ": " << sim.get_nodes_in_use()
                  << " nodes in use\n";
    }

    // Run until all jobs complete
    sim.advance_to(10000.0);

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

### test_streaming_api

Basic functional tests of the streaming API methods.

```bash
./build/test_streaming_api
```

**Tests:**
- Basic `submit_job()` and `advance_to()`/`run_until_exclusive()` operations
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
own, separate [MPI multi-client/multi-server harness](GRPC_GUIDE.md), where
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

1. **Jobs must be in trace**: All jobs must exist in the loaded trace before calling `submit_job()`
2. **Time must advance forward**: Cannot go back in time
3. **No job cancellation**: Once submitted, jobs cannot be cancelled
4. **Single scheduler instance**: No support for multi-scheduler coordination in-process (the gRPC client/server's [MPI multi-client/multi-server harness](GRPC_GUIDE.md) coordinates across separate, independent `Simulation` instances instead, each in its own process)

## See Also

- `src/sim/sim.hpp` - API declarations
- `src/sim/sim.cpp` - Implementation
- `tests/test_streaming_api.cpp` - Usage examples
- [gRPC Client/Server Guide](GRPC_GUIDE.md) - Network-exposed streaming API, MPI multi-client/multi-server harness
