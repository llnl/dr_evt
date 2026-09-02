# Command-Line Options

Complete reference for all DR_EVT command-line options.

## Basic Usage

```bash
./simulator INPUT_FILE [OPTIONS]
```

## Required Arguments

### Input File
```bash
./simulator trace.csv
```
Path to input trace file in CSV format.

## Simulation Options

### Total Nodes
```bash
--total_nodes N
```
Total number of compute nodes in the system.

**Example:** `--total_nodes 100`

### Backfill Policy
```bash
--backfill_policy {easy|conservative|none}
```
- `easy` - EASY backfilling (default, verified correct)
- `conservative` - Conservative backfilling
- `none` - Pure FCFS (no backfilling)

**Example:** `--backfill_policy easy`

### Priority Policy
```bash
--priority_policy {fcfs|sjf|ljf}
```
- `fcfs` - First Come First Served (default)
- `sjf` - Shortest Job First
- `ljf` - Longest Job First

**Example:** `--priority_policy fcfs`

### Runtime Estimation Mode
```bash
--runtime_mode {use_limit|use_actual}
```
- `use_limit` - Use time_limit field (default)
- `use_actual` - Use actual runtime from trace

**Example:** `--runtime_mode use_limit`

## Trace Format Options

### Trace Format
```bash
--trace_format {simple|lassen}
```
- `simple` - Minimal CSV format (7 columns)
- `lassen` - LLNL Lassen format (33 columns, default)

**Example:** `--trace_format simple`

### Timestamp Format
```bash
--timestamp_format {epoch|iso}
```
- `epoch` - Unix epoch seconds (0, 100, 1234567890)
- `iso` - ISO 8601 timestamps (2024-01-15T10:30:00)

**Example:** `--timestamp_format epoch`

### Timezone
```bash
--timezone TIMEZONE
```
Timezone for ISO timestamps (ignored for epoch format).

**Examples:**
- `--timezone UTC`
- `--timezone America/Los_Angeles` (default)
- `--timezone Europe/London`

## Duration Mode

### Duration Mode
```bash
--duration_mode {exact|from_column|distribution}
```
**Simulation mode only:**
- `exact` - Jobs run exactly for their time_limit
- `from_column` - Use actual_duration column from trace
- `distribution` - Sample from statistical distribution

**Example:** `--duration_mode exact`

### Duration Distribution
```bash
--duration_distribution {normal|lognormal|uniform}
```
Used with `--duration_mode distribution`.

**Example:** `--duration_distribution normal --duration_scale 0.9 --duration_stddev 0.1`

## Output Options

### Output File
```bash
--outfile FILE
```
Write simulated trace to FILE.

**Example:** `--outfile results.csv`

### Verbose Mode
```bash
--verbose
```
Enable verbose output (shows detailed simulation progress).

**Example:** `./simulator trace.csv --verbose`

## Advanced Options

### Random Seed
```bash
--seed N
```
Random seed for duration sampling (default: 0).

**Example:** `--seed 42`

### Maximum Jobs
```bash
--max_jobs N
```
Process only first N jobs from trace.

**Example:** `--max_jobs 100`

### Maximum Time
```bash
--max_time T
```
Stop simulation at time T.

**Example:** `--max_time 3600`

## Complete Examples

### Basic Simulation
```bash
./simulator trace.csv \
  --total_nodes 100 \
  --trace_format simple \
  --timestamp_format epoch \
  --duration_mode exact \
  --backfill_policy easy \
  --outfile results.csv
```

### With Real HPC Trace
```bash
./simulator lassen_trace.csv \
  --total_nodes 795 \
  --trace_format lassen \
  --timestamp_format iso \
  --timezone America/Los_Angeles \
  --runtime_mode use_actual \
  --outfile simulation_results.csv \
  --verbose
```

### Different Scheduling Policies
```bash
# EASY backfilling with SJF
./simulator trace.csv \
  --total_nodes 100 \
  --backfill_policy easy \
  --priority_policy sjf

# Conservative backfilling
./simulator trace.csv \
  --total_nodes 100 \
  --backfill_policy conservative

# Pure FCFS (no backfilling)
./simulator trace.csv \
  --total_nodes 100 \
  --backfill_policy none
```

