# Streaming API Documentation

## Overview

The DR_EVT simulator provides a streaming API that allows external code (e.g., gRPC servers, workflow managers) to feed jobs dynamically and control simulation time advancement. This enables online/incremental simulation where jobs arrive over time rather than all at once.

## Core Concepts

### Batch Mode vs Streaming Mode

**Batch Mode** (default):
- All jobs loaded from trace file upfront
- Simulation runs from start to end in one call to `run()`
- Simple but inflexible

**Streaming Mode** (via API):
- Jobs submitted incrementally via `insert_job()`
- Caller controls time advancement via `run_until_*()` methods
- Enables interactive/online simulation scenarios

### Time Advancement

The streaming API provides two time advancement modes:

1. **Inclusive** (`run_until_inclusive(t)`): Advances to time `t` and processes all events AT time `t`
2. **Exclusive** (`run_until_exclusive(t)`): Advances to just before time `t`, excluding events at `t`

## API Methods

### `insert_job(job_idx, submit_time)`

Submits a job to the scheduler's waiting queue.

```cpp
void insert_job(job_no_t job_idx, sim_time_t submit_time);
```

**Parameters:**
- `job_idx`: Index of job in the loaded trace (0-based)
- `submit_time`: When the job is submitted (must be ≥ current_time)

**Behavior:**
- Adds job to waiting queue
- Does NOT advance time or make scheduling decisions
- Call `run_until_*()` afterward to let scheduler process

**Example:**
```cpp
sim.insert_job(0, 0.0);    // Submit job 0 at t=0
sim.insert_job(1, 50.0);   // Submit job 1 at t=50
```

### `run_until_inclusive(target_time)`

Advances simulation to `target_time` and processes all events at that time.

```cpp
void run_until_inclusive(sim_time_t target_time);
```

**Parameters:**
- `target_time`: Time to advance to (must be ≥ current_time)

**Behavior:**
- Advances through all events up to AND INCLUDING `target_time`
- Scheduler makes decisions at each event
- Jobs may start/end during advancement
- `current_time` becomes `target_time` after call

**Example:**
```cpp
sim.insert_job(0, 0.0);
sim.run_until_inclusive(0.0);  // Process job 0's START event
// Job 0 is now running

sim.run_until_inclusive(100.0);  // Process job 0's END event at t=100
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
sim.insert_job(0, 0.0);
sim.run_until_exclusive(0.0);  // Does NOT process START event at t=0
// Job 0 is still queued, not running

sim.run_until_inclusive(0.0);  // Now process START event
// Job 0 is running
```

### Monitoring Methods

**Get current simulation time:**
```cpp
sim_time_t get_current_time() const;
```

**Get nodes currently in use:**
```cpp
num_nodes_t get_nodes_in_use() const;
```

**Access trace data:**
```cpp
Trace& get_trace();
const Trace& get_trace() const;
```

## Usage Patterns

### Pattern 1: Submit All, Then Run

```cpp
// Load trace
Simulation sim(params);
sim.get_trace().load_data(0);

// Submit all jobs at their submit times
for (size_t i = 0; i < sim.get_trace().data().size(); i++) {
    const auto& job = sim.get_trace().data()[i];
    sim_time_t submit = job.get_submit_time().first;
    sim.insert_job(i, submit);
}

// Run entire simulation
sim.run_until_inclusive(MAX_TIME);
```

### Pattern 2: Incremental Job Submission

```cpp
// External system feeds jobs over time
while (external_system.has_more_jobs()) {
    Job job = external_system.get_next_job();
    
    // Submit job
    sim.insert_job(job.idx, job.submit_time);
    
    // Advance to job's submit time
    sim.run_until_inclusive(job.submit_time);
    
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
        sim.insert_job(job.idx, t);
    }
    
    // Advance to next time step
    sim.run_until_inclusive(t);
    
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
        sim.insert_job(evt.job_idx, evt.time);
        sim.run_until_inclusive(evt.time);
    } else if (evt.type == Event::CHECKPOINT) {
        sim.run_until_inclusive(evt.time);
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
    sim.get_trace().load_data(0);
    std::cout << "Loaded " << sim.get_trace().data().size() << " jobs\n";
    
    // Submit and run jobs incrementally
    for (size_t i = 0; i < sim.get_trace().data().size(); i++) {
        const auto& job = sim.get_trace().data()[i];
        sim_time_t submit = job.get_submit_time().first;
        
        // Submit job
        sim.insert_job(i, submit);
        
        // Advance to submit time
        sim.run_until_inclusive(submit);
        
        // Monitor
        std::cout << "t=" << sim.get_current_time() 
                  << ": " << sim.get_nodes_in_use() 
                  << " nodes in use\n";
    }
    
    // Run until all jobs complete
    sim.run_until_inclusive(10000.0);
    
    std::cout << "Simulation complete!\n";
    return 0;
}
```

## Implementation Details

### Scheduling Decisions

The scheduler is invoked automatically at:
- Job arrivals (when `run_until_*` reaches a submit time)
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

This caused `run_until_inclusive(50)` to continue advancing to `t=150` and beyond. The fix: **never modify the target_time parameter** - the caller controls advancement.

## Testing

Three test programs verify the streaming API:

### test_streaming_api
Basic functional tests of the three API methods.

**Run:**
```bash
./build/test_streaming_api
```

### test_streaming_vs_batch
Verifies streaming mode produces identical results to batch mode.

**Run:**
```bash
./build/test_streaming_vs_batch
```

### test_two_stream_manual (requires MPI)
Tests two MPI ranks feeding jobs independently.

**Run:**
```bash
mpirun -np 2 ./build/test_two_stream_manual
```

## Limitations

1. **Jobs must be in trace**: All jobs must exist in the loaded trace before calling `insert_job()`
2. **Time must advance forward**: Cannot go back in time
3. **No job cancellation**: Once submitted, jobs cannot be cancelled
4. **Single scheduler instance**: No support for multi-scheduler coordination (use MPI for that)

## See Also

- `src/sim/sim.hpp` - API declarations
- `src/sim/sim.cpp` - Implementation
- `tests/test_streaming_api.cpp` - Usage examples
- `docs/SIMULATION_ALGORITHM.md` - Core simulation algorithm

---

**Status**: ✅ Complete and tested (as of 2026-08-28)
