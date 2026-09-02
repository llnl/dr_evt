# Discrete Resource Event Modeling and Multi-cluster Scheduling Simulator

[![Documentation Status](https://readthedocs.org/projects/dr-evt/badge/?version=latest)](https://dr-evt.readthedocs.io/en/latest/?badge=latest)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](https://github.com/LLNL/dr_evt/blob/main/LICENSE)

Discrete resource event modeling and multi-cluster scheduling simulator
(DR_EVT) aims to provide a computational environment for simulating job
scheduling and resource management using a set of heterogenous clusters.

**[📚 Read the Full Documentation on ReadTheDocs →](https://dr-evt.readthedocs.io/)**

## Features

### Core Simulation
- **Verified Scheduler Implementation**: EASY backfilling algorithm verified against analytical ground truth (34 comprehensive tests)
- **Multiple Scheduling Policies**: EASY/Conservative backfilling, FCFS, SJF, LJF
- **Block Queue Wait Queue**: Optional block-based queue with metadata filtering (7 block sizes: 4-256)
- **Replay and Simulation Modes**: Replay historical traces or simulate with runtime distributions
- **Early Completion Support**: Jobs can finish before time_limit (actual_runtime < time_limit)

### APIs & Integration
- **Streaming API**: Online simulation with dynamic job submission (`submit_job`, `advance_to`, `run_until_exclusive`)
- **gRPC Client/Server**: Remote simulation control over network (optional)
- **Python Bindings**: Python API for in-process simulation control
- **Protocol Buffer Configuration**: Structured configuration files for complex simulations

### Testing & Validation
- **Dual Validation**: C++ simulator verified against independent Python reference implementation
  - Python reference: Pure Python EASY backfilling (scripts/python_reference_scheduler.py)
  - All 34 comprehensive tests pass with byte-for-byte identical output
  - Regression detection: Any scheduling behavior change flagged immediately
- **Analytical Testing**: Mathematical formulas generate test inputs, hand-calculated expected outputs
- **Differential Testing**: Compare multiple scheduler implementations (deque, multimap, block queue)
- **CI/CD Integration**: Automated testing on every commit via GitHub Actions

## Documentation

**Primary Documentation**: [**ReadTheDocs** (https://dr-evt.readthedocs.io/)](https://dr-evt.readthedocs.io/)
- Searchable, versioned documentation with PDF/EPUB downloads
- Automatic builds from `main` branch
- Mobile-friendly with navigation

### Quick Reference (GitHub)
For quick access without leaving GitHub:
- **[Complete Documentation Index](docs/)** - All guides, references, and specifications
- **[EASY Backfilling Algorithm](docs/EASY_BACKFILLING_ALGORITHM.md)** - Algorithm specification and verification
- **[Streaming API](docs/STREAMING_API.md)** - Dynamic job submission API
- **[Client/Server (gRPC)](docs/CLIENT_SERVER_GUIDE.md)** - Remote simulation over network
- **[Python API](docs/PYTHON_API.md)** - Python bindings and reference implementation
- **[CLI Options](docs/CLI_OPTIONS.md)** - Command-line reference
- **[Testing Guide](docs/TESTING_GUIDE.md)** - Test philosophy, organization, and test suite details

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

**Note:** Local builds are optional for contributors. The official documentation is automatically built and published to ReadTheDocs on every commit to `main`.

## Current Requirements:
 + **Platforms targeted**: Linux-based systems
 + **c++ compiler that supports c++17**
 + **GNU Boost library**
 + **cmake 3.24 or later**
 + [**Protocol Buffers**](https://developers.google.com/protocol-buffers)

   We use the google protocol buffers library for parsing the configuration file
   of simulation, which is written by users in the [**protocol buffers language**](https://developers.google.com/protocol-buffers/docs/proto3).
   This is a required package. A user can indicate the location of a
   pre-installed copy via `-DPROTOBUF_ROOT=<path>`. Without it, building DR_EVT
   consists of two stages. In the first stage, the source of protocol buffer will
   be downloaded. Then, the library as well as the protoc compiler will be built
   and installed under where the rest of DR_EVT project will be.
   In the second stage, the DR_EVT project will be built using the protocol buffer
   installed in the first stage. Both stages require the same set of options for
   the cmake command.
   In case of cross-compiling, the path to the protoc compiler and the path to
   the library built for the target platform can be explicitly specified via
   `-DProtobuf_PROTOC_EXECUTABLE=<installation-for-host/bin/protoc>`
   and `-DPROTOBUF_DIR=<installation-for-target>` respectively.


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
  runtime_mode: "limit"       # "limit" or "actual"

  # Trace format
  trace_format: "simple"      # "simple" or "lassen"
  timestamp_format: "epoch"   # "epoch" or "iso"

  # Simulation limits
  max_jobs: 10000

  # Duration simulation (optional)
  duration_mode: "exact"      # "column", "exact", or "distribution"
  duration_scale: 1.0

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

### Simulation vs Replay Modes

DR_EVT supports two modes for processing job traces:

#### Replay Mode (`--runtime_mode actual`)

**Purpose:** Reproduce what actually happened on a real HPC system

**Input:** Historical trace with actual job durations
- Reads `actual_runtime` from trace file
- Or uses recorded start/end times

**Output:** Schedule matching the original execution
- Jobs run for their actual recorded duration
- Simulates the exact behavior that occurred

**Use cases:**
- Validate scheduler correctness against historical data
- Understand past system behavior
- Compare how different schedulers would have performed on real workloads
- Verify trace processing correctness

**Example:**
```bash
# Replay with actual runtimes from trace
./simulator trace.csv \
    --runtime_mode actual \
    --duration_mode column
```

#### Simulation Mode (`--runtime_mode limit`, default)

**Purpose:** Predict what WOULD happen under different conditions

**Input:** Trace with user-requested time limits
- Uses `time_limit` column (what users requested)
- Actual runtime not required

**Output:** Predicted schedule based on scheduler decisions
- Jobs run for simulated durations (controlled by `--duration_mode`)

**Duration modes:**
- `exact` (default): Jobs run exactly `time_limit` (perfect estimates)
- `distribution`: Sample from statistical distribution (realistic variation)
- `column`: Read `actual_duration` column (if available)

**Use cases:**
- What-if analysis: "What if we changed the backfill policy?"
- Test scheduler modifications before deployment
- Capacity planning: "Can we handle 20% more jobs?"
- Explore different runtime estimation strategies

**Examples:**
```bash
# Simulate with perfect estimates (jobs run exactly time_limit)
./simulator trace.csv \
    --runtime_mode limit \
    --duration_mode exact

# Simulate with realistic variation (80% of time_limit ± 10%)
./simulator trace.csv \
    --runtime_mode limit \
    --duration_mode distribution \
    --duration_distribution normal \
    --duration_scale 0.8 \
    --duration_stddev 0.1

# Simulate but read actual durations from trace column
./simulator trace.csv \
    --runtime_mode limit \
    --duration_mode column
```

**Key difference:**
- **Replay (`actual`)**: Uses actual job durations → reproduces history
- **Simulation (`limit`)**: Uses scheduler's runtime estimates → predicts future

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

DR_EVT can run as a network service, enabling remote simulation control from any gRPC-capable language.

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
    --backfill_policy easy \
    --priority_policy fcfs \
    --outfile results.csv
```

**Use cases:**
- **Remote simulation**: Run simulator on HPC cluster, control from laptop
- **Multi-language integration**: Use Python/Java/Go clients with C++ simulator
- **Distributed testing**: Multiple clients testing different scenarios
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
