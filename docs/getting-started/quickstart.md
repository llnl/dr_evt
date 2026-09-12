# Quick Start

This guide builds DR_EVT and runs a small scheduler simulation. A basic build
requires CMake 3.24 or later, a C++17 compiler, and Boost. Optional Python,
Protobuf, gRPC, MPI, and testing dependencies are covered in
[Installation](installation.md).

## Build

```bash
git clone https://github.com/LLNL/dr_evt.git
cd dr_evt

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

If Boost is not installed, configuration uses the repository's dependency
fallback. The first build can therefore take longer and may require network
access.

## Run a simulation

Run the included two-job trace on a 100-node system:

```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator tests/test_traces/unit/simple_2jobs.csv \
  --trace_format simple \
  --timestamp_format epoch \
  --run_time_mode limit \
  --total_nodes 100 \
  --outfile results.csv \
  --resource_trace resources.csv
```

The simulator prints the resolved configuration followed by job counts,
simulation time, average wait and turnaround times, makespan, average and peak
waiting-queue lengths, and process wall-clock time. Definitions are in
[Output Trace Files](../user-guide/output-traces.md#cli-summary).

It also writes:

- `results.csv`, the scheduled start and end time of each accepted job; and
- `resources.csv`, free and allocated nodes after every resource change.

Inspect them with:

```bash
head results.csv
head resources.csv
```

## Use your own trace

A minimal simulation trace is a CSV file with named columns:

```text
job_submit_time,num_nodes,time_limit
0,80,100
10,15,30
20,60,50
```

Run it by replacing the input path in the command above. The full input
schemas, optional columns, replay detection, and timestamp forms are defined
in [Input Trace Files](../user-guide/trace-formats.md).

## Choose scheduler behavior

The defaults are FCFS priority with EASY backfilling. For example, strict FCFS
without backfilling uses:

```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator input.csv \
  --priority_policy fcfs \
  --backfill_policy none
```

See [Command-Line Options](../user-guide/command-line.md) for every flag and
[Scheduling Policies](../BACKFILLING_ALGORITHMS.md) for policy semantics.
Protobuf configuration files are described in
[Protocol Buffer Configuration](../user-guide/protobuf-config.md).

## Next steps

- [Tutorial](tutorial.md) walks through a three-job EASY schedule.
- [User Guide](../user-guide/overview.md) maps inputs, outputs, and interfaces.
- [Installation](installation.md) covers optional features and troubleshooting.
- [Test Suite](https://github.com/LLNL/dr_evt/blob/main/tests/README.md#build-and-run) lists
  validation commands.
