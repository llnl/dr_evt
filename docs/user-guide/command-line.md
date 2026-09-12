# Command-Line Options

Complete reference for all DR_EVT command-line options for the `simulator` binary.

## All options

| Group | Option | Purpose |
|---|---|---|
| Input/output | `-i, --infile FILENAME` | Read one job trace. |
| Input/output | `-L, --infile_list FILENAME` | Progressively read the trace files named in a list. |
| Input/output | `-o, --outfile FILENAME` | Write the simulated job schedule. |
| Input/output | `-R, --resource_trace FILENAME` | Write resource history. |
| System | `-n, --total_nodes COUNT` | Set simulated cluster capacity. |
| Scheduling | `-b, --backfill_policy POLICY` | Select `easy`, `conservative`, or `none`. |
| Scheduling | `-p, --priority_policy POLICY` | Select the job-ordering policy. |
| Scheduling | `-q, --queue_impl IMPLEMENTATION` | Select the FCFS wait-queue implementation. |
| Scheduling | `-Q, --block_size SIZE` | Set the `block` queue's block size. |
| Storage | `-A, --wait_queue_capacity SIZE` | Set initial wait-queue capacity. |
| Storage | `-G, --wait_queue_overflow POLICY` | Select `abort` or `grow` for wait-queue overflow. |
| Storage | `-K, --job_store_capacity SIZE` | Set initial job-store capacity. |
| Storage | `-W, --job_store_overflow POLICY` | Select `abort` or `grow` for job-store overflow. |
| Storage | `--job_flush_interval RECORDS` | Set the departure count between opportunistic job-output flushes. |
| Storage | `-m, --check_memory_pressure FRACTION` | Guard job-store growth using available memory. |
| Storage | `-H, --resource_history_capacity SIZE` | Set resource-history capacity. |
| Trace | `--trace_type TYPE` | Select the standard or experimental record model. |
| Trace | `-f, --trace_format FORMAT` | Select the input trace schema. |
| Trace | `-T, --timestamp_format FORMAT` | Select epoch or ISO output timestamps. |
| Trace | `-z, --timezone TIMEZONE` | Set the timezone for ISO output timestamps. |
| Trace | `-M, --msec_output` | Preserve millisecond precision in output timestamps. |
| Runtime | `-r, --run_time_mode MODE` | Select how actual execution lengths are determined. |
| Runtime | `-D, --run_time_distribution TYPE` | Select the sampled runtime distribution. |
| Runtime | `-S, --run_time_scale FACTOR` | Scale sampled job runtimes. |
| Runtime | `-V, --run_time_stddev FACTOR` | Set sampled runtime variation. |
| Limits | `-j, --max_jobs COUNT` | Limit the number of simulated jobs. |
| Limits | `-t, --max_time TIME` | Limit simulation time. |
| Other | `-s, --seed VALUE` | Set the random-number seed. |
| Other | `-c, --config CONFIGFILE` | Load a Protobuf text configuration. |
| Other | `-v, --verbose` | Enable verbose output. |
| Other | `-h, --help` | Print command-line help. |

The sections below define accepted values, defaults, and interactions.

## Basic Usage

```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator INPUT_FILE [OPTIONS]
```

`INPUT_FILE` (the input job trace, in CSV format) can also be given via
`-i, --infile FILENAME` instead of as the first positional argument.

## Input/Output Options

### `-i, --infile FILENAME`
Input job trace file. Can also be specified as the first positional argument.

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator --infile traces/jobs.csv
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
${CMAKE_INSTALL_PREFIX}/bin/simulator --infile_list traces/file_list.txt --job_store_capacity 1000
```
where `traces/file_list.txt` contains, one path per line:
```
traces/part1.csv
traces/part2.csv
traces/part3.csv
```

See [`docs/dev/OUTPUT_TRACE_BUFFERS.md`](../dev/OUTPUT_TRACE_BUFFERS.md) for the full design.

### `-o, --outfile FILENAME`
Output file for simulated job trace.

**Default:** Derived from input filename (e.g., `jobs.csv` -> `jobs_sim.csv`)

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv --outfile output/result.csv
```

### `-R, --resource_trace FILENAME`
Write resource usage trace to file.

The generated schemas are defined in [Output Trace Files](output-traces.md).

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv \
    --outfile results/jobs.csv \
    --resource_trace results/resources.csv
```

**Default:** If not specified, resource trace is written to `<outfile>_resources.csv`

## System Configuration

### `-n, --total_nodes COUNT`
Total number of nodes in the simulated cluster.

**Default:** 795

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv --total_nodes 100
```

## Scheduling Policies

