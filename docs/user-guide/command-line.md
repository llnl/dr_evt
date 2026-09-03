# Command-Line Options

Complete reference for all DR_EVT command-line options for the `simulator` binary.

## Basic Usage

```bash
./build/simulator INPUT_FILE [OPTIONS]
```

`INPUT_FILE` (the input job trace, in CSV format) can also be given via
`-i, --infile FILENAME` instead of as the first positional argument.

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

**Default:** Derived from input filename (e.g., `jobs.csv` -> `jobs_sim.csv`)

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
- `none` - Pure FCFS (no backfilling)

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
- `circular` - boost::circular_buffer-based (default)
  - Same O(1) push_back/pop_front as `deque`, but backed by one
    contiguous array instead of `deque`'s chunked storage, so indexed
    access (used throughout the backfill scan) is a direct offset
    rather than a chunk-lookup-then-offset
  - **Performance:** measured 14-28% *faster* than `deque` on a 10,000
    job / 2,000 node benchmark (see [`dev/design-decisions/CIRCULAR_QUEUE.md`](../dev/design-decisions/CIRCULAR_QUEUE.md))
  - Has a fixed capacity, unlike `deque` - see `--circular_capacity`
    and `--circular_overflow` below
- `deque` - std::deque-based
  - Simple, well-tested sequential container
  - Linear backfill search O(n)
  - Kept as a well-tested fallback and for differential testing
- `multimap` - std::multimap-based (FCFS_ALT)
  - Tree-based container for differential testing
  - Produces identical schedules to `deque`/`circular`
  - Useful for verifying FCFS correctness
- `block` - BlockWaitQueue-based
  - Block-based container with metadata pre-filtering
  - Tunable block size (default: 128 jobs per block)
  - **Performance:** `deque` is 30% faster even at the optimal block
    size (16); smaller/larger sizes are worse still, up to 97% slower
    at block size 256 (see [`dev/design-decisions/BLOCK_QUEUE.md`](../dev/design-decisions/BLOCK_QUEUE.md)) - each
    block's multi-index red-black trees dominate the overhead. Kept
    for differential testing and as a reference implementation; not
    recommended over `deque` or `circular` for typical HPC workloads.

**Default:** `circular`

**Note:** This option only affects FCFS scheduler. SJF/LJF always use std::multimap
(already efficient for priority-based scheduling). If `deque`, `block`, or `multimap`
is specified with SJF/LJF, a warning is printed and the default multimap is used.

**Examples:**
```bash
# Standard FCFS with circular queue (default, typically the fastest option)
./build/simulator traces/jobs.csv --priority_policy fcfs

# FCFS with deque explicitly (simple, well-tested fallback)
./build/simulator traces/jobs.csv --priority_policy fcfs --queue_impl deque

# FCFS with block queue (reference implementation, not recommended for performance)
./build/simulator traces/large_10k_jobs.csv --priority_policy fcfs --queue_impl block

# Differential testing: compare deque vs multimap (should produce identical output)
./build/simulator traces/jobs.csv --priority_policy fcfs --queue_impl deque --outfile output_deque.csv
./build/simulator traces/jobs.csv --priority_policy fcfs --queue_impl multimap --outfile output_multimap.csv
diff output_deque.csv output_multimap.csv  # Should be identical
```

### `-A, --circular_capacity SIZE`
Initial capacity of the circular queue. Only used when `--queue_impl circular`.

**Default:** `0`, meaning the size of the job trace - large enough that the
queue can never overflow, since at most one entry is inserted per job in the
trace over the scheduler's lifetime.

A smaller, explicit value trades that guarantee for a smaller initial
allocation; see `--circular_overflow` for what happens if it's exceeded.

**Example:**
```bash
./build/simulator traces/jobs.csv --priority_policy fcfs --queue_impl circular --circular_capacity 1000
```

### `-G, --circular_overflow {abort|grow}`
What to do if an insert would exceed `--circular_capacity`. Only used when
`--queue_impl circular`.

**Options:**
- `abort` - end the simulation with a clean error (`std::runtime_error`,
  reported to stderr / to the gRPC client, exit code 1)
- `grow` (default) - reallocate to double the current capacity via
  `boost::circular_buffer::set_capacity()`, which preserves every existing
  entry; the simulation continues normally

**Default:** `grow`

**Example:**
```bash
# Fail fast if the queue ever needs more than the pre-sized capacity
./build/simulator traces/jobs.csv --priority_policy fcfs --queue_impl circular \
    --circular_capacity 500 --circular_overflow abort
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
Load parameters from a Protobuf `.textproto` configuration file.

**Requires:** Simulator built with `-DDR_EVT_ENABLE_PROTOBUF=ON`

**Precedence (highest to lowest):**
1. Command-line arguments (highest priority)
2. Config file (`--config`)
3. Built-in defaults (lowest priority)

**Example:**
```bash
./build/simulator traces/jobs.csv \
    --config config.textproto \
    --total_nodes 200  # Overrides config file value
```

For the full `.textproto` schema, worked examples (including how to set
`queue_impl`/`circular_capacity`/`circular_overflow` this way), and common
configuration patterns, see
[Protobuf Configuration](protobuf-config.md).

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

### With Real HPC Trace
```bash
./build/simulator lassen_trace.csv \
    --total_nodes 795 \
    --trace_format lassen \
    --timestamp_format iso \
    --timezone America/Los_Angeles \
    --runtime_mode actual \
    --outfile simulation_results.csv \
    --verbose
```

### Replay Mode (Reproduce Historical Behavior)
```bash
# runtime_mode=actual + duration_mode=column together reproduce exactly
# what happened on the real system: actual runtimes drive both the
# scheduler's decisions and how long each job actually takes.
./build/simulator production_trace.csv \
    --total_nodes 2048 \
    --trace_format lassen \
    --timestamp_format iso \
    --timezone America/Los_Angeles \
    --runtime_mode actual \
    --duration_mode column \
    --backfill_policy easy \
    --priority_policy fcfs \
    --outfile replay_results.csv
```

### Different Scheduling Policies
```bash
# EASY backfilling with SJF
./build/simulator input.csv \
    --backfill_policy easy \
    --priority_policy sjf \
    --outfile results.csv

# Conservative backfilling
./build/simulator input.csv \
    --backfill_policy conservative \
    --outfile results.csv

# Pure FCFS (no backfilling)
./build/simulator input.csv \
    --backfill_policy none \
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
./build/simulator input.csv --config my_config.textproto
```

## See Also

- [User Guide Overview](overview.md) - Complete user guide with trace formats and simulation modes
- [Protobuf Configuration](protobuf-config.md) - Full `.textproto` schema and worked examples
- [Streaming API](../STREAMING_API.md) - Programmatic C++ API for online simulation
- [EASY Backfilling Algorithm](../EASY_BACKFILLING_ALGORITHM.md) - Scheduling algorithm details
- [Quick Start](../getting-started/quickstart.md) - Quick reference
- [Testing Guide](../TESTING_GUIDE.md) - Running tests and validation
- [Test Suite](../../tests/README.md) - Example usage in test scripts
- [Block Queue Implementation](../dev/design-decisions/BLOCK_QUEUE.md) - Performance analysis of `--queue_impl block`
- [Circular Queue Implementation](../dev/design-decisions/CIRCULAR_QUEUE.md) - Performance analysis of `--queue_impl circular`
