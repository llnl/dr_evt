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
- **[Streaming API](docs/STREAMING_API.md)** - Online simulation API
- **[Client/Server (gRPC)](docs/CLIENT_SERVER_GUIDE.md)** - Remote simulation over network
- **[Python API](docs/PYTHON_API.md)** - Python bindings and reference implementation
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
- **Auto-download**: If not found, gRPC (with bundled Protobuf) is auto-downloaded via FetchContent (~5-10 min first build)
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
cmake .. -Wno-author -DBOOST_ROOT=/path/to/boost
# or use environment variable
export BOOST_ROOT=/path/to/boost

# Skip system paths (useful if system install is broken or mismatched by version)
cmake .. -Wno-author -DAVOID_SYSTEM_BOOST=ON
```

**Protobuf (standalone, when gRPC not used):**
```bash
cmake .. -Wno-author -DPROTOBUF_ROOT=/path/to/protobuf

# Skip system paths (useful if system install is broken or mismatched by version)
cmake .. -Wno-author -DDR_EVT_ENABLE_PROTOBUF=ON -DAVOID_SYSTEM_PROTOBUF=ON
```

**gRPC:**
```bash
# Enable gRPC support (auto-enables Protobuf)
cmake .. -Wno-author -DDR_EVT_ENABLE_GRPC=ON

# Skip system path search (useful if system install is broken or mismatched by version)
cmake .. -Wno-author -DDR_EVT_ENABLE_GRPC=ON -DAVOID_SYSTEM_GRPC=ON

# Clear FetchContent cache to retry system search
cmake -U DR_EVT_GRPC_FETCHCONTENT ..
```

**Python bindings:**
```bash
cmake .. -Wno-author -DDR_EVT_BUILD_PYTHON=ON

# Specify Python interpreter (useful with virtual environments)
cmake .. -Wno-author -DDR_EVT_BUILD_PYTHON=ON -DPython3_EXECUTABLE=/path/to/python3
```

**Cross-compilation (Protobuf):**
```bash
cmake .. -Wno-author \
  -DProtobuf_PROTOC_EXECUTABLE=/host/bin/protoc \
  -DPROTOBUF_DIR=/target/protobuf
```


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

**Quick test:**
```bash
# Run a simple test
./simulator ../tests/test_traces/unit/simple.csv

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
  duration_mode: "limit"       # "limit" or "actual"

  # Trace format
  trace_format: "simple"      # "simple" or "lassen"
  timestamp_format: "epoch"   # "epoch" or "iso"

  # Simulation limits
  max_jobs: 10000

  # Run time simulation (optional)
  run_time_mode: "exact"      # "column", "exact", or "distribution"
  run_time_scale: 1.0

  # Output options
  verbose: false
}
```

Run with configuration file:
```bash
# Use protobuf config (overrides command-line defaults)
./simulator --config sim_config.textproto trace.csv

# Command-line options override config file values
./simulator --config sim_config.textproto \
    --total_nodes 2000 \
    --verbose \
    trace.csv
```

**Configuration precedence:**
1. Command-line options (highest priority)
2. Protocol Buffer config file (`--config`)
3. Built-in defaults (lowest priority)

### Simulation Modes

DR_EVT supports different modes for processing job traces:

#### Simulation Mode (`--duration_mode limit`, default)

**Purpose:** Simulate scheduling decisions with realistic scheduler knowledge

**Input:** Trace with job submissions and time limits
- Requires: `submit_time`, `time_limit`, job size
- Scheduler plans using time limits (realistic)
- Jobs run according to `--run_time_mode` setting

**Use cases:**
- Standard HPC scheduling simulation
- Compare scheduling policies
- Predict scheduler performance

**Example:**
```bash
# Standard simulation
./simulator trace.csv \
    --duration_mode limit \
    --backfill_policy easy
```

#### Oracle Mode (`--duration_mode actual`)

**Purpose:** Upper bound on scheduler performance (perfect knowledge)

**Input:** Trace with actual job durations
- Requires: `submit_time`, `actual_run_time`, job size
- Scheduler knows exact runtime in advance (omniscient)
- Unrealistic but useful for comparison

**Use cases:**
- Theoretical best-case performance analysis
- Algorithm comparison baseline
- Upper bound on achievable utilization

**Example:**
```bash
# Oracle mode (scheduler omniscience)
./simulator trace.csv \
    --duration_mode actual
```

#### Replay Mode

**Purpose:** Generate resource traces from historical or pre-computed schedules

**Input:** Trace with pre-computed schedule
- Requires: `begin_time`, `end_time`, job size
- Scheduler is bypassed (uses provided times)
- Can replay simulation output or historical HPC logs
- Supports `advance_to()` for incremental processing

**Use cases:**
- Generate resource usage traces from historical schedules
- Validate resource accounting correctness
- Reproduce execution for debugging/visualization
- Analyze utilization of past system behavior
- Stream historical data for real-time visualization

**Example:**
```bash
# Step 1: Run simulation
./simulator input.csv --outfile schedule.csv --resource_trace sim.csv

# Step 2: Replay the schedule
./simulator schedule.csv --resource_trace replay.csv

# Step 3: Verify (should be identical)
diff sim.csv replay.csv
```

**Note:** Replay mode works with both batch (`run()`) and streaming (`advance_to()`) APIs, making it suitable for real-time visualization and incremental processing.

**Run time modes:**
- `exact` (default): Jobs run exactly `time_limit` (perfect estimates)
- `distribution`: Sample from statistical distribution (realistic variation)
- `column`: Read the job's real run time from the trace (accepted column
  names: `actual_run_time`, `duration`, `actual_duration`, `run_time`)

**Use cases:**
- What-if analysis: "What if we changed the backfill policy?"
- Test scheduler modifications before deployment
- Capacity planning: "Can we handle 20% more jobs?"
- Explore different run time estimation strategies

**Examples:**
```bash
# Simulate with perfect estimates (jobs run exactly time_limit)
./simulator trace.csv \
    --duration_mode limit \
    --run_time_mode exact

# Simulate with realistic variation (80% of time_limit ± 10%)
./simulator trace.csv \
    --duration_mode limit \
    --run_time_mode distribution \
    --run_time_distribution normal \
    --run_time_scale 0.8 \
    --run_time_stddev 0.1

# Simulate but read actual durations from trace column
./simulator trace.csv \
    --duration_mode limit \
    --run_time_mode column
```

**Key difference:**
- **Replay (`actual`)**: Uses actual job durations → reproduces history
- **Simulation (`limit`)**: Uses scheduler's run time estimates → predicts future

**When to use which:**
| Scenario | Mode | Why |
|----------|------|-----|
| Validate against historical data | Replay | Need to match what actually happened |
| Test scheduler changes | Simulation | Explore hypothetical scenarios |
| Capacity planning | Simulation | Predict future with different loads |
| Understand past incidents | Replay | Reproduce exact historical behavior |
| Compare schedulers | Either | Replay = fair comparison, Simulation = realistic estimates |

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
./simulator trace.csv
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
./dr_evt_server --port 50051
```

**Run a client:**
```bash
# Basic usage
./dr_evt_client --server localhost:50051 \
    --trace trace.csv \
    --total_nodes 1000

# With custom parameters
./dr_evt_client --server localhost:50051 \
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
