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
simulation_params {
  infile: "trace.csv"
  outfile: "results.csv"
  total_nodes: 1000
  
  backfill_policy: "easy"
  priority_policy: "fcfs"
  runtime_mode: "limit"
}
```

Run with:
```bash
./simulator --config sim_config.textproto trace.csv
```

### Complete Example

`advanced_config.textproto`:
```protobuf
simulation_params {
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
  runtime_mode: "limit"           # Options: "limit", "actual"
  
  # Trace Format
  trace_format: "simple"          # Options: "simple", "lassen"
  timestamp_format: "epoch"       # Options: "epoch", "iso"
  
  # Simulation Limits
  max_jobs: 100000
  time_limit: 86400               # Stop after 86400 seconds (24 hours)
  
  # Duration Simulation
  duration_mode: "distribution"   # Options: "exact", "column", "distribution"
  duration_distribution: "normal" # Options: "normal", "uniform", "exponential"
  duration_scale: 0.8             # Jobs run for 80% of time_limit on average
  duration_stddev: 0.1            # Standard deviation: 10%
  
  # Output Options
  verbose: false
  log_level: "info"               # Options: "debug", "info", "warning", "error"
}
```

Run with:
```bash
./simulator --config advanced_config.textproto
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
| `backfill_policy` | string | `"none"` | `"easy"`, `"conservative"`, `"none"` |
| `priority_policy` | string | `"fcfs"` | `"fcfs"`, `"sjf"`, `"ljf"` |
| `runtime_mode` | string | `"limit"` | `"limit"`, `"actual"` |

**backfill_policy:**
- `"easy"` - EASY backfilling (only first queued job gets reservation)
- `"conservative"` - Conservative backfilling (all queued jobs get reservations)
- `"none"` - No backfilling

**priority_policy:**
- `"fcfs"` - First-Come-First-Served (arrival order)
- `"sjf"` - Shortest Job First (by runtime estimate)
- `"ljf"` - Longest Job First (by runtime estimate)

**runtime_mode:**
- `"limit"` - Use `time_limit` column for scheduling decisions (simulation mode)
- `"actual"` - Use `actual_runtime` column (replay mode)

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
| `time_limit` | double | Unlimited | Stop after N seconds simulation time |

### Duration Simulation

Control how long jobs run in simulation mode:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `duration_mode` | string | `"exact"` | How to determine job runtime |
| `duration_distribution` | string | `"normal"` | Statistical distribution for sampling |
| `duration_scale` | double | `1.0` | Scale factor for durations |
| `duration_stddev` | double | `0.0` | Standard deviation (for distributions) |

**duration_mode:**
- `"exact"` - Jobs run for exactly `time_limit` (perfect estimates)
- `"column"` - Read `actual_duration` from trace file
- `"distribution"` - Sample from statistical distribution

**duration_distribution:**
- `"normal"` - Normal distribution: mean=`time_limit × duration_scale`, stddev=`duration_stddev`
- `"uniform"` - Uniform distribution: [0, `time_limit × duration_scale`]
- `"exponential"` - Exponential distribution: lambda=`1/(time_limit × duration_scale)`

**Example: Realistic Runtime Variation**
```protobuf
simulation_params {
  runtime_mode: "limit"
  duration_mode: "distribution"
  duration_distribution: "normal"
  duration_scale: 0.8          # Jobs run for 80% of time_limit on average
  duration_stddev: 0.1          # ±10% variation
}
```

### Output Options

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `verbose` | bool | `false` | Print detailed scheduling events |
| `log_level` | string | `"info"` | Logging verbosity |

## Command-Line Override

Command-line arguments override protobuf config values:

```bash
# Config file says total_nodes: 1000
./simulator --config sim_config.textproto --total_nodes 2000

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
simulation_params {
  infile: "production_trace.csv"
  outfile: "replay_results.csv"
  
  total_nodes: 2048
  
  # Replay mode: use actual runtimes from trace
  runtime_mode: "actual"
  duration_mode: "column"
  
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
simulation_params {
  infile: "production_trace.csv"
  outfile: "what_if_results.csv"
  
  total_nodes: 2048
  
  # Simulation mode with realistic variation
  runtime_mode: "limit"
  duration_mode: "distribution"
  duration_distribution: "normal"
  duration_scale: 0.85
  duration_stddev: 0.15
  
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
simulation_params {
  infile: "synthetic_high_load.csv"
  outfile: "capacity_results.csv"
  
  # Test with fewer nodes
  total_nodes: 1500
  
  runtime_mode: "limit"
  duration_mode: "exact"
  
  backfill_policy: "easy"
  priority_policy: "fcfs"
  
  # Stop after 7 days simulation time
  time_limit: 604800
  
  verbose: true
}
```

### Performance Testing

Benchmark different block queue sizes:

`block_queue_test.textproto`:
```protobuf
simulation_params {
  infile: "large_scale_10k_jobs.csv"
  outfile: "block_queue_results.csv"
  
  total_nodes: 1000
  
  # Use block queue (command-line: --queue_impl block --block_size 128)
  backfill_policy: "easy"
  priority_policy: "fcfs"
  
  runtime_mode: "limit"
  duration_mode: "exact"
  
  trace_format: "simple"
  timestamp_format: "epoch"
}
```

## Protocol Buffer Schema

The full schema is defined in `src/proto/dr_evt_params.proto`:

```protobuf
message Simulation_Params {
  // Input/Output
  string infile = 1;
  string outfile = 2;
  string resource_trace = 3;
  
  // System configuration
  int32 total_nodes = 4;
  int32 seed = 5;
  
  // Scheduling policies
  string backfill_policy = 6;
  string priority_policy = 7;
  string runtime_mode = 8;
  
  // Trace format
  string trace_format = 9;
  string timestamp_format = 10;
  string timezone = 11;
  
  // Simulation limits
  int32 max_jobs = 12;
  double time_limit = 13;
  
  // Duration simulation
  string duration_mode = 14;
  string duration_distribution = 15;
  double duration_scale = 16;
  double duration_stddev = 17;
  
  // Output options
  bool verbose = 18;
  string log_level = 19;
}
```

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
