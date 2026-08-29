# DR_EVT: HPC Job Scheduler Simulator

**Discrete Event-Driven Simulator for High-Performance Computing Job Schedulers**

DR_EVT is a validated simulator for HPC job scheduling policies, with verified EASY backfilling implementation.

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
- **[Trace Formats](user-guide/trace-formats.md)** - Input file formats
- **[Scheduling Policies](user-guide/scheduling-policies.md)** - EASY backfilling, FCFS, etc.
- **[Simulation Modes](user-guide/simulation-modes.md)** - Replay vs simulation

### Verification

Algorithm correctness and testing:

- **[Verification Summary](verification/summary.md)** - ✓ All 23 tests pass
- **[Analytical Verification](verification/analytical.md)** - Hand-traced ground truth
- **[EASY Backfilling](verification/easy-backfilling.md)** - Algorithm properties
- **[Test Descriptions](verification/test-descriptions.md)** - What each test validates

### Development

For contributors and developers:

- **[Architecture](development/architecture.md)** - System design
- **[Algorithm Details](development/algorithm.md)** - Simulation algorithm
- **[Design Decisions](development/design-decisions.md)** - Why we made certain choices
- **[Streaming API](STREAMING_API.md)** - Online simulation API (NEW)
- **[Contributing](development/contributing.md)** - How to contribute

### Reference

Technical reference material:

- **[API Documentation](api/index.md)** - C++ API reference
- **[Streaming API](STREAMING_API.md)** - Online simulation API
- **[Terminology](reference/terminology.md)** - Terms and definitions
- **[Configuration Files](reference/config-files.md)** - Protobuf configs

## Quick Links

- [GitHub Repository](https://github.com/LLNL/dr_evt)
- [Report Issues](https://github.com/LLNL/dr_evt/issues)
- [License](../LICENSE)

## Project Status

**Version:** 1.0  
**Status:** ✓ Verified Correct (54 tests pass)  
**Scheduling Policies:** EASY Backfilling (verified), Conservative Backfilling, FCFS, SJF, LJF  
**API:** Streaming API for online simulation (NEW)  
**Last Updated:** 2026-08-28

## About

DR_EVT simulates discrete event-driven HPC job scheduling with:
- Verified EASY backfilling algorithm
- Both replay and simulation modes
- Streaming API for online/incremental simulation
- Support for real HPC traces (Lassen format)
- Comprehensive test suite (54 tests)
- Analytical verification against hand-traced oracles

Developed at Lawrence Livermore National Laboratory.