### Statistical Duration Sampling
```bash
./simulator trace.csv \
  --total_nodes 100 \
  --duration_mode distribution \
  --duration_distribution normal \
  --duration_scale 0.9 \
  --duration_stddev 0.1 \
  --seed 42
```

## See Also

- [User Guide Overview](overview.md) - Complete user guide with trace formats and simulation modes
- [EASY Backfilling Algorithm](../EASY_BACKFILLING_ALGORITHM.md) - Scheduling algorithm details
- [Quick Start](../getting-started/quickstart.md) - Quick reference
- [Testing Guide](../TESTING_GUIDE.md) - Running tests and validation

## Configuration Files (Protocol Buffer)

Instead of long command lines, use a `.textproto` configuration file:

### Basic Config File

`config.textproto`:
```protobuf
simulation_params {
  infile: "trace.csv"
  outfile: "results.csv"
  total_nodes: 1000
  backfill_policy: "easy"
  priority_policy: "fcfs"
}
```

Run with:
```bash
./simulator --config config.textproto
```

### Config + Command-Line Override

Command-line arguments override config file values:

```bash
# Config says total_nodes: 1000, command-line overrides to 2000
./simulator --config config.textproto --total_nodes 2000
```

**Precedence (highest to lowest):**
1. Command-line arguments (highest priority)
2. Config file (`--config`)
3. Built-in defaults (lowest priority)

### All Options in Config File

```protobuf
simulation_params {
  # === Input/Output ===
  infile: "workload.csv"
  outfile: "schedule.csv"
  
  # === System ===
  total_nodes: 2000
  seed: 42
  
  # === Scheduling ===
  backfill_policy: "easy"        # easy | conservative | none
  priority_policy: "fcfs"        # fcfs | sjf | ljf
  runtime_mode: "limit"          # limit | actual
  
  # === Trace Format ===
  trace_format: "simple"         # simple | lassen
  timestamp_format: "epoch"      # epoch | iso
  timezone: "America/Los_Angeles"
  
  # === Duration Simulation ===
  duration_mode: "distribution"  # exact | column | distribution
  duration_distribution: "normal" # normal | uniform | exponential
  duration_scale: 0.8            # Scale factor (0.0 - 1.0)
  duration_stddev: 0.15          # Standard deviation
  
  # === Limits ===
  max_jobs: 100000
  time_limit: 86400              # Stop after 24 hours sim time
  
  # === Output ===
  verbose: false
}
```

### Common Configuration Patterns

**Replay Mode (Reproduce Historical Behavior):**
```protobuf
simulation_params {
  infile: "production_trace.csv"
  total_nodes: 2048
  
  runtime_mode: "actual"         # Use actual runtimes from trace
  duration_mode: "column"
  
  backfill_policy: "easy"
  trace_format: "lassen"
  timestamp_format: "iso"
  timezone: "America/Los_Angeles"
}
```

**What-If Analysis (Test Different Policy):**
```protobuf
simulation_params {
  infile: "production_trace.csv"
  total_nodes: 2048
  
  runtime_mode: "limit"          # Use scheduler estimates
  duration_mode: "distribution"  # Realistic variation
  duration_distribution: "normal"
  duration_scale: 0.85           # Jobs run ~85% of time_limit
  duration_stddev: 0.1           # ±10% variation
  
  backfill_policy: "conservative" # Try different policy!
}
```

**Capacity Planning (Test With More Load):**
```protobuf
simulation_params {
  infile: "synthetic_high_load.csv"
  total_nodes: 1500              # Test with fewer nodes
  
  runtime_mode: "limit"
  duration_mode: "exact"
  
  time_limit: 604800             # Simulate 7 days
  verbose: true
}
```

### Validation

Protobuf validates configuration:
- Type checking (integers, strings, booleans)
- Unknown field detection
- Invalid enum values

Example error:
```
Error: Unknown field "totalnodes" (did you mean "total_nodes"?)
```

