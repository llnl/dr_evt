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

### `-L, --infile_list FILENAME`
Path to a file listing multiple trace files, one per line - progressive
loading: each is loaded in turn as the simulation reaches it, so
`--job_store_capacity` can actually bound memory (`--infile`/single-file
mode always grows to fit the whole trace regardless of that setting).
Mutually exclusive with `--infile`/the positional trace-file argument -
do not provide both.

**Requirements:** files must already be sorted by `submit_time`, both
within each file and across the sequence (each file's earliest
`submit_time` must be `>=` the previous file's latest).

**Example:**
```bash
./build/simulator --infile_list traces/file_list.txt --job_store_capacity 1000
```
where `traces/file_list.txt` contains, one path per line:
```
traces/part1.csv
traces/part2.csv
traces/part3.csv
```

See [`docs/dev/design-decisions/OUT_TRACE_STREAMING.md`](../dev/design-decisions/OUT_TRACE_STREAMING.md) for the full design.

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
```text
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
- `sjf` - Shortest Job First (by run time estimate)
- `ljf` - Longest Job First (by run time estimate)

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
  - Has a fixed capacity, unlike `deque` - see `--wait_queue_capacity`
    and `--wait_queue_overflow` below
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

### `-A, --wait_queue_capacity SIZE`
Initial capacity of the circular queue. Only used when `--queue_impl circular`.

**Default:** `0`, meaning the size of the job trace - large enough that the
queue can never overflow, since at most one entry is inserted per job in the
trace over the scheduler's lifetime.

A smaller, explicit value trades that guarantee for a smaller initial
allocation; see `--wait_queue_overflow` for what happens if it's exceeded.

**Example:**
```bash
./build/simulator traces/jobs.csv --priority_policy fcfs --queue_impl circular --wait_queue_capacity 1000
```

### `-G, --wait_queue_overflow {abort|grow}`
What to do if an insert would exceed `--wait_queue_capacity`. Only used when
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
    --wait_queue_capacity 500 --wait_queue_overflow abort
```

### `-K, --job_store_capacity SIZE`
Initial capacity of the job-record store (`Trace::m_data`, a
`boost::circular_buffer`). See
[Trace as a streaming-ready state container](../dev/design-decisions/OUT_TRACE_STREAMING.md)
for what this actually buys you - in short: with `--infile` (single-file
mode, loading a whole trace file upfront), capacity always grows to fit
the whole trace during loading regardless of this setting, so a smaller
value here does not reduce the final allocation and essentially never
triggers reclaiming a slot mid-run. Use `--infile_list` instead for a
capacity that can actually bound memory across a trace.

**Default:** `0`, meaning the size of the job trace - large enough that the
store can never overflow, since at most one entry is inserted per job.

**Example:**
```bash
./build/simulator traces/jobs.csv --job_store_capacity 1000
```

### `-W, --job_store_overflow {abort|grow}`
What to do if an insert would exceed `--job_store_capacity`.

**Options:**
- `abort` - end the simulation with a clean error (`std::runtime_error`,
  reported to stderr / to the gRPC client, exit code 1)
- `grow` (default) - reallocate to a larger capacity, copying every
  existing entry over; the simulation continues normally

**Default:** `grow`

**Example:**
```bash
# Fail fast if the job store ever needs more than the pre-sized capacity
./build/simulator traces/jobs.csv \
    --job_store_capacity 500 --job_store_overflow abort
```

### `-m, --check_memory_pressure FRACTION`
Before growing the job-record store for a new batch (`--infile_list`
progressive loading, or a batch appended via the streaming API), refuse
with a clean error if doing so would push projected peak usage past
`FRACTION` of actual available system memory, rather than growing
unconditionally. Independent of `--job_store_overflow` - applies
regardless of whether that's set to `abort` or `grow`.

`FRACTION` must be `> 0.0` and `<= 1.0` (e.g. `0.8` for 80%) - there's
no baked-in default fraction, since what's safe headroom genuinely
differs by environment: a bare-metal HPC node with nothing else running
can tolerate a much looser fraction than a container or
memory-cgroup'd process, where `/proc/meminfo` reports host-level
availability rather than the effective cgroup limit (see below).

Available memory is read from `/proc/meminfo`'s `MemAvailable` on
Linux; a no-op on any other platform (nothing to check against), not a
hard failure.

**Default:** disabled - this option must be given a value to take
effect at all; unlike `--job_store_capacity` (bounding a buffer size
you explicitly chose), this queries the actual machine's memory, which
not everyone wants tied to (e.g. containerized or memory-cgroup'd
environments as noted above).

**Example:**
```bash
./build/simulator --infile_list traces/file_list.txt --check_memory_pressure 0.8
```

See [`docs/dev/design-decisions/OUT_TRACE_STREAMING.md`](../dev/design-decisions/OUT_TRACE_STREAMING.md) for the exact formula (mirrors the actual grow-doubling logic, not a fixed multiplier) and rationale.

### `-H, --resource_history_capacity SIZE`
Initial capacity of the resource-history circular buffer (the
`time,free_nodes,allocated_nodes` samples behind `--resource_trace`).
Bounds memory for long-running/streaming sessions: once full, the whole
buffer is flushed to the `--resource_trace` file (if one was given) and
cleared, in one batch, rather than growing without limit.

Unlike `--wait_queue_overflow`/`--job_store_overflow`, there's no overflow
policy here to configure - every entry is a strictly time-ordered,
already-finalized sample, so it's always immediately safe to reclaim; the
abort/grow fallback those two need for entries that aren't safe to reclaim
yet never applies here.

**Default:** `0`, meaning the size of the job trace (large enough it never
needs to reclaim purely to make room) - though never less than 4096, since
the trace size may still be tiny (or 0, early in a streaming session) at
the moment the very first sample is recorded.

**Example:**
```bash
./build/simulator traces/jobs.csv --resource_trace resources.csv --resource_history_capacity 10000
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

### `-r, --run_time_mode MODE`
How to determine the job's actual, observed execution length in simulation mode.

**Options:**
- `actual` - Read job's actual run time from trace column (default, most realistic).
  Accepted column names: `actual_run_time`, `duration`, `actual_duration`, `run_time`
- `distribution` - Sample from statistical distribution (realistic with variation)
- `limit` - Jobs run exactly their time_limit (unrealistic, for debugging only)

**Default:** `actual`

**Example:**
```bash
./build/simulator traces/jobs.csv --run_time_mode distribution
```

### `-D, --run_time_distribution TYPE`
Statistical distribution for run time sampling (when `--run_time_mode distribution`).

**Options:**
- `normal` - Normal (Gaussian) distribution (default)
- `lognormal` - Log-normal distribution
- `uniform` - Uniform distribution

**Default:** `normal`

**Example:**
```bash
./build/simulator traces/jobs.csv \
    --run_time_mode distribution \
    --run_time_distribution lognormal
```

### `-S, --run_time_scale FACTOR`
Scale factor for job run times.

**Range:** > 0.0

**Default:** 1.0 (100% of time_limit)

**Example:** Jobs run 80% of their time_limit on average:
```bash
./build/simulator traces/jobs.csv \
    --run_time_mode distribution \
    --run_time_scale 0.8
```

### `-V, --run_time_stddev FACTOR`
Standard deviation for run time distribution.

**Range:** >= 0.0

**Default:** 0.0 (no variation)

**Example:** 10% standard deviation:
```bash
./build/simulator traces/jobs.csv \
    --run_time_mode distribution \
    --run_time_scale 0.9 \
    --run_time_stddev 0.1
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
`queue_impl`/`wait_queue_capacity`/`wait_queue_overflow`/`job_store_capacity` this way),
and common configuration patterns, see
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
    --run_time_mode limit \
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
    --outfile simulation_results.csv \
    --verbose