### `-b, --backfill_policy POLICY`
Backfilling algorithm to use.

**Options:**
- `easy` - EASY backfilling (default): backfill jobs that complete before FCFS head reservation
- `conservative` - Conservative backfilling. Pair with
  `--priority_policy fcfs_conservative` for the full FCFS
  conservative-reservation implementation.
- `none` - Pure FCFS (no backfilling)

**Default:** `easy`

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv --backfill_policy conservative
```

### `-p, --priority_policy POLICY`
Job priority/ordering policy.

**Options:**
- `fcfs` - First Come First Served (default)
- `fcfs_alt` - Alternative FCFS implementation for differential testing
- `fcfs_conservative` - FCFS implementation that maintains conservative
  reservations; currently uses the `deque` wait queue
- `sjf` - Shortest Job First (by run time estimate)
- `ljf` - Longest Job First (by run time estimate)

**Default:** `fcfs`

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv --priority_policy sjf
```

### `-q, --queue_impl IMPLEMENTATION`
Wait queue implementation (FCFS scheduler only).

**Options:**
- `circular` - circular-buffer implementation (default)
- `deque` - sequential deque implementation
- `multimap` - tree-based alternative FCFS implementation
- `block` - block-based implementation; configured by `--block_size`

**Default:** `circular`

**Note:** This option only affects FCFS scheduler. SJF/LJF always use
`std::multimap` (already efficient for priority-based scheduling). If `deque`,
`block`, or `multimap` is specified with SJF/LJF, a warning is printed and the
default `multimap` is used.

Implementation and benchmark details are in
[Wait Queues](../dev/WAIT_QUEUES.md).

### `-Q, --block_size SIZE`
Block size for the `block` wait-queue implementation. Must be a supported
power of two from `4` through `256`. Only used when `--queue_impl=block`.
See [Block Wait Queue](../dev/BLOCK_WAIT_QUEUE.md) for implementation details.

**Default:** `128`

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/large_10k_jobs.csv --priority_policy fcfs --queue_impl block --block_size 64
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
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv --priority_policy fcfs --queue_impl circular --wait_queue_capacity 1000
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
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv --priority_policy fcfs --queue_impl circular \
    --wait_queue_capacity 500 --wait_queue_overflow abort
```

### `-K, --job_store_capacity SIZE`
Initial capacity of the job-record circular buffer. A single-file input is
loaded in full and grows the store if necessary; use progressive loading when
the capacity must bound storage across a larger trace.

**Default:** `0`, meaning the size of the job trace - large enough that the
store can never overflow, since at most one entry is inserted per job.

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv --job_store_capacity 1000
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
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv \
    --job_store_capacity 500 --job_store_overflow abort
```

### `--job_flush_interval RECORDS`

Write and reclaim the completed contiguous front prefix after this many
additional departures. This batches schedule output and reclamation without
checking and removing a record after every completion. Capacity pressure,
`Trace::flush_completed_jobs()`, and final output are independent flush reasons
and reset the interval.

**Default:** `0`, meaning the current job-store circular-buffer capacity. Thus
the default normally waits until space is needed; set a smaller record count to
spread output I/O through a long streaming run.

### `-m, --check_memory_pressure FRACTION`
Before growing the job-record store for a progressive file or streaming
batch, refuse the operation if projected peak usage would exceed `FRACTION`
of available system memory. This check is independent of
`--job_store_overflow`.

`FRACTION` must be greater than `0.0` and no greater than `1.0`.

On Linux, available memory comes from `/proc/meminfo`'s `MemAvailable`.
The option is a no-op on other platforms and does not account for a
container's effective cgroup limit.

**Default:** disabled

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator --infile_list traces/file_list.txt --check_memory_pressure 0.8
```

See [`docs/dev/OUTPUT_TRACE_BUFFERS.md`](../dev/OUTPUT_TRACE_BUFFERS.md) for the exact formula (mirrors the actual grow-doubling logic, not a fixed multiplier) and rationale.

### `-H, --resource_history_capacity SIZE`
Initial capacity of the resource-history circular buffer. When full, its
finalized records are written to `--resource_trace` and the buffer is cleared.

**Default:** `0`, meaning 2x the number of loaded jobs (large enough it
never needs to reclaim purely to make room) - though never less than
4096, since the loaded count may still be tiny (or 0, early in a
streaming session) at the moment the very first sample is recorded.

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv --resource_trace resources.csv --resource_history_capacity 10000
```

## Trace Format Options

### `--trace_type TYPE`
Select the job/resource data model independently of the input trace format.

