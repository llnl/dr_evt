# gRPC Client/Server Guide and API

## Overview

DR_EVT's [streaming API](api/STREAMING_API.md) (`append_job()`/`append_jobs()`,
`advance_to()`, and the monitoring/statistics methods) lets
external code feed genuinely new jobs incrementally and control simulation
time advancement, rather than loading a full trace and running it
start-to-finish in one call. The gRPC client/server exposes that same
streaming API over the network: a `dr_evt_server` process holds one
`Simulation` instance per connected session, and any number of
`dr_evt_client` processes (or your own gRPC client, in any language gRPC
supports) can drive it remotely.

This is separate from, and does not replace, the plain CLI `simulator`
binary (batch mode) or the Python bindings (in-process streaming API). Use
the gRPC client/server specifically when the thing feeding jobs needs to
run in a different process - or on a different machine - than the
simulation itself.

Build and connection instructions are in
[Client/Server Setup](user-guide/grpc-setup.md). Dependency discovery is
documented in [Installation](getting-started/installation.md).

## The service definition

`src/proto/dr_evt_service.proto` defines a single bidirectional-streaming
RPC (`SimulationService.Session`), wrapping the same operations available
in-process via the streaming API:

| Request | Corresponds to |
|---|---|
| `InitRequest` | Constructing a `Simulation` from a `Sim_Params`-equivalent config |
| `InitializeTraceRequest` | `Simulation::initialize_trace()` |
| `AppendJobRequest` | `Simulation::append_job()` - a genuinely new job the server has never seen before |
| `AppendJobsRequest` | `Simulation::append_jobs()` - the batch counterpart, several new jobs in one call |
| `AdvanceToRequest` | `Simulation::advance_to()` |
| `RunUntilExclusiveRequest` | `Simulation::run_until_exclusive()` |
| `GetFCFSHeadShadowTimeRequest` | FCFS-head shadow time only: the earliest reserved start time, or `-1` with no waiting head |
| `GetBackfillWindowRequest` | One FCFS/EASY reservation snapshot: current capacity, shadow time, and projected releases |
| `GetStatisticsRequest`, `GetCurrentTimeRequest`, etc. | The monitoring/statistics methods |

Every `ClientMessage` carries a `request_id`, echoed back on the matching
`ServerMessage`, so a client can correlate responses even if it pipelines
multiple in-flight requests (the provided `dr_evt_client` sends one at a
time and waits for each response, but the protocol itself doesn't require
that).

### FCFS/EASY shadow-time and resource-change queries

Send `GetFCFSHeadShadowTimeRequest` when only the FCFS queue head's earliest
reserved start time is needed. Its `GetFCFSHeadShadowTimeResponse.shadow_time`
is `-1` when no job is waiting.

Send `GetBackfillWindowRequest` when the resource-change times that lead to
that reservation are also needed. After submitting work and advancing the
simulation to the desired point in time, its matching
`GetBackfillWindowResponse` is an atomic scheduling snapshot with:

- `current_time`: the simulation time at which the snapshot was made.
- `available_nodes`: nodes free immediately at `current_time`.
- `shadow_time`: the earliest start time reserved for the FCFS queue head;
  `-1` if no job is waiting.
- `releases`: the resource-change-time query: chronologically ordered
  `ResourceRelease` events between the current time and shadow time,
  inclusive. Each has its absolute simulation `time` and `nodes_released`;
  jobs ending at the same time are combined into one event.

The projection deliberately uses each running job's `time_limit`, not its
actual runtime. That is the same estimate used by the FCFS/EASY scheduler to
calculate `shadow_time`, so clients can safely use the response to evaluate
backfill candidates without seeing a conflicting reservation model. If there
is no waiting head, or the head can run immediately, `releases` is empty.

For example, with no free nodes, a 40-node job predicted to end at time 50,
a 60-node job predicted to end at time 100, and a 100-node FCFS head, the
response at time 0 has `shadow_time = 100` and releases `(50, 40)` and
`(100, 60)`.

Session initialization, completion, reuse, and shutdown are documented in
[Client/Server Setup](user-guide/grpc-setup.md#session-identity-and-completion).

## Examples, use cases, and tests

- The [example C++ client](https://github.com/LLNL/dr_evt/blob/main/src/proto/dr_evt_client.cpp)
  demonstrates the complete request sequence described in
  [Connecting the example client](user-guide/grpc-setup.md#connecting-the-example-client).
- [Client/Server Use Cases](user-guide/client-server-use-cases.md) links the
  Python multi-server, MPI-launcher, and synchronized-system examples.
- The [distributed client/server tests](https://github.com/LLNL/dr_evt/blob/main/tests/README.md#distributed-clientserver-tests)
  section lists the exact gRPC and MPI test commands, fixtures, and coverage.
