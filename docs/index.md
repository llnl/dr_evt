# DR_EVT: HPC Job Scheduler Simulator

[![Documentation Status](https://readthedocs.org/projects/dr-evt/badge/?version=latest)](https://dr-evt.readthedocs.io/en/latest/?badge=latest)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](https://github.com/LLNL/dr_evt/blob/main/LICENSE)

**Discrete Event-Driven Simulator for High-Performance Computing Job Schedulers**

DR_EVT simulates HPC job scheduling policies with EASY and CONSERVATIVE backfilling
implementations. Uniquely supports **online simulation via gRPC**, enabling coordinated
multi-cluster simulations in a distributed fashion and digital-twin scheduler interacting in real-time.

Scheduler behavior is verified against from-scratch Python reference implementations
(consistency check between implementations, not independently derived ground truth).

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

### Algorithm and Testing

- **[EASY Backfilling Algorithm](EASY_BACKFILLING_ALGORITHM.md)** - How the scheduling algorithm works with diagrams
- **[Testing Guide](TESTING_GUIDE.md)** - Complete test catalog, how to run tests, test patterns, and verification methodology

### APIs

- **[Streaming API](STREAMING_API.md)** - Online/incremental simulation API
- **[Python API](PYTHON_API.md)** - Python bindings reference
- **[gRPC Client/Server](CLIENT_SERVER_GUIDE.md)** - Network-exposed streaming API, including the MPI-based multi-client/multi-server test harness

### Development

For contributors and maintainers:

- **[Developer Notes](dev/README.md)** - Design decision index and development resources

## Quick Links

- [GitHub Repository](https://github.com/LLNL/dr_evt)
- [Report Issues](https://github.com/LLNL/dr_evt/issues)
- [License](../LICENSE)

## Project Status

**Version:** 1.0

**Scheduling Policies:**
- **Backfill:** EASY and CONSERVATIVE (fully implemented)
- **Priority:** FCFS, SJF, LJF

**APIs:**
- **Streaming API:** Online/incremental simulation
- **gRPC Service:** Network-exposed streaming API for multi-cluster coordination
- **Python Bindings:** Full Python API support

**Test Suite:** 57 tests across comprehensive/unit/feature/conservative/scale/replay
categories. All 57/57 passing as of 2026-09-03 (see [Testing Guide](TESTING_GUIDE.md)).

## About

DR_EVT simulates discrete event-driven HPC job scheduling with:
- **EASY and CONSERVATIVE backfilling** implementations, verified against
  independent Python reference implementations (consistency checks, not
  mathematical ground truth - see Testing Guide)
- **gRPC-based online simulation service** enabling coordinated multi-cluster
  simulations in a distributed fashion and digital-twin scheduler interacting in real-time
- **Replay and simulation modes** for both offline analysis and online operation
- **Streaming API** for incremental job submission and online scheduling decisions
- **Real HPC trace support** (Lassen format) plus simpler CSV format
- **Comprehensive test suite** covering correctness, differential comparisons,
  and large-scale scenarios

Developed at Lawrence Livermore National Laboratory.