**Options:**
- `standard` - Standard DR_EVT job and resource records (default)
- `pcon` - Experimental records carrying `avgpcon`, `minpcon`, and `maxpcon`

**Default:** `standard`

See [Input Trace Files](trace-formats.md) for required input columns and
[Output Trace Files](output-traces.md) for generated columns.

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/pcon.csv \
    --trace_type pcon \
    --trace_format simple \
    --timestamp_format epoch
```

### `-f, --trace_format FORMAT`
Input trace format.

**Options:**
- `simple` - Simple CSV format (minimal columns)
- `lassen` - Lassen HPC format (many metadata columns)

**Default:** `simple`

See [Input Trace Files](trace-formats.md) for both schemas.

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/simple.csv --trace_format simple
```

### `-T, --timestamp_format FORMAT`
Timestamp format in output.

**Options:**
- `epoch` - Unix epoch seconds (e.g., `1693234567.0`)
- `iso` - ISO 8601 format (e.g., `2026-08-29T14:35:00-07:00`)

**Default:** `iso`

See [Input Trace Files](trace-formats.md) for accepted timestamp forms.

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv --timestamp_format epoch
```

### `-z, --timezone TIMEZONE`
Timezone for ISO timestamp output.

**Format:** IANA timezone database name (e.g., `"America/Los_Angeles"`, `"UTC"`, `"America/New_York"`)

**Default:** `America/Los_Angeles`

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv \
    --timestamp_format iso \
    --timezone "America/New_York"
```

### `-M, --msec_output`

Write timestamps in the simulated-job and resource traces with millisecond
precision instead of rounding them to integer seconds.

**Default:** Disabled

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv --msec_output
```

## Simulation Mode Options

### `-r, --run_time_mode MODE`
How to determine the job's actual, observed execution length in simulation mode.

**Options:**
- `actual` - Read the job's actual run time from the input trace (default)
- `distribution` - Sample from statistical distribution (realistic with variation)
- `limit` - Jobs run exactly their time_limit (unrealistic, for debugging only)

**Default:** `actual`

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv --run_time_mode distribution
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
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv \
    --run_time_mode distribution \
    --run_time_distribution lognormal
```

### `-S, --run_time_scale FACTOR`
Scale factor for job run times.

**Range:** > 0.0

**Default:** 1.0 (100% of time_limit)

**Example:** Jobs run 80% of their time_limit on average:
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv \
    --run_time_mode distribution \
    --run_time_scale 0.8
```

### `-V, --run_time_stddev FACTOR`
Standard deviation for run time distribution.

**Range:** >= 0.0

**Default:** 0.0 (no variation)

**Example:** 10% standard deviation:
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv \
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
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv --max_jobs 100
```

### `-t, --max_time TIME`
Maximum simulation time (in trace time units).

**Default:** Unlimited (run until all jobs complete)

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv --max_time 3600.0
```

### `-s, --seed VALUE`
Random number generator seed for reproducibility.

**Default:** System clock

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv --seed 42
```

## Configuration File Option

### `-c, --config CONFIGFILE`
Load parameters from a Protobuf `.textproto` configuration file.

**Requires:** Simulator built with `-DDR_EVT_ENABLE_PROTOBUF=ON`

Options are applied in command-line order. When `--config` is encountered,
the config file is loaded at that point; command-line arguments appearing
after `--config` override the corresponding config values.

**Example:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv \
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
${CMAKE_INSTALL_PREFIX}/bin/simulator traces/jobs.csv --verbose
```

### `-h, --help`
Display help message with all options.

```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator --help
```

## See Also

- [User Guide Overview](overview.md) - User guide navigation
- [Input Trace Files](trace-formats.md) - input schemas and mode selection
- [Output Trace Files](output-traces.md) - output schemas and statistics
- [Protobuf Configuration](protobuf-config.md) - Full `.textproto` schema and worked examples
- [Streaming API](../api/STREAMING_API.md) - Programmatic C++ API for online simulation
- [Backfilling Algorithms](../BACKFILLING_ALGORITHMS.md) - EASY and CONSERVATIVE algorithm details
- [Quick Start](../getting-started/quickstart.md) - Quick reference
- [Testing Guide](../TESTING_GUIDE.md) - Running tests and validation
- [Test Suite](https://github.com/LLNL/dr_evt/blob/main/tests/README.md#build-and-run) - example usage in test scripts
- [Block Queue Implementation](../dev/design-decisions/BLOCK_QUEUE.md) - Performance analysis of `--queue_impl block`
- [Circular Queue Implementation](../dev/design-decisions/CIRCULAR_QUEUE.md) - Performance analysis of `--queue_impl circular`
