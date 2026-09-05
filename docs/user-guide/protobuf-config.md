# Protocol Buffer Configuration

DR_EVT supports structured configuration files using Protocol Buffers (protobuf) for complex simulation setups.

## Why Use Protobuf Config?

**Benefits over command-line arguments:**
- ✅ **Reproducible**: Configuration files can be versioned and shared
- ✅ **Complex setups**: Manage many parameters in one file
- ✅ **Type-safe**: Protobuf validates types and required fields
- ✅ **Documented**: Schema defines all available options
- ✅ **Composable**: Override config with command-line arguments

## Configuration File Format

Configuration files use Protocol Buffer text format (`.textproto` extension).

### Basic Example

`sim_config.textproto`:
```protobuf
sim_setup {
  infile: "trace.csv"
  outfile: "results.csv"
  total_nodes: 1000
  
  backfill_policy: "easy"
  priority_policy: "fcfs"
}
```

Run with:
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator --config sim_config.textproto trace.csv
```

### Complete Example

`advanced_config.textproto`:
```protobuf
sim_setup {
  # Input/Output
  infile: "workload.csv"
  outfile: "schedule_output.csv"
  resource_trace: "node_availability.csv"
  
  # System Configuration
  total_nodes: 2000
  seed: 42
  
  # Scheduling Policies
  backfill_policy: "easy"        # Options: "easy", "conservative", "none"
  priority_policy: "fcfs"         # Options: "fcfs", "sjf", "ljf"
  
  # Queue Implementation (FCFS scheduler only)
  queue_impl: "circular"          # Options: "circular", "deque", "multimap", "block"
  circular_capacity: 0            # 0 = size of job trace; only used when queue_impl="circular"
  circular_overflow: "grow"       # "abort" | "grow"; only used when queue_impl="circular"
  
  # Trace Format
  trace_format: "simple"          # Options: "simple", "lassen"
  timestamp_format: "epoch"       # Options: "epoch", "iso"
  
  # Simulation Limits
  max_jobs: 100000
  max_time: 86400                 # Stop after 86400 seconds (24 hours)
  
  # Duration Simulation
  run_time_mode: "distribution"   # Options: "actual", "distribution", "limit"
  run_time_distribution: "normal" # Options: "normal", "lognormal", "uniform"
  run_time_scale: 0.8             # Jobs run for 80% of time_limit on average
  run_time_stddev: 0.1            # Standard deviation: 10%
  
  # Output Options
  verbose: false
}
```

Run with:
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator --config advanced_config.textproto
```

## Configuration Options Reference

### Input/Output Parameters

| Field | Type | Description |
|-------|------|-------------|
| `infile` | string | Input trace file path (required) |
| `outfile` | string | Output schedule file path (default: `stdout`) |
| `resource_trace` | string | Node availability trace (optional) |

### System Configuration

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `total_nodes` | int32 | Required | Total compute nodes in system |
| `seed` | int32 | Random | Random seed for reproducibility |

### Scheduling Policies

| Field | Type | Default | Options |
|-------|------|---------|---------|
| `backfill_policy` | string | `"easy"` | `"easy"`, `"conservative"`, `"none"` |
| `priority_policy` | string | `"fcfs"` | `"fcfs"`, `"sjf"`, `"ljf"` |
| `queue_impl` | string | `"circular"` | `"circular"`, `"deque"`, `"multimap"`, `"block"` |
| `block_size` | uint32 | `128` | Power of 2; only used when `queue_impl="block"` |
| `circular_capacity` | uint64 | `0` | `0` = size of job trace; only used when `queue_impl="circular"` |
| `circular_overflow` | string | `"grow"` | `"abort"`, `"grow"`; only used when `queue_impl="circular"` |

**backfill_policy:**
- `"easy"` - EASY backfilling (only first queued job gets reservation)
- `"conservative"` - Conservative backfilling (all queued jobs get reservations)
- `"none"` - No backfilling

**priority_policy:**
- `"fcfs"` - First-Come-First-Served (arrival order)
- `"sjf"` - Shortest Job First (by run time estimate)
- `"ljf"` - Longest Job First (by run time estimate)

