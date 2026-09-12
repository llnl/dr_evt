# User Guide

## Introduction

DR_EVT (Discrete Resource Event Modeling) simulates HPC job schedulers with
EASY or conservative backfilling and FCFS, SJF, or LJF priority policies. It
can schedule workload traces, replay recorded schedules, and accept jobs
incrementally through its C++, Python, or gRPC interfaces.

## Inputs and outputs

DR_EVT accepts:

- a job trace or an ordered list of trace files, documented in
  [Input Trace Files](trace-formats.md); and
- simulation settings supplied through
  [command-line options](command-line.md), a
  [Protobuf text configuration](protobuf-config.md), or both.

It produces:

- a scheduled-job trace and a resource-usage trace, documented in
  [Output Trace Files](output-traces.md); and
- a CLI summary of the run and its scheduling statistics, also documented in
  [Output Trace Files](output-traces.md#cli-summary).

## Interfaces

- The `simulator` executable runs complete traces. See the
  [Quick Start](../getting-started/quickstart.md) and
  [Command-Line Options](command-line.md).
- The [Streaming API](../api/STREAMING_API.md) accepts jobs and advances
  simulation time incrementally in-process.
- The [Python API](../api/PYTHON_API.md) exposes the simulator to Python.

The native C++ simulator, C++ streaming API, and Python API run in-process and
do not require client/server setup.

## Distributed client/server deployment

For distributed or digital-twin deployments, the optional
[gRPC client/server interface](grpc-setup.md) lets client processes and
controllers open independent sessions to any number of server processes. Each
session gets its own isolated simulation, scheduler state, and nodes.

:::{figure} ../_static/client-server-architecture.svg
:alt: Workload sources feed client processes and digital-twin controllers, which open independent gRPC sessions to server processes. Each session has an isolated simulation, scheduler state, and nodes.
:width: 100%
:align: center
:::

For deployment patterns, see
[Client/Server Use Cases](client-server-use-cases.md).

## Further reading

- [Backfilling Algorithms](../BACKFILLING_ALGORITHMS.md)
- [Fugaku Power-Usage Experiment](fugaku-power-experiment.md)
- [Testing Guide](../TESTING_GUIDE.md)
- [Developer Notes](../dev/README.md)
