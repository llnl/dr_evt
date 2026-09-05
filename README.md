# Discrete Resource Event Modeling and Multi-cluster Scheduling Simulator

[![Documentation Status](https://readthedocs.org/projects/dr-evt/badge/?version=latest)](https://dr-evt.readthedocs.io/en/latest/?badge=latest)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](https://github.com/LLNL/dr_evt/blob/main/LICENSE)

DR_EVT is a high-performance HPC job scheduler simulator supporting EASY and
CONSERVATIVE backfilling algorithms. **Uniquely supports online simulation via
gRPC**, enabling coordinated multi-cluster simulations where distributed schedulers
interact in real-time.

**[📚 Read the Full Documentation on ReadTheDocs →](https://dr-evt.readthedocs.io/)**

## Features

### Core Simulation
- **Backfilling Algorithms**: EASY and CONSERVATIVE backfilling (fully implemented)
  - EASY: O(n) complexity, optimizes utilization (~95%)
  - CONSERVATIVE: O(n²) complexity, guarantees fairness to all waiting jobs
  - Both verified against independent Python reference implementations
- **Priority Policies**: FCFS, SJF, LJF with multiple wait-queue implementations
- **Queue Implementations**: Circular buffer (default), deque, multimap, block-based (7 sizes: 4-256)
- **Replay and Simulation Modes**: Replay historical traces or simulate with run time distributions
- **Early Completion Support**: Jobs can finish before time_limit (actual_run_time < time_limit)

### APIs & Integration
- **Streaming API**: Online simulation with dynamic job submission (`submit_job`, `advance_to`, `run_until_exclusive`)
- **gRPC Service**: Network-exposed streaming API enabling:
  - **Multi-cluster coordination**: Distributed schedulers interact in real-time
  - Remote simulation control from any gRPC-capable language
  - MPI-based multi-client/multi-server test harness for coordinated simulations
- **Python Bindings**: Full Python API for in-process simulation control
- **Protocol Buffer Configuration**: Structured configuration files for complex simulations

### Testing & Validation
- **Comprehensive Test Suite**: 57 tests (57/57 passing as of 2026-09-03)
  - 34 comprehensive tests (EASY backfilling correctness)
  - 7 unit tests (I/O and format validation)
  - 5 feature tests (policy comparisons)
  - 2 conservative tests (CONSERVATIVE vs EASY behavioral differences)
  - 6 scale tests (10-2000 jobs)
  - 3 replay tests (determinism verification)
- **Dual Validation**: C++ verified against independent Python reference implementations
  - EASY: scripts/python_reference_scheduler.py
  - CONSERVATIVE: scripts/python_conservative_scheduler.py
  - 0 mismatches on all test traces
- **Differential Testing**: Compare multiple queue implementations (circular/deque/multimap/block)
- **CI/CD Integration**: Automated testing on every commit via GitHub Actions

## Documentation

**Primary Documentation**: [**ReadTheDocs** (https://dr-evt.readthedocs.io/)](https://dr-evt.readthedocs.io/)
- Searchable, versioned documentation with PDF/EPUB downloads
- Automatic builds from `main` branch
- Mobile-friendly with navigation

### Quick Reference (GitHub)
For quick access without leaving GitHub:
- **[Complete Documentation Index](docs/)** - All guides, references, and specifications
- **[Backfilling Algorithms](docs/BACKFILLING_ALGORITHMS.md)** - EASY and CONSERVATIVE algorithm specifications
- **[Streaming API](docs/api/STREAMING_API.md)** - Online simulation API
- **[Client/Server (gRPC)](docs/CLIENT_SERVER_GUIDE.md)** - Remote simulation over network
- **[Python API](docs/api/PYTHON_API.md)** - Python bindings and reference implementation
- **[CLI Options](docs/user-guide/command-line.md)** - Command-line reference
- **[Testing Guide](docs/TESTING_GUIDE.md)** - Test philosophy, organization, and test suite details
- **[Test Suite](tests/README.md)** - All tests and validation
- **[Comprehensive Tests](tests/test_traces/comprehensive/README.md)** - 34 tests organized by complexity
- **[Scripts Guide](scripts/README.md)** - Testing and verification scripts

**Note:** Local builds are optional for contributors. The official documentation is automatically built and published to ReadTheDocs on every commit to `main`.


### Building Documentation Locally

Build the same Sphinx documentation that powers ReadTheDocs:

```bash
cd docs

# Install dependencies (one-time setup)
make install
# or manually: pip install -r requirements.txt

# Build HTML documentation
make html
# Output: docs/_build/html/index.html

# Build PDF documentation (requires LaTeX)
make pdf
# Output: docs/_build/latex/DR_EVT.pdf

# Serve with live-reload (for development)
make serve
# Opens browser at http://localhost:8000

# Check for broken links
make linkcheck
```


## Requirements

### Required
 + **Platforms**: Linux-based systems (macOS may work but not officially supported)
 + **C++ compiler**: C++17 support (GCC 7+, Clang 5+, or newer)
 + **CMake**: 3.24 or later
 + **Boost**: Components required: `regex`, `filesystem`, `system`, `program_options`, `serialization`, `container`, `multi_index`, `circular_buffer`
   - Tested with Boost 1.70+
   - Install: `apt-get install libboost-all-dev` (Ubuntu/Debian) or `brew install boost` (macOS)
 + [**Protocol Buffers**](https://developers.google.com/protocol-buffers): Auto-downloaded if not found, or use `-DPROTOBUF_ROOT=<path>`

### Optional (for full features)

**Python 3.7+**: For Python bindings (`-DDR_EVT_BUILD_PYTHON=ON`)
- Python development headers required: `apt-get install python3-dev`
- pybind11 auto-downloaded via FetchContent if not found

**[gRPC](https://grpc.io/)**: For online simulation service (`-DDR_EVT_ENABLE_GRPC=ON`)
- **Auto-download**: If not found, gRPC (with bundled Protobuf) is auto-downloaded via FetchContent (~5-10 min first build). Can OOM under full parallelism on memory-constrained machines - see "Livermore Computing (LC) HPC systems" below.
- **Manual install**: `apt-get install libgrpc++-dev protobuf-compiler-grpc` (Ubuntu/Debian)
- **Important**: gRPC includes its own Protobuf. If gRPC is enabled, you don't need separate Protobuf install.

**MPI**: For multi-client/server test harness only (optional even with gRPC)
- Install: `apt-get install libopenmpi-dev openmpi-bin`

### Protocol Buffers & gRPC Details

**Protobuf usage:** Configuration file parsing ([proto3 syntax](https://developers.google.com/protocol-buffers/docs/proto3))
- Enabled by default (`-DDR_EVT_ENABLE_PROTOBUF=ON`)
- If gRPC is enabled, Protobuf comes bundled with gRPC (no separate install needed)
- If gRPC is **not** enabled, standalone Protobuf is auto-downloaded via FetchContent if not found

**Key relationship:**
```
gRPC build → includes Protobuf (bundled)
Protobuf-only build → standalone Protobuf installation
```

### CMake Configuration Options

**Boost:**
```bash
cmake .. -Wno-author -Wno-dev -DBOOST_ROOT=/path/to/boost
# or use environment variable
export BOOST_ROOT=/path/to/boost

# Skip system paths (useful if system install is broken or mismatched by version)
cmake .. -Wno-author -Wno-dev -DAVOID_SYSTEM_BOOST=ON
```

**Protobuf (standalone, when gRPC not used):**
```bash
cmake .. -Wno-author -Wno-dev -DPROTOBUF_ROOT=/path/to/protobuf

# Skip system paths (useful if system install is broken or mismatched by version)
cmake .. -Wno-author -Wno-dev -DDR_EVT_ENABLE_PROTOBUF=ON -DAVOID_SYSTEM_PROTOBUF=ON
```

**gRPC:**
```bash
# Enable gRPC support (auto-enables Protobuf)
cmake .. -Wno-author -Wno-dev -DDR_EVT_ENABLE_GRPC=ON

# Skip system path search (useful if system install is broken or mismatched by version)
cmake .. -Wno-author -Wno-dev -DDR_EVT_ENABLE_GRPC=ON -DAVOID_SYSTEM_GRPC=ON

# Clear FetchContent cache to retry system search
cmake -U DR_EVT_GRPC_FETCHCONTENT ..
```
`-DAVOID_SYSTEM_GRPC=ON` guarantees the from-source FetchContent build - see the OOM note under "Livermore Computing (LC) HPC systems" below.

**Python bindings:**
```bash
cmake .. -Wno-author -Wno-dev -DDR_EVT_BUILD_PYTHON=ON

# Specify Python interpreter (useful with virtual environments)
cmake .. -Wno-author -Wno-dev -DDR_EVT_BUILD_PYTHON=ON -DPython3_EXECUTABLE=/path/to/python3
```

**Cross-compilation (Protobuf):**
```bash
cmake .. -Wno-author -Wno-dev \
  -DProtobuf_PROTOC_EXECUTABLE=/host/bin/protoc \
  -DPROTOBUF_DIR=/target/protobuf
```

**Livermore Computing (LC) HPC systems:**
```bash
mkdir build && cd build
cmake .. \
  -DDR_EVT_ENABLE_GRPC=ON \
  -DDR_EVT_BUILD_PYTHON=ON \
  -DAVOID_SYSTEM_GRPC=ON \
  -DAVOID_SYSTEM_BOOST=ON \
  -DCMAKE_INSTALL_PREFIX=$(realpath ../install)
make -j4
make install

# Set up environment
export CMAKE_INSTALL_PREFIX=$(realpath ../install)
export PATH=${CMAKE_INSTALL_PREFIX}/bin:$PATH
export PYTHONPATH=${CMAKE_INSTALL_PREFIX}/lib/python:$PYTHONPATH
```

The `AVOID_SYSTEM_*` options prevent ABI mismatches with system-installed libraries (common on HPC systems with multiple compiler toolchains), but force gRPC/BoringSSL/Protobuf and Boost to build from source via FetchContent. That from-source build can OOM on memory-constrained nodes under full parallelism, with output like:

```
make[2]: *** [.../boringssl_gtest.dir/build.make:90: .../gtest-all.cc.o] Killed
make[1]: *** [CMakeFiles/Makefile2:12579: .../boringssl_gtest.dir/all] Error 2
```

`Killed` means memory pressure, not a compiler error. `-j4` above is deliberately conservative for this reason (~2 GB/job is a reasonable estimate for gRPC); lower it further if you still hit this.
When using pre-built gRPC, `make -j$(nproc)` should still be ok.

## Quick Start

### Build from Source

```bash
# Clone repository
git clone https://github.com/llnl/dr_evt.git
cd dr_evt

# Create build directory
mkdir build && cd build

# Configure (first run builds Protocol Buffers)
cmake -DBOOST_ROOT=/path/to/boost \
      -DCMAKE_INSTALL_PREFIX=/install/path \
      ..

# Build Protocol Buffers dependency
make -j4

# Configure DR_EVT (second run builds the simulator)
cmake -DBOOST_ROOT=/path/to/boost \
      -DCMAKE_INSTALL_PREFIX=/install/path \
      ..

# Build and install
make -j4
make install
```

### Run Your First Simulation

After installation, the binaries are in `${CMAKE_INSTALL_PREFIX}/bin`:

**Quick test:**
```bash
cd ${CMAKE_INSTALL_PREFIX}/bin

# Run a simple test
./simulator /path/to/dr_evt/tests/test_traces/unit/simple.csv

# Run with EASY backfilling (default)
./simulator trace.csv --priority_policy fcfs --backfill_policy easy

# Run with custom parameters
./simulator trace.csv \
    --priority_policy fcfs \
    --backfill_policy easy \
    --total_nodes 100 \
    --outfile results.csv
```

**Using Protocol Buffer configuration files:**

Create a configuration file `sim_config.textproto`:
```protobuf
simulation_params {
  infile: "trace.csv"
  outfile: "results.csv"
  resource_trace: "resources.csv"

  # System configuration
  total_nodes: 1000
  seed: 42

  # Scheduling policies
  backfill_policy: "easy"     # "easy", "conservative", or "none"
  priority_policy: "fcfs"     # "fcfs", "sjf", or "ljf"

  # Trace format
  trace_format: "simple"      # "simple" or "lassen"
  timestamp_format: "epoch"   # "epoch" or "iso"

  # Simulation limits
  max_jobs: 10000

  # Run time simulation (optional)
  run_time_mode: "actual"     # "actual" (default), "distribution", or "limit"
  run_time_scale: 1.0

  # Output options
  verbose: false
}
```

Run with configuration file:
```bash
# Use protobuf config (overrides command-line defaults)
${CMAKE_INSTALL_PREFIX}/bin/simulator --config sim_config.textproto trace.csv

# Command-line options override config file values
${CMAKE_INSTALL_PREFIX}/bin/simulator --config sim_config.textproto \
    --total_nodes 2000 \
    --verbose \
    trace.csv
```

**Configuration precedence:**
1. Command-line options (highest priority)
2. Protocol Buffer config file (`--config`)
3. Built-in defaults (lowest priority)

### Simulation Modes

DR_EVT supports two top-level modes for processing job traces: **Simulation
Mode**, where the scheduler makes real decisions, and **Replay Mode**, where
the scheduler is bypassed.

#### Simulation Mode

**Purpose:** Simulate scheduling decisions with realistic scheduler knowledge

**Input:** Trace with job submissions and time limits
- Requires: `submit_time`, `time_limit`, job size
- Scheduler plans based on time limits as the estimation of job duration (realistic but limited)
- Each job's actual duration is then determined by `--run_time_mode`:
  - `actual` (default): use the trace's real duration (accepted columns:
    `actual_run_time`, `duration`, `actual_duration`, `run_time`)
  - `distribution`: sample from a statistical distribution
  - `limit`: job runs exactly to its `time_limit` (no early completion —
    an upper bound on scheduler performance, since the scheduler's estimate
    is never wrong)

**Use cases:**
- Standard HPC scheduling simulation; compare scheduling policies
- What-if analysis: "What if we changed the backfill policy?"
- Capacity planning: "Can we handle 20% more jobs?"
- Theoretical best-case performance analysis (`limit`, since the scheduler's
  plan always matches reality)

**Examples:**
```bash
# Use the trace's own actual, historical run times (default)
${CMAKE_INSTALL_PREFIX}/bin/simulator trace.csv \
    --run_time_mode actual \
    --backfill_policy easy

# Realistic variation (80% of time_limit ± 10%)
${CMAKE_INSTALL_PREFIX}/bin/simulator trace.csv \
    --run_time_mode distribution \
    --run_time_distribution normal \
    --run_time_scale 0.8 \
    --run_time_stddev 0.1

# Upper bound: jobs run exactly their time_limit
${CMAKE_INSTALL_PREFIX}/bin/simulator trace.csv \
    --run_time_mode limit
```

#### Replay Mode

**Purpose:** Reproduce a known execution's resource usage from historical or
pre-computed job times, with no scheduler involved

**Tool:** `tracer` (installed as `${CMAKE_INSTALL_PREFIX}/bin/tracer`) - a
separate binary from `simulator`, with no scheduler code linked in at all.
Feeding a `begin_time`/`end_time` file into `simulator` instead does **not**
bypass its scheduler - `simulator` always calls into the same FCFS/backfill
logic and computes its own start times, discarding any recorded `begin_time`.
Only `tracer` honors the file's own times directly.

**Input:** Trace with pre-computed schedule
- Requires: `num_nodes`, `begin_time`, `end_time`, `job_submit_time`, `queue`,
  `time_limit` - all six are required columns. `job_submit_time` isn't used
  to decide when a job runs (that's `begin_time`), but it drives a per-job
  "nodes busy at submission" stat for downstream analysis/visualization, and
  there's no shorter format that omits it.
- No scheduler is consulted - `begin_time`/`end_time` are used as-is
- Can replay simulation output or historical HPC logs

**Use cases:**
- Generate resource usage traces from historical schedules
- Validate resource accounting correctness
- Reproduce execution for debugging/visualization
- Analyze utilization of past system behavior

**Example:**
```bash
# Step 1: Run simulation
${CMAKE_INSTALL_PREFIX}/bin/simulator input.csv \
    --total_nodes 100 --outfile schedule.csv --resource_trace sim.csv

# Step 2: Replay the schedule with tracer - no scheduler-related flags exist for it
${CMAKE_INSTALL_PREFIX}/bin/tracer --infile schedule.csv \
    --total_nodes 100 --resource_trace replay.csv \
    --outfile tracer_out.csv --subfile tracer_sub.csv --subsumf tracer_subsum.csv

# Step 3: Verify (should be identical)
diff sim.csv replay.csv
```

**Key difference:**
- **Simulation Mode** (`simulator`): the scheduler makes real decisions as
  jobs are submitted; `--run_time_mode` controls how each job's actual
  duration is determined
- **Replay** (`tracer`): no scheduler is linked in; a previously computed
  schedule (`begin_time`/`end_time`) is read directly and run straight
  through resource accounting

**When to use which:**
| Scenario | Mode | Why |
|----------|------|-----|
| Test scheduler or policy changes | Simulation | Scheduler must make real decisions to see the effect |
| Capacity planning | Simulation | Need the scheduler in the loop under hypothetical load |
| Reproduce a specific historical schedule | Replay | Bypasses the scheduler; replays known begin/end times exactly |
| Validate resource-accounting correctness | Replay | Compare against a known-correct trace |

### Verify Installation

```bash
# Run comprehensive test suite
./tests/test_all_dr_evt.sh

# Run quick unit tests
./tests/run_unit_tests.sh
```

### Build Options

**Enable OpenMP for parallel execution:**
```bash
cmake -DDR_EVT_WITH_OPENMP=ON \
      -DBOOST_ROOT=/path/to/boost \
      ..
```

Then control parallelism with environment variables:
```bash
export OMP_NUM_THREADS=4
export OMP_PROC_BIND=close
${CMAKE_INSTALL_PREFIX}/bin/simulator trace.csv
```

**Enable gRPC client/server (optional):**
```bash
cmake -DDR_EVT_ENABLE_PROTOBUF=ON \
      -DDR_EVT_ENABLE_GRPC=ON \
      -DBOOST_ROOT=/path/to/boost \
      ..
make -j4
make dr_evt_server-bin dr_evt_client-bin
```

**Cross-compilation:**
```bash
cmake -DProtobuf_PROTOC_EXECUTABLE=/host/bin/protoc \
      -DPROTOBUF_DIR=/target/protobuf \
      -DBOOST_ROOT=/path/to/boost \
      ..
```

### gRPC Client/Server Mode (Optional)

DR_EVT runs as a network service, enabling **coordinated multi-cluster simulations**
in a distributed fashion and digital-twin scheduler interacting in real-time, as well as remote simulation
control from any gRPC-capable language.

**Start the server:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_server --port 50051
```

**Run a client:**
```bash
# Basic usage
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_client --server localhost:50051 \
    --trace trace.csv \
    --total_nodes 1000

# With custom parameters
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_client --server localhost:50051 \
    --trace trace.csv \
    --total_nodes 1000 \
    --backfill_policy conservative \
    --priority_policy fcfs \
    --outfile results.csv
```

**Use cases:**
- **Multi-cluster coordination**: Simulate distributed schedulers coordinating across clusters
- **Remote simulation**: Run simulator on HPC cluster, control from laptop
- **Multi-language integration**: Use Python/Java/Go clients with C++ simulator
- **Distributed testing**: Multiple clients testing different scenarios simultaneously
- **Web dashboards**: Real-time simulation monitoring over HTTP/gRPC

**Architecture:**
```
┌─────────────┐                  ┌──────────────┐
│   Client    │  gRPC Stream     │    Server    │
│ (Any Lang)  │ ←──────────────→ │  (C++ Core)  │
└─────────────┘                  └──────────────┘
     │                                   │
     │ submit_job()                      │ Simulation
     │ advance_to()                      │ Instance
     │ get_statistics()                  │
     └───────────────────────────────────┘
```

See [Client/Server Guide](docs/CLIENT_SERVER_GUIDE.md) for details.


## Authors:
  Many thanks go to DR_EVT's [contributors](https://github.com/llnl/dr_evt/graphs/contributors).

## Release:
 DR_EVT is distributed under the terms of the MIT license.
 All new contributions must be made under this license.
 See [LICENSE](https://github.com/llnl/wcs/blob/master/LICENSE) and [NOTICE](https://github.com/llnl/wcs/blob/master/NOTICE) for details.

 + `SPDX-License-Identifier: MIT`
 + `LLNL-CODE-844050`

## Contributing:
 Please submit any bugfixes or feature improvements as [pull requests](https://help.github.com/en/github/collaborating-with-issues-and-pull-requests/creating-a-pull-request-from-a-fork).
