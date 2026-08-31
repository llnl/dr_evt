# DR_EVT: HPC Job Scheduler Simulator

**Discrete Event-Driven Simulator for High-Performance Computing Job Schedulers**

DR_EVT simulates HPC job scheduling policies, including an EASY backfilling
implementation whose behavior is checked against a from-scratch Python
reference implementation (see Verification note below - this is a
consistency check between two implementations, not independently derived
ground truth).

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

- **[EASY Backfilling Algorithm](EASY_BACKFILLING_ALGORITHM.md)** - How the scheduling algorithm works
- **[Testing Guide](TESTING_GUIDE.md)** - How the test suite is structured, what it does and doesn't verify, and known limitations

### APIs

- **[Streaming API](STREAMING_API.md)** - Online/incremental simulation API
- **[Python API](PYTHON_API.md)** - Python bindings reference

### Reference

- **[Terminology](reference/terminology.md)** - Terms and definitions

### Development

For contributors:

- **[Developer Notes](dev/README.md)** - Session notes and design decision index
- **[Design Decisions](dev/design-decisions/)** - Specific architectural decisions (Conan removal, renaming history, trace format choices)

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