**queue_impl** (FCFS scheduler only - SJF/LJF always use multimap):
- `"circular"` - boost::circular_buffer-based (default; measured 14-28% faster
  than `deque` - see [`../dev/design-decisions/CIRCULAR_QUEUE.md`](../dev/design-decisions/CIRCULAR_QUEUE.md))
- `"deque"` - std::deque-based (simple, well-tested fallback)
- `"multimap"` - std::multimap-based (for differential testing)
- `"block"` - block-based with multi-index (reference implementation, not
  recommended for performance - see [`../dev/design-decisions/BLOCK_QUEUE.md`](../dev/design-decisions/BLOCK_QUEUE.md))

### Trace Format

| Field | Type | Default | Options |
|-------|------|---------|---------|
| `trace_format` | string | `"simple"` | `"simple"`, `"lassen"` |
| `timestamp_format` | string | `"epoch"` | `"epoch"`, `"iso"` |
| `timezone` | string | `"UTC"` | Any IANA timezone (e.g., `"America/Los_Angeles"`) |

**trace_format:**
- `"simple"` - 7-column CSV format (job_submit_time, begin_time, end_time, num_nodes, exit_status, queue, time_limit)
- `"lassen"` - 33-column LLNL HPC trace format

**timestamp_format:**
- `"epoch"` - Integer seconds since Unix epoch (1970-01-01)
- `"iso"` - ISO 8601 format (e.g., `"2024-01-15T08:00:00"`)

### Simulation Limits

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `max_jobs` | int32 | Unlimited | Stop after processing N jobs |
| `max_time` | double | Unlimited | Stop after N seconds simulation time |

### Run Time Simulation

Control how a job's actual, observed execution length is determined in simulation mode:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `run_time_mode` | string | `"actual"` | How to determine the job's actual run time |
| `run_time_distribution` | string | `"normal"` | Statistical distribution for sampling |
| `run_time_scale` | double | `1.0` | Scale factor for run times |
| `run_time_stddev` | double | `0.0` | Standard deviation (for distributions) |

**run_time_mode:**
- `"actual"` - Read job's actual run time from trace's `actual_run_time` column (default, most realistic)
- `"distribution"` - Sample from statistical distribution around `time_limit × run_time_scale`
- `"limit"` - Jobs run for exactly `time_limit` (debugging only, unrealistic)

**run_time_distribution:**
- `"normal"` - Normal distribution: mean=`time_limit × run_time_scale`, stddev=`run_time_stddev`
- `"lognormal"` - Log-normal distribution with median=`time_limit × run_time_scale`
- `"uniform"` - Uniform distribution: [0, `time_limit × run_time_scale`]

**Example: Realistic Run Time Variation**
```protobuf
sim_setup {
  run_time_mode: "distribution"
  run_time_distribution: "normal"
  run_time_scale: 0.8          # Jobs run for 80% of time_limit on average
  run_time_stddev: 0.1          # ±10% variation
}
```

### Output Options

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `verbose` | bool | `false` | Print detailed scheduling events |

## Command-Line Override

Command-line arguments override protobuf config values:

```bash
# Config file says total_nodes: 1000
${CMAKE_INSTALL_PREFIX}/bin/simulator --config sim_config.textproto --total_nodes 2000

# Result: Uses 2000 nodes (command-line wins)
```

**Precedence (highest to lowest):**
1. Command-line arguments
2. Protobuf config file (`--config`)
3. Built-in defaults

## Common Configurations

### Production Replay

Replay exactly what happened on a real system:

`replay.textproto`:
```protobuf
sim_setup {
  infile: "production_trace.csv"
  outfile: "results.csv"
  
  total_nodes: 2048
  run_time_mode: "actual"  # Use actual run times from trace
  
  backfill_policy: "easy"
  priority_policy: "fcfs"
  
  trace_format: "lassen"
  timestamp_format: "iso"
  timezone: "America/Los_Angeles"
}
```

### What-If Analysis

Simulate how system would behave with different policy:

`what_if.textproto`:
```protobuf
sim_setup {
  infile: "production_trace.csv"
  outfile: "what_if_results.csv"
  
  total_nodes: 2048
  
  # Simulation mode with realistic variation
  run_time_mode: "distribution"
  run_time_distribution: "normal"
  run_time_scale: 0.85
  run_time_stddev: 0.15
  
  # Try conservative backfilling instead of EASY
  backfill_policy: "conservative"
  priority_policy: "fcfs"
  
  trace_format: "simple"
  timestamp_format: "epoch"
}
```

