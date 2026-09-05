# DR_EVT Python API

Python bindings for DR_EVT HPC Job Scheduler Simulator.

## Features

- **Streaming API**: Insert jobs dynamically and control simulation time
- **Monitoring API**: Query resource usage, queue status, and scheduling decisions
- **Statistics API**: Get comprehensive performance metrics
- **Full EASY Backfilling**: Same verified algorithm as C++ implementation

## Installation

### Prerequisites

- Python 3.6+
- C++ compiler with C++17 support
- CMake 3.12+
- pybind11 (automatically fetched during build)
- Boost libraries

### Build and Install

```bash
cd python
pip install .
```

Or for development:

```bash
pip install -e .
```

## Quick Start

```python
import dr_evt

# Configure simulation
params = dr_evt.SimParams()
params.infile = "trace.csv"
params.total_nodes = 100
params.backfill_policy = dr_evt.BackfillPolicy.EASY

# Create simulator
sim = dr_evt.Simulation(params)
sim.initialize_trace()

# Submit job at t=0
sim.insert_job(0, 0.0)
sim.run_until_inclusive(0.0)

# Check resources
print(f"Nodes in use: {sim.get_nodes_in_use()}")
print(f"Available: {sim.get_available_nodes()}")
print(f"Queue size: {sim.get_wait_queue_size()}")

# Get statistics
stats = sim.get_statistics()
print(f"Utilization: {stats.utilization * 100:.1f}%")
```

## API Reference

### SimParams

Configuration parameters for simulation.

**Attributes:**
- `infile` (str): Path to input trace file
- `total_nodes` (int): Total number of nodes in system
- `trace_format` (str): Trace format ("simple" or "lassen")
- `timestamp_format` (str): Timestamp format ("epoch" or "iso")
- `run_time_mode` (RunTimeMode): How the job's actual run time is determined
- `backfill_policy` (BackfillPolicy): Backfilling policy (NONE, EASY, CONSERVATIVE)
- `priority_policy` (PriorityPolicy): Job priority policy (FCFS, SJF, LJF)
- `verbose` (bool): Enable verbose output

**Enums:**

**Run Time Modes:**
- `RunTimeMode.ACTUAL`: Read actual run time from trace (default, most realistic)
  - Accepted trace columns: `actual_run_time`, `duration`, `actual_duration`, `run_time`
- `RunTimeMode.DISTRIBUTION`: Sample from statistical distribution
- `RunTimeMode.LIMIT`: Use time_limit in place of run_time (debugging only)

**Note:** Scheduler uses time_limit as the best estimator for planning

- `BackfillPolicy.NONE`: No backfilling
- `BackfillPolicy.EASY`: EASY backfilling (verified)
- `BackfillPolicy.CONSERVATIVE`: Conservative backfilling

- `PriorityPolicy.FCFS`: First-Come-First-Served
- `PriorityPolicy.SJF`: Shortest Job First
- `PriorityPolicy.LJF`: Longest Job First

### Simulation

Main simulation class.

**Constructor:**
```python
sim = Simulation(params: SimParams)
```

**Initialization:**
```python
sim.initialize_trace(max_jobs=0) -> int
```
Load trace from file. Must be called before streaming. Returns number of jobs loaded.

**Streaming API:**

```python
sim.insert_job(job_idx: int, submit_time: float)
```
Submit a job to scheduler at specified time.

```python
sim.run_until_inclusive(target_time: float)
```
Advance simulation to `target_time`, processing all events AT `target_time`.

```python
sim.run_until_exclusive(target_time: float)
```
Advance to just before `target_time`, excluding events at `target_time`.

**Monitoring API:**

```python
sim.get_current_time() -> float
```
Get current simulation time.

```python
sim.get_nodes_in_use() -> int
```
Get number of nodes currently allocated.

```python
sim.get_available_nodes() -> int
```
Get number of nodes currently available.

```python
sim.get_wait_queue_size() -> int
```
Get number of jobs waiting to be scheduled.

```python
sim.get_fcfs_head_shadow_time() -> float
```
Get estimated start time for FCFS head of queue. Returns -1 if queue is empty.

