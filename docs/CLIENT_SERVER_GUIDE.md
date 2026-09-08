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

## Building

The gRPC client/server is optional and requires two CMake flags:

```bash
cmake .. \
  -DDR_EVT_ENABLE_PROTOBUF=ON \
  -DDR_EVT_ENABLE_GRPC=ON
make dr_evt_server-bin dr_evt_client-bin
```

`DR_EVT_ENABLE_GRPC=ON` requires `DR_EVT_ENABLE_PROTOBUF=ON` too, since the
gRPC service definition (`dr_evt_service.proto`) is itself built on the
Protobuf runtime. When gRPC is enabled, it also becomes the sole provider
of Protobuf support for the rest of the project (including the existing
`--config` file feature, see `docs/user-guide/command-line.md`) - `SetupProtobuf.cmake`
is skipped entirely in that case, since gRPC's own Protobuf is already
fully sufficient, and running both independently would mean building or
linking two separate copies of Protobuf for no benefit.

### Dependency resolution

`cmake/modules/SetupGRPC.cmake` tries, in order:

1. `find_package(gRPC CONFIG)` and `find_package(Protobuf CONFIG)` - works
   with a properly-packaged install (e.g. `apt install libgrpc++-dev
   protobuf-compiler-grpc` on Debian/Ubuntu also provides gRPC's own CMake
   config; a MODULE-mode Protobuf fallback is tried too, since Debian/
   Ubuntu's separate `libprotobuf-dev` package does not ship a CMake
   config file itself, only gRPC's own package does).
2. `FetchContent`, building gRPC's own full source tree (which bundles a
   compatible Protobuf) if neither of the above succeeds.

