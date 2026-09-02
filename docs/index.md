# DR_EVT: HPC Job Scheduler Simulator

[![Documentation Status](https://readthedocs.org/projects/dr-evt/badge/?version=latest)](https://dr-evt.readthedocs.io/en/latest/?badge=latest)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](https://github.com/LLNL/dr_evt/blob/main/LICENSE)

**Discrete Event-Driven Simulator for High-Performance Computing Job Schedulers**

DR_EVT simulates HPC job scheduling policies, including an EASY backfilling
implementation whose behavior is checked against a from-scratch Python
reference implementation (see Verification note below - this is a
consistency check between two implementations, not independently derived
ground truth).

```{toctree}
:maxdepth: 2
:caption: Getting Started

getting-started/quickstart
getting-started/installation
getting-started/tutorial
```

```{toctree}
:maxdepth: 2
:caption: User Guide

user-guide/overview
user-guide/command-line
user-guide/grpc-setup
```

```{toctree}
:maxdepth: 2
:caption: Algorithm & Testing

EASY_BACKFILLING_ALGORITHM
TESTING_GUIDE
```

```{toctree}
:maxdepth: 2
:caption: APIs

STREAMING_API
PYTHON_API
```

```{toctree}
:maxdepth: 2
:caption: Reference

CLI_OPTIONS
```

```{toctree}
:maxdepth: 2
:caption: Development

dev/README
DOCUMENTATION_ORGANIZATION
```

## Documentation

### Getting Started

Start here if you're new to DR_EVT:

- **[Quick Start](getting-started/quickstart.md)** - Get up and running in 5 minutes
- **[Installation](getting-started/installation.md)** - Build and install DR_EVT
- **[Basic Tutorial](getting-started/tutorial.md)** - Your first simulation

### User Guide

Complete guide for running simulations:

- **[User Guide](user-guide/overview.md)** - Complete usage manual
- **[Command-Line Options](user-guide/command-line.md)** - All available options
- **[CLI Options Reference](CLI_OPTIONS.md)** - Full option reference

### Algorithm and Testing

- **[EASY Backfilling Algorithm](EASY_BACKFILLING_ALGORITHM.md)** - How the scheduling algorithm works with diagrams
- **[Testing Guide](TESTING_GUIDE.md)** - Complete test catalog, how to run tests, test patterns, and verification methodology

### APIs

- **[Streaming API](STREAMING_API.md)** - Online/incremental simulation API
- **[Python API](PYTHON_API.md)** - Python bindings reference
- **[gRPC Client/Server](CLIENT_SERVER_GUIDE.md)** - Network-exposed streaming API, including the MPI-based multi-client/multi-server test harness

### Reference

- **[CLI Options](CLI_OPTIONS.md)** - Complete command-line reference with configuration examples

### Development

For contributors and maintainers:

- **[Developer Notes](dev/README.md)** - Design decision index and development resources

## Quick Links

- [GitHub Repository](https://github.com/LLNL/dr_evt)
- [Report Issues](https://github.com/LLNL/dr_evt/issues)
- [License](../LICENSE)

## Project Status

**Version:** 1.0
**Scheduling Policies:** EASY Backfilling, FCFS, SJF, LJF priority policies.
Conservative backfilling exists as a CLI/API option but is **not currently
implemented as a distinct algorithm** - it runs the same logic as EASY (see
`src/sim/scheduler_fcfs.cpp`). Don't rely on it producing different
schedules from EASY yet.
**API:** Streaming API for online simulation, Python bindings
**Test suite:** 54 tests across comprehensive/unit/feature/scale/replay
categories; last verified count was 46/54 passing, with 8 tests known and
documented as broken (incomplete or corrupted fixture data - see
[Testing Guide](TESTING_GUIDE.md) for specifics and status, which reflects
a point-in-time check rather than a continuously re-run one).

## About

DR_EVT simulates discrete event-driven HPC job scheduling with:
- An EASY backfilling implementation, checked for consistency against an
  independent Python reference implementation (not independently verified
  against a mathematical ground truth - see the Testing Guide for what
  that distinction means)
- Both replay and simulation modes
- Streaming API for online/incremental simulation
- Support for real HPC traces (Lassen format) and a simpler CSV format
- A test suite covering correctness scenarios, unit/format tests, policy
  comparisons, larger-scale traces, and replay-consistency checks

Developed at Lawrence Livermore National Laboratory.
