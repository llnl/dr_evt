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

- [Trace Formats](trace-formats.md) - Input file format specifications
- [Scheduling Policies](scheduling-policies.md) - Algorithm explanations
- [Simulation Modes](simulation-modes.md) - Replay vs simulation
- [Quick Start](../getting-started/quickstart.md) - Quick reference
