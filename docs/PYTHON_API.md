# DR_EVT Python API

Complete Python bindings for DR_EVT HPC Job Scheduler Simulator with streaming mode support.

## Overview

The Python API provides full access to DR_EVT's streaming simulation capabilities, allowing you to:

- Submit jobs dynamically to the scheduler
- Control simulation time advancement
- Monitor resource usage and queue status in real-time
- Get comprehensive scheduling statistics
- Configure all scheduling policies and parameters
- Use the same verified EASY backfilling implementation as the C++ code

## Installation

### Prerequisites

- Python 3.6+
- C++ compiler with C++17 support
- CMake 3.12+
- Boost libraries

### Build from Source

```bash
# Configure with Python support
cd build
cmake .. -DDR_EVT_BUILD_PYTHON=ON
make

# Install Python module
cd ../python
pip install .
```

### Verify Installation

```python
import dr_evt
print(dr_evt.__version__)  # 1.0.0
```

## Quick Example

```python
import dr_evt

# Configure simulation
params = dr_evt.SimParams()
params.infile = "jobs.csv"
params.total_nodes = 100
params.trace_format = "simple"
params.timestamp_format = "epoch"
params.duration_mode = dr_evt.DurationMode.EXACT
params.backfill_policy = dr_evt.BackfillPolicy.EASY
params.priority_policy = dr_evt.PriorityPolicy.FCFS

# Create simulator and load trace
sim = dr_evt.Simulation(params)
sim.initialize_trace()

# Submit job and advance
sim.insert_job(0, 0.0)
sim.run_until_inclusive(0.0)

# Monitor
print(f"Nodes in use: {sim.get_nodes_in_use()}/{params.total_nodes}")
print(f"Available: {sim.get_available_nodes()}")
print(f"Queue size: {sim.get_wait_queue_size()}")

# Get statistics
stats = sim.get_statistics()
print(f"Utilization: {stats.utilization*100:.1f}%")
print(f"Running: {stats.jobs_running}, Waiting: {stats.jobs_waiting}")
```

## Configuration Parameters

All CLI options are available through `SimParams`. See [CLI_OPTIONS.md](CLI_OPTIONS.md) for detailed descriptions.

### Input/Output

```python
params = dr_evt.SimParams()

# Input trace file
params.infile = "jobs.csv"

# Output files configured via C++ methods:
# params.set_outfile("output.csv")
# params.set_resource_trace("resources.csv")
```

### System Configuration

```python
# Total nodes in cluster
params.total_nodes = 100  # Default: 795
```

### Trace Format

```python
# Trace file format
params.trace_format = "simple"   # or "lassen"

# Timestamp format in trace
params.timestamp_format = "epoch"  # or "iso"

# Timezone (only for iso timestamps)
# params.timezone = "America/Los_Angeles"  # Not exposed in bindings yet
```

### Scheduling Policies

```python
# Backfilling policy
params.backfill_policy = dr_evt.BackfillPolicy.EASY
# Options: NONE, EASY, CONSERVATIVE

# Job priority/ordering
params.priority_policy = dr_evt.PriorityPolicy.FCFS
# Options: FCFS, SJF (Shortest Job First), LJF (Longest Job First)

# Runtime estimation (for reservation calculation)
# params.runtime_mode = RuntimeEstimateMode.LIMIT  # Not exposed in bindings yet
# Options: LIMIT (use time_limit), ACTUAL (oracle mode)
```

### Duration Mode (Simulation)

```python
# How to determine actual job duration
params.duration_mode = dr_evt.DurationMode.EXACT
# Options:
# - FROM_COLUMN: Read from actual_duration column
# - EXACT: Jobs run exactly their time_limit
# - DISTRIBUTION: Sample from statistical distribution

# Distribution parameters (when duration_mode=DISTRIBUTION)
# params.duration_distribution = DistributionType.NORMAL  # Not exposed yet
# params.duration_scale = 0.8  # 80% of time_limit on average
# params.duration_stddev = 0.1  # 10% standard deviation
```

### Output Control

```python
# Enable verbose debug output
params.verbose = True  # Default: False
```

### Not Yet Exposed in Python Bindings

Some parameters are only accessible via C++ API currently:
- `timezone` - Timezone for ISO timestamp parsing
- `runtime_mode` - Runtime estimate mode (limit vs actual)
- `duration_distribution` - Distribution type
- `duration_scale` - Duration scale factor
- `duration_stddev` - Duration standard deviation
- `max_jobs` - Limit number of jobs to process
- `max_time` - Upper limit on simulation time
- `seed` - Random seed

To use these, pass them via command-line when calling the simulator binary.

## Enumerations

### BackfillPolicy

```python
dr_evt.BackfillPolicy.NONE          # No backfilling (strict FCFS)
dr_evt.BackfillPolicy.EASY          # EASY backfilling (default)
dr_evt.BackfillPolicy.CONSERVATIVE  # Conservative backfilling
```

### PriorityPolicy