The second path exists specifically for users **without root/sudo access**
to install system packages - the common case on shared HPC/cluster
environments, which is most of this project's actual user base. It works
without any special configuration, but is significantly slower (it clones
and builds gRPC's entire source tree) the first time it runs for a given
build directory.

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

One session (one call to `Session()`) corresponds to one server-side
`Simulation` instance for the stream's lifetime - there's no way to reset
or reinitialize a session in place; disconnect and reconnect for a new run.

## Running the server and client

```bash
# Start a server, listening on all interfaces
./build/dr_evt_server 0.0.0.0:50051

# In another terminal (or on another machine, if reachable):
./build/dr_evt_client <server_host>:50051 /path/to/trace.csv
```

`dr_evt_client` is a minimal example client demonstrating genuine
streaming: it reads job data (`submit_time`, `num_nodes`, `queue`,
`time_limit`) from a file *only* client-side - the server never loads
this file itself (no `InitializeTraceRequest` is sent at all) - and
appends each job via `AppendJobRequest` to a server that has never seen
any of it before; each append enqueues the job at its supplied
`submit_time`. It's meant as a working reference for writing your own
client against the same `.proto` service, not as a general-purpose tool.

The server binds `0.0.0.0` by default in the examples above so it's
reachable from other machines - bind `127.0.0.1` instead if you only need
same-machine access.

## Optional MPI test harness

The normal client/server deployment is a directly managed bare-metal server or
a containerized server, with clients connecting over gRPC. MPI is neither
required nor recommended as the default deployment mechanism. The harness in
this section exists to exercise coordinated multi-client test scenarios.

A single client feeding a single server needs no special coordination.
But when **multiple independent clients** are each feeding their own
server - for example, modeling job submission from multiple sites or
queues that together are supposed to represent one shared, real-world
timeline - naively running each client as fast as it can would let one
client's stream race arbitrarily far ahead of the other's, breaking the
cross-stream arrival ordering the two streams are meant to represent
together.

`tests/test_grpc_multi_client_server.cpp` (built as
`test_grpc_multi_client_server`, requires MPI - see below) is a two-server
composite-stream integration test. Its two client ranks load separate
ordinary-job CSVs plus a shared composite-job CSV. Composite rows are grouped
by `composite_id`; every event supplies one fragment per server and the
clients synchronize with an MPI client-only barrier before issuing the two
corresponding `AppendJobsRequest`s.

The first composite event tests the inclusive boundary
`append_jobs(... <= t2)`, `advance_to(t1 == t2)`, then composite append at
`t3 == t2`. Subsequent composite events form an ordered stream. The fixture
also includes an ordinary batch with `t1 < t2 < t3`, followed by ordinary
work at `t4 > t3`, so both equality and strict inequalities are tested over
the network API.

### Composite-stream test timeline

```{mermaid}
:name: composite-stream-timeline
:align: center
:config: {"theme":"base","themeVariables":{"noteBkgColor":"#e2f3e7","noteBorderColor":"#8ab99a","noteTextColor":"#234d31"},"sequence":{"useMaxWidth":true,"diagramMarginX":5}}

sequenceDiagram
    participant C1 as Client 1
    participant S1 as Server 1
    participant C2 as Client 2
    participant S2 as Server 2

    Note over C1,S2: Client inputs: ordinary-server1.csv, ordinary-server2.csv, composite_jobs.csv
    par Initial ordinary batch, through t2 = 10
        C1->>S1: AppendJobs([t=0, t=10])
    and
        C2->>S2: AppendJobs([t=0, t=10])
    end
    par Equality boundary: t1 = t2 = t3 = 10
        C1->>S1: AdvanceTo(10)
    and
        C2->>S2: AdvanceTo(10)
    end
    Note over C1,C2: MPI barrier
    par Composite event "equal_boundary"
        C1->>S1: AppendJobs([composite fragment, t=10])
    and
        C2->>S2: AppendJobs([composite fragment, t=10])
    end
    par Evaluate equal-time arrivals
        C1->>S1: AdvanceTo(10)
    and
        C2->>S2: AdvanceTo(10)
    end

    par Ordinary batch through t2 = 20
        C1->>S1: AppendJobs([t=20]) + SubmitJob
    and
        C2->>S2: AppendJobs([t=20]) + SubmitJob
    end
    par Strict boundary: t1 = 15 < t2 = 20 < t3 = 25
        C1->>S1: AdvanceTo(15)
    and
        C2->>S2: AdvanceTo(15)
    end
    Note over C1,C2: MPI barrier
    par Composite event "strict_boundary"
        C1->>S1: AppendJobs([composite fragment, t=25])
    and
        C2->>S2: AppendJobs([composite fragment, t=25])
    end
    par Evaluate composite event
        C1->>S1: AdvanceTo(25)
    and
        C2->>S2: AdvanceTo(25)
    end
    par Final ordinary work: t4 = 30 > t3
        C1->>S1: AppendJobs([t=30]) + SubmitJob
        C1->>S1: FinishSimulation()
    and
        C2->>S2: AppendJobs([t=30]) + SubmitJob
        C2->>S2: FinishSimulation()
    end
```

### Building and running the test

Requires MPI (`find_package(MPI)` in `CMakeLists.txt` - the target is
silently skipped, not a build failure, if MPI isn't found):

```bash
cmake .. -DDR_EVT_ENABLE_PROTOBUF=ON -DDR_EVT_ENABLE_GRPC=ON
make test_grpc_multi_client_server-bin

mpirun -np 4 ./build/test_grpc_multi_client_server \
    ./build/dr_evt_server <base_port> \
    tests/test_traces/grpc/composite_server1.csv \
    tests/test_traces/grpc/composite_server2.csv \
    tests/test_traces/grpc/composite_jobs.csv

# Or, from a Slurm allocation:
srun -N 1 -n 4 ./build/test_grpc_multi_client_server \
    ./build/dr_evt_server <base_port> \
    tests/test_traces/grpc/composite_server1.csv \
    tests/test_traces/grpc/composite_server2.csv \
    tests/test_traces/grpc/composite_jobs.csv
```

Rank layout: ranks 0-1 are servers and ranks 2-3 are clients, paired 1:1.
Each server rank forks and execs its own
`dr_evt_server` child process, listening on `<base_port> + i`. The current
test advertises `127.0.0.1` to its paired client, so it is a **single-node
test**; use one allocated node for it. It is intentionally a testing aid, not
a multi-node deployment recipe.

## Testing

`tests/run_grpc_tests.sh` covers both the basic single-pair session and
the MPI harness (skipped gracefully, not failed, if the MPI binary wasn't
built or neither `mpirun` nor `srun` is on `PATH`). The runner prefers
`mpirun` and falls back to `srun`:

```bash
./tests/run_grpc_tests.sh
```

The focused FCFS/EASY window test starts a local server and runs the wire
test, including the example projection above:

```bash
./tests/run_backfill_window_grpc_test.sh
```

It uses `composite_server1.csv`, `composite_server2.csv`, and
`composite_jobs.csv`. The three files deliberately exercise the equal-time
and strict-time `AppendJobsRequest`/`AdvanceToRequest` boundaries described
above.

This is a separate, dedicated test script (not folded into
`run_feature_tests.sh` or another existing harness) because it needs its
own binaries (`dr_evt_server`, `dr_evt_client`, and optionally
`test_grpc_multi_client_server`) and its own MPI-launcher invocation, neither
of which fit the existing scripts' `./build/simulator <trace> --flags`
pattern.