```

### Realistic Simulation with Actual Run Times
```bash
# Most realistic - uses historical execution times from trace
./build/simulator production_trace.csv \
    --total_nodes 2048 \
    --trace_format lassen \
    --timestamp_format iso \
    --timezone America/Los_Angeles \
    --backfill_policy easy \
    --priority_policy fcfs \
    --run_time_mode actual \
    --outfile results.csv
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

### Distribution-Based Run Time Simulation
```bash
./build/simulator input.csv \
    --run_time_mode distribution \
    --run_time_distribution lognormal \
    --run_time_scale 0.85 \
    --run_time_stddev 0.15 \
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
- [Streaming API](../api/STREAMING_API.md) - Programmatic C++ API for online simulation
- [Backfilling Algorithms](../BACKFILLING_ALGORITHMS.md) - EASY and CONSERVATIVE algorithm details
- [Quick Start](../getting-started/quickstart.md) - Quick reference
- [Testing Guide](../TESTING_GUIDE.md) - Running tests and validation
- [Test Suite](https://github.com/llnl/dr_evt/blob/main/tests/README.md) - Example usage in test scripts
- [Block Queue Implementation](../dev/design-decisions/BLOCK_QUEUE.md) - Performance analysis of `--queue_impl block`
- [Circular Queue Implementation](../dev/design-decisions/CIRCULAR_QUEUE.md) - Performance analysis of `--queue_impl circular`