**Statistics API:**

```python
sim.get_statistics() -> Statistics
```
Get comprehensive scheduling statistics.

**Statistics Structure:**

- `jobs_submitted` (int): Total jobs submitted
- `jobs_completed` (int): Total jobs completed
- `jobs_running` (int): Currently running jobs
- `jobs_waiting` (int): Jobs in wait queue
- `current_time` (float): Current simulation time
- `total_nodes` (int): Total nodes in system
- `nodes_in_use` (int): Nodes currently allocated
- `nodes_available` (int): Nodes currently free
- `utilization` (float): Resource utilization (0.0-1.0)
- `avg_wait_time` (float): Average wait time (seconds)
- `avg_turnaround_time` (float): Average turnaround time (seconds)
- `makespan` (float): Time to complete all jobs (seconds)

**Output:**

```python
sim.write_simulated_trace()
```
Write job results to output file (specified in params).

```python
sim.print_stats()
```
Print statistics to stdout.

## Usage Patterns

### Pattern 1: Batch Simulation

```python
sim = dr_evt.Simulation(params)
sim.initialize_trace()
sim.run()  # Run all jobs at once
stats = sim.get_statistics()
```

### Pattern 2: Online Monitoring

```python
sim = dr_evt.Simulation(params)
sim.initialize_trace()

for job_idx in range(num_jobs):
    submit_time = get_submit_time(job_idx)
    sim.insert_job(job_idx, submit_time)
    sim.run_until_inclusive(submit_time)
    
    # Monitor after each submission
    print(f"Time {sim.get_current_time()}: {sim.get_nodes_in_use()} nodes in use")
    print(f"  Queue: {sim.get_wait_queue_size()} waiting")
```

### Pattern 3: Decision Making with Shadow Time

```python
# Check when FCFS head can start
shadow_time = sim.get_fcfs_head_shadow_time()
if shadow_time >= 0:
    # Job at head of queue will start at shadow_time
    # Use this for admission control or user feedback
    estimated_wait = shadow_time - sim.get_current_time()
    print(f"Next job will start in {estimated_wait:.1f} seconds")
```

### Pattern 4: Time-Stepped Simulation

```python
time_step = 60.0  # 1 minute steps
for t in range(0, int(end_time), int(time_step)):
    # Submit any jobs arriving in this window
    for job in jobs_arriving_at(t):
        sim.insert_job(job, float(t))
    
    # Advance simulation
    sim.run_until_inclusive(float(t))
    
    # Record metrics
    stats = sim.get_statistics()
    log_metrics(t, stats.utilization, stats.jobs_waiting)
```

## Examples

See `example_streaming.py` for a complete working example.

## Testing

Run the example:

```bash
python example_streaming.py
```

## Building from Source

The Python module is built using CMake and pybind11:

1. CMakeLists.txt detects pybind11
2. Compiles `dr_evt_bindings.cpp`
3. Links against DR_EVT C++ library
4. Produces `dr_evt.so` (or `.pyd` on Windows)

To build manually:

```bash
mkdir build
cd build
cmake .. -DDR_EVT_BUILD_PYTHON=ON
make
```

## Troubleshooting

**Import error: `ModuleNotFoundError: No module named 'dr_evt'`**

Make sure the module is installed:
```bash
pip install .
```

**Trace loading fails**

Call `initialize_trace()` before any streaming operations:
```python
sim = dr_evt.Simulation(params)
sim.initialize_trace()  # <- Required!
sim.insert_job(0, 0.0)
```

**Shadow time returns -1**

The wait queue is empty. Submit jobs first:
```python
sim.insert_job(0, 0.0)  # Submit a job
shadow_time = sim.get_fcfs_head_shadow_time()  # Now returns valid time
```

## Performance

The Python API has minimal overhead - it's a thin wrapper around the verified C++ implementation. Performance is essentially identical to running the C++ simulator directly.

## See Also

- [C++ Streaming API Documentation](../docs/api/STREAMING_API.md)
- [Main DR_EVT Documentation](../docs/)
- [Test Suite](../tests/)

---

**Version:** 1.0.0  
**License:** MIT  
**Status:** Production Ready