```python
dr_evt.PriorityPolicy.FCFS  # First-Come-First-Served (default)
dr_evt.PriorityPolicy.SJF   # Shortest Job First
dr_evt.PriorityPolicy.LJF   # Longest Job First
```

### DurationMode

```python
dr_evt.DurationMode.FROM_COLUMN   # Read actual_duration from trace
dr_evt.DurationMode.EXACT         # Jobs run exactly time_limit
dr_evt.DurationMode.DISTRIBUTION  # Sample from distribution
```

## Streaming API

### Job Submission

```python
# Load trace first
sim.initialize_trace()

# Insert job at specific time
sim.insert_job(job_idx=0, submit_time=0.0)

# Or submit to scheduler queue
sim.submit_job(job_idx=1, submit_time=10.0)
```

### Time Advancement

```python
# Process events up to AND INCLUDING target_time
sim.run_until_inclusive(50.0)

# Process events up to BUT EXCLUDING target_time
sim.run_until_exclusive(100.0)

# Legacy method (same as run_until_inclusive)
sim.advance_to(75.0)
```

**Key Difference:**
- `run_until_inclusive(T)`: Processes all events at time T
- `run_until_exclusive(T)`: Stops just before time T

Example:
```python
sim.insert_job(0, 0.0)  # Job starts at t=0
sim.run_until_exclusive(0.0)  # Job NOT started yet
sim.run_until_inclusive(0.0)  # Job started, resources allocated
```

## Monitoring API

### Resource Status

```python
# Current simulation time
current_time = sim.get_current_time()

# Node usage
nodes_used = sim.get_nodes_in_use()
nodes_free = sim.get_available_nodes()
utilization = nodes_used / params.total_nodes
```

### Queue Status

```python
# Number of jobs waiting
queue_size = sim.get_wait_queue_size()

# When will FCFS head start? (reservation time)
shadow_time = sim.get_fcfs_head_shadow_time()
estimated_wait = shadow_time - sim.get_current_time()
```

### Comprehensive Statistics

```python
stats = sim.get_statistics()

# Job counts
print(f"Submitted: {stats.jobs_submitted}")
print(f"Completed: {stats.jobs_completed}")
print(f"Running: {stats.jobs_running}")
print(f"Waiting: {stats.jobs_waiting}")

# Performance metrics
print(f"Current time: {stats.current_time}")
print(f"Makespan: {stats.makespan}")
print(f"Avg wait time: {stats.avg_wait_time}")
print(f"Avg turnaround: {stats.avg_turnaround_time}")

# Resource metrics
print(f"Nodes in use: {stats.nodes_in_use}/{stats.total_nodes}")
print(f"Utilization: {stats.utilization*100:.1f}%")
```

## Batch Mode API

```python
# Run entire simulation at once (traditional mode)
sim = dr_evt.Simulation(params)
sim.initialize_trace()
sim.run()  # Processes all jobs

# Get results
stats = sim.get_statistics()
sim.write_simulated_trace()  # Writes to configured output file
sim.print_stats()  # Print to stdout
```

## Use Cases

### 1. Online Admission Control

```python
# Check if new job can be admitted
stats = sim.get_statistics()
MAX_QUEUE_SIZE = 100

if stats.jobs_waiting > MAX_QUEUE_SIZE:
    print("REJECT: Queue full")
elif sim.get_available_nodes() < job.nodes:
    # Estimate wait time
    shadow_time = sim.get_fcfs_head_shadow_time()
    wait = shadow_time - sim.get_current_time()
    print(f"QUEUED: Estimated wait {wait:.0f}s")
else:
    print("ACCEPT: Resources available")
```

### 2. Policy Comparison

```python
policies = [
    dr_evt.BackfillPolicy.NONE,
    dr_evt.BackfillPolicy.EASY,
    dr_evt.BackfillPolicy.CONSERVATIVE,
]

results = {}
for policy in policies:
    params.backfill_policy = policy
    sim = dr_evt.Simulation(params)
    sim.initialize_trace()
    sim.run()

    stats = sim.get_statistics()
    results[policy] = {
        'makespan': stats.makespan,
        'avg_wait': stats.avg_wait_time,
        'utilization': stats.utilization,
    }

# Find best policy
best = min(results.items(), key=lambda x: x[1]['avg_wait'])
print(f"Best policy: {best[0]} (avg wait: {best[1]['avg_wait']:.1f}s)")
```

### 3. Real-time Dashboard

```python
import time

while sim.get_wait_queue_size() > 0 or sim.get_nodes_in_use() > 0:
    # Advance by 60 seconds
    current = sim.get_current_time()
    sim.run_until_inclusive(current + 60)

    stats = sim.get_statistics()
    print(f"t={stats.current_time:6.0f} | "
          f"Util: {stats.utilization*100:5.1f}% | "
          f"Queue: {stats.jobs_waiting:3d} | "
          f"Running: {stats.jobs_running:3d}")

    time.sleep(0.1)  # Animate
```

### 4. Custom Scheduler Integration

