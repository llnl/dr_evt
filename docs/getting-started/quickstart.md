# Quick Start Guide - DR_EVT Backfilling Scheduler

## Overview

DR_EVT includes a **SLURM-style backfilling scheduler**, enabling realistic job scheduling simulation.

## Features

- **EASY Backfilling**: First job gets reservation, others backfill if they don't delay it
- **Conservative Backfilling**: All jobs get reservations
- **Priority Policies**: FCFS (First-Come-First-Served), an alternative FCFS implementation (for testing), FCFS with conservative/no backfilling support, SJF (Shortest-Job-First), LJF (Longest-Job-First)
- **Run Time Modes**: Read from trace (actual), sample from distribution, or use time limits

## Build Instructions

### Prerequisites
- Python 3.9+
- C++ compiler with C++17 support
- No admin access required

### Setup (One-Time)

```bash
# 1. Create virtual environment
python3 -m venv venv
source venv/bin/activate

# 2. Install build tools
pip install --upgrade pip cmake

# 3. Configure and build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4

# 4. Verify
${CMAKE_INSTALL_PREFIX}/bin/simulator --help
```

### Dependencies

Both Boost and Protobuf are found via `find_package` first, falling back
to CMake `FetchContent` (downloading and building from source, no
root/sudo needed) if not found on the system - Boost takes ~10-15
minutes this way, Protobuf ~5-10 minutes. Subsequent builds are fast.
Protobuf is only needed at all if you enable it explicitly (see below) -
the plain build shown above does not require or fetch it.

- **Boost**: `find_package` first; `FetchContent` fallback if not found
- **Protobuf**: only relevant with `-DDR_EVT_ENABLE_PROTOBUF=ON` (needed for `--config` files and the gRPC client/server); `find_package` first, `FetchContent` fallback if not found - see [Protobuf Configuration](../user-guide/protobuf-config.md) and [Client/Server Setup](../user-guide/grpc-setup.md)

## Usage

### Basic Example
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator trace_file.txt \
  --total_nodes 795 \
  --backfill_policy easy \
  --priority_policy fcfs \
```

### All Options

This is a quick summary; see [Command-Line Reference](../user-guide/command-line.md)
for the full description of every option. DR_EVT also supports prototext-based
configuration files (see [Protobuf Configuration](../user-guide/protobuf-config.md)).

```
Input/Output:
  -i, --infile <file>          Input trace file
  -L, --infile_list <file>     File listing multiple trace files - progressive loading (mutually exclusive with --infile)
  -o, --outfile <file>         Output file for results
  -R, --resource_trace <file>  Write resource usage trace to file
  -H, --resource_history_capacity <size>  Initial resource-history buffer capacity (default: 0 = 2x loaded jobs, floored at 4096)

Simulation setup:
  -n, --total_nodes <N>        Number of nodes in system (default: 795)
  -j, --max_jobs <N>           Maximum number of jobs to simulate
  -t, --max_time <T>           Maximum simulation time
  -s, --seed <N>                Random number seed

Scheduling policy:
  -b, --backfill_policy <policy>   easy|conservative|none (default: easy)
  -p, --priority_policy <policy>   fcfs|fcfs_alt|fcfs_conservative|sjf|ljf (default: fcfs)
  -q, --queue_impl <impl>          circular|deque|multimap|block (default: circular)
  -Q, --block_size <size>          Block size when queue_impl=block (default: 128)
  -A, --wait_queue_capacity <size>   Initial wait queue capacity when queue_impl=circular
  -G, --wait_queue_overflow <mode>   abort|grow when queue_impl=circular (default: grow)

Job store:
  -K, --job_store_capacity <size>      Initial job-record store capacity (default: 0 = size of trace)
  -W, --job_store_overflow <mode>      abort|grow when job_store_capacity exceeded (default: grow)
  -m, --check_memory_pressure <fraction>  Refuse to grow the job store past this fraction of available memory (0 < fraction <= 1; disabled unless given)

Trace format:
  -f, --trace_format <fmt>     simple|lassen (default: simple)
  -T, --timestamp_format <fmt> epoch|iso (default: iso)
  -z, --timezone <tz>          Timezone for iso timestamps (default: America/Los_Angeles)

Duration/run time modeling:
                                       planning estimate
  -r, --run_time_mode <mode>          actual|distribution|limit (default: actual)
                                       how the job's actual run time is determined
  -D, --run_time_distribution <type>  normal|lognormal|uniform (default: normal)
  -S, --run_time_scale <factor>       Scale factor for run time sampling (default: 1.0)
  -V, --run_time_stddev <factor>      Std deviation factor for run time sampling

Config file (requires protobuf support):
  -c, --config <file>          Read simulation parameters from a protobuf config file
                               (see [Protobuf Configuration](../user-guide/protobuf-config.md))

Other:
  -v, --verbose                 Enable verbose output
  -M, --msec_output             Millisecond-precision timestamps in output
  -h, --help                    Display usage information
```

### Examples

**Conservative backfilling with shortest-job-first:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator trace.txt \
  --backfill_policy conservative \
  --priority_policy sjf \
  --outfile results_conservative_sjf.txt
```

