# Quick Start Guide - DR_EVT Backfilling Scheduler

## What's New

This branch (`feature/backfilling-scheduler`) adds a **SLURM-style backfilling scheduler** to DR_EVT, enabling realistic job scheduling simulation.

## Features

- **EASY Backfilling**: First job gets reservation, others backfill if they don't delay it
- **Conservative Backfilling**: All jobs get reservations
- **Priority Policies**: FCFS (First-Come-First-Served), SJF (Shortest-Job-First), LJF (Longest-Job-First)
- **Run Time Modes**: Realistic (use time limits) or Oracle (perfect knowledge)

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
./simulator --help
```

**Note**: First build downloads and compiles Protobuf (~5-10 minutes). Subsequent builds are fast.

### Dependencies
All dependencies are automatically downloaded via CMake FetchContent:
- **Protobuf 3.21.12**: Auto-downloaded and built
- **Boost**: Uses system installation (required separately)

## Usage

### Basic Example
```bash
./simulator trace_file.txt \
  --total_nodes 795 \
  --backfill_policy easy \
  --priority_policy fcfs \
  --duration_mode limit
```

### All Options

```
--total_nodes <N>           Number of nodes in system (default: 795)
--backfill_policy <policy>  Backfilling algorithm: easy|conservative
--priority_policy <policy>  Job ordering: fcfs|sjf|ljf
--duration_mode <mode>       Run time estimates: limit|actual
--max_jobs <N>              Maximum number of jobs to simulate
--max_time <T>              Maximum simulation time
--outfile <file>            Output file for results
--seed <N>                  Random number seed
```

### Examples

**Conservative backfilling with shortest-job-first:**
```bash
./simulator trace.txt \
  --backfill_policy conservative \
  --priority_policy sjf \
  --outfile results_conservative_sjf.txt
```

**Oracle mode (perfect run time knowledge):**
```bash
./simulator trace.txt \
  --duration_mode actual \
  --outfile results_oracle.txt
```

**Limited simulation (first 1000 jobs):**
```bash
./simulator trace.txt \
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

Input trace files should have tab-separated columns:
```
num_nodes  begin_time  end_time  submit_time  queue  time_limit
```
`time_limit` is also accepted under the column names `timelimit` or
`walltime`, so an existing trace can be reused without editing its header.

See existing trace files in the project for examples.

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

**USE_LIMIT (Realistic)**
- Scheduler uses user-provided time limits
- Jobs may finish earlier than estimated
- Mimics real HPC systems

**USE_ACTUAL (Oracle)**
- Scheduler knows exact run times
- Unrealistic but useful for comparison
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
```bash
cd build
ctest
```

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
  title = {DR\_EVT SLURM-Style Backfilling Scheduler},
  author = {Your Name},
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
- Python bindings for easier integration
- Additional scheduling policies
- Real-time visualization
- Multi-queue support
- Checkpoint/restart capability
