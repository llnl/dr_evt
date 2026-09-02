# Command-Line Options

This document describes all command-line options for the `simulator` binary.

## Configuration Methods

DR_EVT supports two ways to configure simulations:

### 1. Command-Line Arguments (documented below)
```bash
./simulator trace.csv --total_nodes 1000 --backfill_policy easy
```

### 2. Protocol Buffer Config Files (recommended for complex setups)
```bash
./simulator --config sim_config.textproto
```

**When to use config files:**
- ✅ Complex simulations with 10+ parameters
- ✅ Reproducible configurations (version in git)
- ✅ Sharing setups with team
- ✅ Avoiding command-line mistakes

**Configuration precedence:**
1. Command-line arguments (highest priority)
2. Config file (`--config`)
3. Built-in defaults (lowest priority)

**Example override:**
```bash
# Config file says total_nodes=1000, CLI overrides to 2000
./simulator --config config.textproto --total_nodes 2000
```

**See:** [Command-Line Reference](user-guide/command-line.md#configuration-files-protocol-buffer) for protobuf config examples and all available parameters.

---

## Quick Reference

```bash
./build/simulator <input_trace> [options]
```

## Input/Output Options

### `-i, --infile FILENAME`
Input job trace file. Can also be specified as the first positional argument.

**Format:** CSV with columns `job_submit_time`, `num_nodes`, `time_limit`, etc.

**Example:**
```bash
./build/simulator --infile traces/jobs.csv
```

### `-o, --outfile FILENAME`
Output file for simulated job trace.

**Format:** CSV with columns `job_submit_time`, `begin_time`, `end_time`, `num_nodes`, `exit_status`, `queue`, `time_limit`

**Default:** Derived from input filename (e.g., `jobs.csv` → `jobs_sim.csv`)

**Example:**
```bash
./build/simulator traces/jobs.csv --outfile output/result.csv
```

### `-R, --resource_trace FILENAME`
Write resource usage trace to file.

**Format:** CSV with columns `time`, `free_nodes`, `allocated_nodes`

**Purpose:** Track cluster resource utilization over time for visualization and analysis.

**Example:**
```bash
./build/simulator traces/jobs.csv \
    --outfile results/jobs.csv \
    --resource_trace results/resources.csv
```

**Default:** If not specified, resource trace is written to `<outfile>_resources.csv`

**Output example:**
```csv
time,free_nodes,allocated_nodes
0,100,0
0,20,80
10,5,95
40,20,80
100,100,0
```

## System Configuration

### `-n, --total_nodes COUNT`
Total number of nodes in the simulated cluster.

**Default:** 795

**Example:**
```bash
./build/simulator traces/jobs.csv --total_nodes 100
```

## Scheduling Policies

### `-b, --backfill_policy POLICY`
Backfilling algorithm to use.

**Options:**
- `easy` - EASY backfilling (default): backfill jobs that complete before FCFS head reservation
- `conservative` - Conservative backfilling: backfill only if won't delay any waiting job

**Default:** `easy`

**Example:**
```bash
./build/simulator traces/jobs.csv --backfill_policy conservative
```

### `-p, --priority_policy POLICY`
Job priority/ordering policy.

**Options:**
- `fcfs` - First Come First Served (default)
- `sjf` - Shortest Job First (by runtime estimate)
- `ljf` - Longest Job First (by runtime estimate)

**Default:** `fcfs`

**Example:**
```bash
./build/simulator traces/jobs.csv --priority_policy sjf
```

### `-q, --queue_impl IMPLEMENTATION`
Wait queue implementation (FCFS scheduler only).

**Options:**
- `deque` - std::deque-based (default)
  - Simple, well-tested sequential container
  - Linear backfill search O(n)
  - Recommended for <1000 queued jobs
- `multimap` - std::multimap-based (FCFS_ALT)
  - Tree-based container for differential testing
  - Produces identical schedules to `deque`
  - Useful for verifying FCFS correctness
- `block` - BlockWaitQueue-based (optimized)
  - Block-based container with metadata pre-filtering
  - Faster backfill search O(blocks) via block-level metadata
  - Tunable block size (default: 128 jobs per block)
  - Recommended for >1000 queued jobs or backfill-heavy workloads
  - **Performance:** 2-10x speedup on large-scale FCFS workloads

**Default:** `deque`

**Note:** This option only affects FCFS scheduler. SJF/LJF always use std::multimap
(already efficient for priority-based scheduling). If `block` or `multimap` is specified
with SJF/LJF, a warning is printed and the default multimap is used.

**Examples:**
```bash
# Standard FCFS with deque (default)
./build/simulator traces/jobs.csv --priority_policy fcfs

# FCFS with block queue optimization (large-scale workloads)
./build/simulator traces/large_10k_jobs.csv --priority_policy fcfs --queue_impl block

# Differential testing: compare deque vs multimap (should produce identical output)
./build/simulator traces/jobs.csv --priority_policy fcfs --queue_impl deque --outfile output_deque.csv
./build/simulator traces/jobs.csv --priority_policy fcfs --queue_impl multimap --outfile output_multimap.csv
diff output_deque.csv output_multimap.csv  # Should be identical
```

### `-r, --runtime_mode MODE`
How to estimate job runtimes for scheduling decisions.

**Options:**
- `limit` - Use job's time_limit field (default)
- `actual` - Use actual runtime (omniscient scheduler, for analysis)

**Default:** `limit`

**Example:**
```bash
./build/simulator traces/jobs.csv --runtime_mode actual
```

## Trace Format Options

### `-f, --trace_format FORMAT`
Input trace format.

**Options:**
- `simple` - Simple CSV format (minimal columns)
- `lassen` - Lassen HPC format (many metadata columns)

**Default:** `lassen`

**Example:**
```bash
./build/simulator traces/simple.csv --trace_format simple
```

### `-T, --timestamp_format FORMAT`
Timestamp format in output.

**Options:**
- `epoch` - Unix epoch seconds (e.g., `1693234567.0`)
- `iso` - ISO 8601 format (e.g., `2026-08-29T14:35:00-07:00`)

**Default:** `iso`

**Example:**
```bash
./build/simulator traces/jobs.csv --timestamp_format epoch
```

### `-z, --timezone TIMEZONE`
Timezone for ISO timestamp output.

**Format:** IANA timezone database name (e.g., `"America/Los_Angeles"`, `"UTC"`, `"America/New_York"`)

**Default:** `America/Los_Angeles`

**Example:**
```bash
./build/simulator traces/jobs.csv \
    --timestamp_format iso \
    --timezone "America/New_York"
```

## Simulation Mode Options

### `-d, --duration_mode MODE`
How to determine job execution durations in simulation mode.

**Options:**
- `exact` - Jobs run exactly their time_limit (default)
- `column` - Use `duration` column from input trace
- `distribution` - Sample from statistical distribution

**Default:** `exact`

**Example:**
```bash
./build/simulator traces/jobs.csv --duration_mode distribution
```

### `-D, --duration_distribution TYPE`
Statistical distribution for duration sampling (when `--duration_mode distribution`).

**Options:**
- `normal` - Normal (Gaussian) distribution (default)
- `lognormal` - Log-normal distribution
- `uniform` - Uniform distribution

**Default:** `normal`

**Example:**
```bash
./build/simulator traces/jobs.csv \
    --duration_mode distribution \
    --duration_distribution lognormal
```

### `-S, --duration_scale FACTOR`
Scale factor for job durations.

**Range:** > 0.0

**Default:** 1.0 (100% of time_limit)

**Example:** Jobs run 80% of their time_limit on average:
```bash
./build/simulator traces/jobs.csv \
    --duration_mode distribution \
    --duration_scale 0.8
```

### `-V, --duration_stddev FACTOR`
Standard deviation for duration distribution.

**Range:** >= 0.0

**Default:** 0.0 (no variation)

**Example:** 10% standard deviation:
```bash
./build/simulator traces/jobs.csv \
    --duration_mode distribution \
    --duration_scale 0.9 \
    --duration_stddev 0.1
```

## Limit Options

### `-j, --max_jobs COUNT`
Maximum number of jobs to simulate.

**Default:** Unlimited (process all jobs in trace)

**Example:**
```bash
./build/simulator traces/jobs.csv --max_jobs 100
```

### `-t, --max_time TIME`
Maximum simulation time (in trace time units).

**Default:** Unlimited (run until all jobs complete)

**Example:**
```bash
./build/simulator traces/jobs.csv --max_time 3600.0
```

### `-s, --seed VALUE`
Random number generator seed for reproducibility.

**Default:** System clock

**Example:**
```bash
./build/simulator traces/jobs.csv --seed 42
```

## Configuration File Option

### `-c, --config CONFIGFILE`
Load parameters from Protobuf configuration file (`.pb` format).

**Requires:** Simulator built with `-DDR_EVT_ENABLE_PROTOBUF=ON`

**Precedence:** Config file loaded first, then command-line options override.

**Example config file** (`config.pb`):
```protobuf
total_nodes: 100
trace_format: "simple"
timestamp_format: "epoch"
backfill_policy: "easy"
priority_policy: "fcfs"
duration_mode: "exact"
resource_trace: "/tmp/resources.csv"
```

**Usage:**
```bash
./build/simulator traces/jobs.csv \
    --config config.pb \
    --total_nodes 200  # Overrides config file value
```

## Debug Options

### `-v, --verbose`
Enable verbose output for debugging.

**Output includes:**
- Simulation progress
- Scheduling decisions
- Resource usage
- Job state transitions

**Example:**
```bash
./build/simulator traces/jobs.csv --verbose
```

### `-h, --help`
Display help message with all options.

```bash
./build/simulator --help
```

## Common Usage Patterns

### Basic Simulation
```bash
./build/simulator input.csv \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --duration_mode exact \
    --outfile output.csv
```

### Simulation with Resource Tracking
```bash
./build/simulator input.csv \
    --total_nodes 100 \
    --outfile jobs.csv \
    --resource_trace resources.csv
```

### Conservative Backfilling with SJF
```bash
./build/simulator input.csv \
    --backfill_policy conservative \
    --priority_policy sjf \
    --outfile results.csv
```

### Distribution-Based Duration Simulation
```bash
./build/simulator input.csv \
    --duration_mode distribution \
    --duration_distribution lognormal \
    --duration_scale 0.85 \
    --duration_stddev 0.15 \
    --seed 42 \
    --outfile simulated.csv
```

### Using Config File
```bash
./build/simulator input.csv --config my_config.pb
```

## See Also

- [Streaming API](STREAMING_API.md) - Programmatic C++ API for online simulation
- [Test Suite](../tests/README.md) - Example usage in test scripts
- [Performance Analysis](SCHEDULER_PERFORMANCE_ANALYSIS.md) - Performance characteristics