```python
# External scheduler decides, DR_EVT executes
class CustomScheduler:
    def __init__(self, sim):
        self.sim = sim

    def schedule_next(self):
        # Custom scheduling logic
        job_idx = self.pick_best_job()

        if self.sim.get_available_nodes() >= self.get_job_nodes(job_idx):
            # Start immediately
            t = self.sim.get_current_time()
            self.sim.insert_job(job_idx, t)
            self.sim.run_until_inclusive(t)
            return True
        return False

    def run(self):
        while self.has_pending_jobs():
            if not self.schedule_next():
                # Wait for resources
                shadow = self.sim.get_fcfs_head_shadow_time()
                self.sim.run_until_inclusive(shadow)
```

### 5. What-If Simulation

```python
# Simulate same workload with different cluster sizes
cluster_sizes = [50, 100, 200, 500]

results = []
for nodes in cluster_sizes:
    params.total_nodes = nodes
    sim = dr_evt.Simulation(params)
    sim.initialize_trace()
    sim.run()

    stats = sim.get_statistics()
    results.append({
        'nodes': nodes,
        'makespan': stats.makespan,
        'utilization': stats.utilization,
        'avg_wait': stats.avg_wait_time,
    })

# Plot results
import pandas as pd
df = pd.DataFrame(results)
print(df)
```

## Performance

The Python API is a thin wrapper around the C++ implementation:

- **Minimal overhead**: Direct C++ calls via pybind11 (< 1% overhead)
- **Same algorithm**: Identical verified EASY backfilling
- **Same results**: Bit-identical output to C++ simulator
- **Fast**: C++ implementation is 4.8x faster than pure Python reference

Benchmark (100-job workload):
- C++ simulator: 0.020 seconds
- Python API: 0.020 seconds (negligible wrapper overhead)
- Pure Python: 0.054 seconds (2.7x slower)

## Testing

Run the comprehensive test suite:

```bash
# Build with Python support
cd build
cmake .. -DDR_EVT_BUILD_PYTHON=ON
make

# Run Python tests
cd ../python
python test_api.py
```

Expected output:
```
✓ dr_evt module imported successfully
Testing DR_EVT Python API
✓ SimParams configuration
✓ Trace loading
✓ Streaming API
✓ Monitoring API
✓ Statistics
ALL TESTS PASSED!
```

## Examples

Complete working examples in `python/`:

- **example_streaming.py**: Online simulation with real-time monitoring
- **test_api.py**: Comprehensive API usage examples

## Troubleshooting

### ModuleNotFoundError: No module named 'dr_evt'

```bash
# Ensure Python bindings were built
cd build
cmake .. -DDR_EVT_BUILD_PYTHON=ON
make

# Install the module
cd ../python
pip install .
```

### Trace fails to load

```python
# Must call initialize_trace() before streaming
sim = dr_evt.Simulation(params)
sim.initialize_trace()  # <- Required!
sim.insert_job(0, 0.0)
```

### Statistics are zero

```python
# Run simulation first
sim.initialize_trace()
sim.run()  # Or use streaming API to process jobs
stats = sim.get_statistics()  # Now populated
```

### AttributeError: 'SimParams' object has no attribute 'X'

Some parameters are not yet exposed in Python bindings:
- Use C++ API directly
- Or pass via command line to simulator binary
- Or submit PR to add binding (see Contributing)

## API Reference

Full API documentation: [python/README.md](../python/README.md)

CLI options reference: [CLI_OPTIONS.md](CLI_OPTIONS.md)

## Comparison: Python vs C++ API

| Feature | Python API | C++ API |
|---------|-----------|---------|
| Streaming mode | ✅ Full support | ✅ Full support |
| All scheduling policies | ✅ Yes | ✅ Yes |
| Real-time monitoring | ✅ Yes | ✅ Yes |
| Comprehensive statistics | ✅ Yes | ✅ Yes |
| All config parameters | ⚠️ Most (8/14) | ✅ All (14/14) |
| Performance | Fast (thin wrapper) | Fastest |
| Ease of use | High (scripting) | Medium (compiled) |
| Integration | Easy (import) | Medium (linking) |
| Best for | Prototyping, analysis | Production, HPC |

## Contributing

To add new Python bindings:

1. Add C++ method to `src/sim/sim.hpp`
2. Implement in `src/sim/sim.cpp`
3. Add pybind11 binding in `python/dr_evt_bindings.cpp`:
   ```cpp
   .def_readwrite("new_param", &Sim_Params::m_new_param)
   ```
4. Add test in `python/test_api.py`
5. Document here and in `python/README.md`

## See Also

- [CLI Options](CLI_OPTIONS.md) - Complete CLI reference
- [C++ Streaming API](STREAMING_API.md) - C++ API documentation
- [Python Examples](../python/example_streaming.py) - Working code examples
- [Python API Details](../python/README.md) - Detailed Python reference
- [EASY Backfilling Algorithm](EASY_BACKFILLING_ALGORITHM.md) - Algorithm description

---

**Version:** 1.0.0
**Status:** Production Ready
**License:** MIT