**Using actual run times from trace:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator trace.txt \
  --run_time_mode actual \
  --outfile results_actual.txt
```

**Limited simulation (first 1000 jobs):**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator trace.txt \
  --max_jobs 1000 \
  --outfile results_subset.txt
```

## Output

The simulator outputs:
- Job statistics (submissions, completions)
- Average wait time
- Average turnaround time
- Makespan (total time to complete all jobs)
- Per-job results (submit, start, end times)

## Trace File Format

Input trace files are CSV, with columns looked up by name in the header
row (any order works) - not fixed-position, and not tab-separated.

**Simulation mode** (scheduler computes start/end times - the common case):
```text
job_submit_time,num_nodes,queue,time_limit
0,10,pbatch,100
50,10,pbatch,50
```

**Replay mode** (`begin_time`/`end_time` already known, replayed exactly):
```text
job_submit_time,begin_time,end_time,num_nodes,queue,time_limit
0,0,100,10,pbatch,100
50,100,150,10,pbatch,50
```

`time_limit` is also accepted under the column names `timelimit` or
`walltime`, so an existing trace can be reused without editing its header.
Only `pbatch`/`pall` (and `pbatch0`-`pbatch3`) queue values are accepted
by default.

See [Trace File Formats](../user-guide/trace-formats.md) for the full
column reference (including the Lassen format and the `lassen` format
option) and [Simulation vs Replay Modes](../dev/design-decisions/SIMULATION_VS_REPLAY_MODES.md)
for how the parser picks a mode.

## Understanding the Algorithms

### EASY Backfilling
- **How it works**: The first job in the queue gets a guaranteed start time (reservation)
- **Backfilling**: Smaller jobs can "jump the queue" if they finish before the first job's reservation
- **Best for**: Mixed workloads with varying job sizes
- **Tradeoff**: Simple but may delay some jobs unnecessarily

### Conservative Backfilling
- **How it works**: ALL queued jobs get reservations
- **Backfilling**: Jobs can only backfill if they don't delay ANY reservation
- **Best for**: Fairness - prevents starvation
- **Tradeoff**: More conservative, may leave resources idle

### Priority Policies

**FCFS (First-Come-First-Served)**
- Jobs scheduled in submission order
- Traditional fair scheduling
- Default policy

**SJF (Shortest-Job-First)**
- Shorter jobs scheduled first
- Minimizes average wait time
- May starve long jobs

**LJF (Longest-Job-First)**
- Longer jobs scheduled first
- Useful for throughput optimization
- May starve short jobs

### Run Time Modes

**How jobs actually run (run_time_mode):**

- `actual` (default) - Read actual run time from trace (most realistic)
- `distribution` - Sample from statistical distribution
- `limit` - Run for exactly time_limit (debug mode)

**Note:** Scheduler uses time_limit as the best estimator for planning
- Upper bound on performance

## Troubleshooting

### CMake can't find Boost
```bash
# On macOS with Homebrew
cmake .. -DBOOST_ROOT=/opt/homebrew/opt/boost

# Or set environment variable
export BOOST_ROOT=/path/to/boost
cmake .. -DCMAKE_BUILD_TYPE=Release
```

### Build fails during Protobuf download
- Check internet connection
- Or download protobuf manually and use:
```bash
cmake .. -DPROTOBUF_ROOT=/path/to/protobuf
```

### "No such file or directory" errors
Make sure you're in the build directory:
```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

## Development

### Running Tests

There's no `ctest` integration - tests run as shell scripts against the
built binaries instead:

```bash
# From the repo root, after building (and installing, or with
# CMAKE_INSTALL_PREFIX pointed at wherever `make install` put things)
./tests/test_all_dr_evt.sh          # comprehensive scheduler tests
./tests/run_append_job_tests.sh     # streaming API (append_job/append_jobs)
./tests/run_progressive_load_tests.sh  # --infile_list progressive loading
```

See the [Testing Guide](../TESTING_GUIDE.md) for the full list of test
scripts and what each covers.

### Rebuilding After Code Changes
```bash
cd build
make -j4  # Only rebuilds changed files
```

### Clean Build
```bash
rm -rf build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

## Documentation

- **[Installation Guide](installation.md)**: Detailed build instructions
- **[Tutorial](tutorial.md)**: Step-by-step first simulation
- **[User Guide](../user-guide/overview.md)**: Complete usage manual
- **[Testing Guide](../TESTING_GUIDE.md)**: Test suite and validation

## Citation

If you use this scheduler implementation in your research, please cite:
```bibtex
@software{dr_evt_scheduler,
  title = {DR\_EVT: distributed discrete resource event simulation},
  author = {Jae-Seung Yeom},
  year = {2026},
  url = {https://github.com/llnl/dr_evt}
}
```

## License

MIT License - See LICENSE file for details

## Support

For issues or questions:
1. Check [Installation Guide](installation.md) for build problems
2. Review [Testing Guide](../TESTING_GUIDE.md) for test suite details
3. Read [User Guide](../user-guide/overview.md) for usage questions
4. Open an issue on GitHub

## What's Next?

Planned features:
- Checkpoint/restart capability for long-running simulations