### Capacity Planning

Test if system can handle increased load:

`capacity_test.textproto`:
```protobuf
sim_setup {
  infile: "synthetic_high_load.csv"
  outfile: "capacity_results.csv"
  
  # Test with fewer nodes
  total_nodes: 1500
  
  run_time_mode: "limit"
  
  backfill_policy: "easy"
  priority_policy: "fcfs"
  
  # Stop after 7 days simulation time
  max_time: 604800
  
  verbose: true
}
```

### Performance Testing

Benchmark different queue implementations:

`circular_queue_test.textproto` (default, typically fastest):
```protobuf
sim_setup {
  infile: "large_scale_10k_jobs.csv"
  outfile: "circular_queue_results.csv"
  
  total_nodes: 1000
  
  backfill_policy: "easy"
  priority_policy: "fcfs"
  
  # queue_impl defaults to "circular" - explicit here for clarity.
  # circular_capacity/circular_overflow are optional; omitting them
  # defaults to a capacity sized to the job trace, which can never
  # overflow.
  queue_impl: "circular"
  
  run_time_mode: "limit"
  
  trace_format: "simple"
  timestamp_format: "epoch"
}
```

`block_queue_test.textproto` (reference implementation, not recommended
for performance):
```protobuf
sim_setup {
  infile: "large_scale_10k_jobs.csv"
  outfile: "block_queue_results.csv"
  
  total_nodes: 1000
  
  backfill_policy: "easy"
  priority_policy: "fcfs"
  
  # Use block queue with a 128-job block size
  queue_impl: "block"
  block_size: 128
  
  run_time_mode: "limit"
  
  trace_format: "simple"
  timestamp_format: "epoch"
}
```

## Protocol Buffer Schema

The full schema is defined in `src/proto/dr_evt_params.proto`:

```protobuf
message Simulation_Params {
  // Random seed (default: system clock-dependent if unset)
  uint32 seed = 1;

  // Simulation limits
  uint32 max_jobs = 2;
  double max_time = 3;

  // Input/Output
  string infile = 4;
  string outfile = 5;
  string resource_trace = 6;

  // Enable verbose output for debugging/testing (default: false)
  bool verbose = 7;

  // Scheduling parameters
  int32 total_nodes = 8;          // default: 795
  string backfill_policy = 9;     // "easy", "conservative", or "none" (default: "easy")
  string priority_policy = 10;    // "fcfs", "sjf", or "ljf" (default: "fcfs")

  // Trace format
  string trace_format = 11;       // "simple" or "lassen" (default: "simple")
  string timestamp_format = 12;   // "epoch" or "iso" (default: "iso")
  string timezone = 13;           // e.g. "UTC", "America/Los_Angeles"

  // Duration simulation
  string run_time_mode = 14;          // "actual", "distribution", or "limit" (default: "actual")
  string run_time_distribution = 15;  // "normal", "lognormal", or "uniform" (default: "normal")
  double run_time_scale = 16;         // default: 1.0
  double run_time_stddev = 17;        // default: 0.0

  // Queue implementation (FCFS scheduler only)
  string queue_impl = 18;         // "circular", "deque", "multimap", or "block" (default: "circular")
  uint32 block_size = 19;         // power of 2 (default: 128); only used when queue_impl="block"
  uint64 circular_capacity = 20;  // 0 = size of job trace (default: 0); only used when queue_impl="circular"
  string circular_overflow = 21;  // "abort" or "grow" (default: "grow"); only used when queue_impl="circular"
}
```

This mirrors `src/proto/dr_evt_params.proto`'s actual `Simulation_Params` message -
check that file directly if this drifts out of sync again.

## Validation

Protobuf validates:
- **Type checking**: `total_nodes` must be integer, not string
- **Required fields**: Missing required fields cause errors
- **Enum values**: Invalid policy names are rejected

Example error:
```
Error parsing config file: Unknown field "totalnodes" (did you mean "total_nodes"?)
```

## See Also

- [CLI Options](command-line.md) - Command-line alternatives to protobuf config
- [User Guide Overview](overview.md) - Trace formats and simulation modes
- [Quick Start](../getting-started/quickstart.md) - Basic usage examples
