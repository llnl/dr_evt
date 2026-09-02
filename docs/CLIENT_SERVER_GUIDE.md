# gRPC Client/Server Guide

## Overview

DR_EVT's [streaming API](STREAMING_API.md) (`submit_job()`, `advance_to()`,
and the monitoring/statistics methods) lets external code feed jobs
incrementally and control simulation time advancement, rather than loading
a full trace and running it start-to-finish in one call. The gRPC
client/server exposes that same streaming API over the network: a
`dr_evt_server` process holds one `Simulation` instance per connected
session, and any number of `dr_evt_client` processes (or your own gRPC
client, in any language gRPC supports) can drive it remotely.

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
`--config` file feature, see `docs/CLI_OPTIONS.md`) - `SetupProtobuf.cmake`
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
| `SubmitJobRequest` | `Simulation::submit_job()` |
| `AdvanceToRequest` | `Simulation::advance_to()` |
| `RunUntilExclusiveRequest` | `Simulation::run_until_exclusive()` |
| `GetStatisticsRequest`, `GetCurrentTimeRequest`, etc. | The monitoring/statistics methods |

Every `ClientMessage` carries a `request_id`, echoed back on the matching
`ServerMessage`, so a client can correlate responses even if it pipelines
multiple in-flight requests (the provided `dr_evt_client` sends one at a
time and waits for each response, but the protocol itself doesn't require
that).

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

`dr_evt_client` is a minimal example client: it reads a trace file's own
`job_submit_time` column directly (the client, not the server, is
authoritative on arrival timing for the streaming API - see the source
comments in `src/proto/dr_evt_client.cpp` for why), submits every job in
the file using those real submit times, advances the simulation to
completion, and prints final statistics. It's meant as a working reference
for writing your own client against the same `.proto` service, not as a
general-purpose tool.

The server binds `0.0.0.0` by default in the examples above so it's
reachable from other machines - bind `127.0.0.1` instead if you only need
same-machine access.

## Multi-client/multi-server: the MPI test harness

A single client feeding a single server needs no special coordination.
But when **multiple independent clients** are each feeding their own
server - for example, modeling job submission from multiple sites or
queues that together are supposed to represent one shared, real-world
timeline - naively running each client as fast as it can would let one
client's stream race arbitrarily far ahead of the other's, breaking the
cross-stream arrival ordering the two streams are meant to represent
together.

`tests/test_grpc_multi_client_server.cpp` (built as
`test_grpc_multi_client_server`, requires MPI - see below) demonstrates
and tests the fix: a conservative, lockstep synchronization scheme, in the
same spirit as Chandy-Misra-Bryant parallel discrete-event simulation.
Every round, every client reports the arrival time of its own next
not-yet-submitted job (`MPI_Allreduce(MPI_MIN)` across a client-only MPI
sub-communicator); the global minimum across all clients is the only time
it's safe for *any* of them to advance to, since no client can still have
an unsubmitted job below that time once the reduction completes. Whichever
client's own next job matches that global minimum submits it this round
(ties - simultaneous arrivals across streams - are both submitted in the
same round); every client then advances its own server to the global
minimum; repeat until every client has exhausted its trace.

Note what this coordination does and doesn't guarantee: since each
client/server pair is otherwise fully independent (no shared nodes or
state between the separate `Simulation` instances), the lockstep
scheme doesn't change either individual simulation's own schedule - that's
already guaranteed correct by `advance_to()`'s own precondition,
regardless of pacing. What it provides is realistic, coordinated wall-clock
pacing *across* the streams, for scenarios where that relative timing
matters (e.g. future cross-stream interaction, or live/interactive
multi-site demos).

### Building and running

Requires MPI (`find_package(MPI)` in `CMakeLists.txt` - the target is
silently skipped, not a build failure, if MPI isn't found):

```bash
cmake .. -DDR_EVT_ENABLE_PROTOBUF=ON -DDR_EVT_ENABLE_GRPC=ON
make test_grpc_multi_client_server-bin

mpirun -np <2*N> ./build/test_grpc_multi_client_server \
    ./build/dr_evt_server <base_port> \
    <trace_file_1> ... <trace_file_N>
```

Rank layout: with `2*N` total ranks, ranks `[0, N)` are servers and ranks
`[N, 2N)` are clients, paired 1:1 (client rank `N+i` drives server rank
`i`, both using `trace_file_i`). Each server rank forks and execs its own
`dr_evt_server` child process, listening on `<base_port> + i`, and sends
its own real hostname (via `gethostname()`, not assumed shared loopback)
to its paired client over MPI - so this places correctly across real,
physically separate nodes via `mpirun`'s own host placement
(`--host`/`--hostfile`), not just on a single machine. (Verified
single-node only in development - no multi-node environment was available
to confirm cross-machine placement directly, though nothing in the design
assumes same-machine access.)

## Testing

`tests/run_grpc_tests.sh` covers both the basic single-pair session and
the MPI harness (skipped gracefully, not failed, if the MPI binary wasn't
built or `mpirun` isn't on `PATH`):

```bash
./tests/run_grpc_tests.sh
```

It uses `tests/test_traces/grpc/trace_a.csv` and `trace_b.csv` - two small,
independent traces with deliberately interleaved arrival times, chosen
specifically to exercise the lockstep synchronization (rather than two
traces that happen to never actually compete for the same round). Expected
makespans (40 and 30 respectively) are hand-computed directly from each
trace's own job list, not generated by any script - see the comments in
`tests/run_grpc_tests.sh` for the arithmetic.

This is a separate, dedicated test script (not folded into
`run_feature_tests.sh` or another existing harness) because it needs its
own binaries (`dr_evt_server`, `dr_evt_client`, and optionally
`test_grpc_multi_client_server`) and its own `mpirun` invocation, neither
of which fit the existing scripts' `./build/simulator <trace> --flags`
pattern.
