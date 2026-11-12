# Client/Server Setup

DR_EVT can run as a network service, enabling remote simulation control
from any language that supports gRPC. This page covers building and connecting
a client and server. See [Client/Server Use Cases](client-server-use-cases.md)
for bare-metal, container, multi-server, and synchronized-system patterns;
MPI is documented there only as an optional test harness. See also
the [Client/Server Reference](../CLIENT_SERVER_GUIDE.md) for the service
definition and the
[examples, use cases, and tests](../CLIENT_SERVER_GUIDE.md#examples-use-cases-and-tests)
for concrete implementations and validation.

## Client/server session

```{mermaid}
:name: client-server-session-diagram

sequenceDiagram
    participant C as Client
    participant S as dr_evt_server
    C->>S: Session stream
    C->>S: InitRequest(session_name, simulation settings)
    S-->>C: InitResponse(session_id, report paths)
    Note over S: Creates one Simulation for this session
    loop Arrivals and simulation control
        C->>S: AppendJob(s) / AdvanceTo / queries
        S-->>C: Matching response and statistics
    end
    C->>S: FinishSimulationRequest
    Note over S: Drains work, writes reports, releases Simulation
    S-->>C: FinishSimulationResponse(final statistics, report paths)
    C->>S: Close stream or InitRequest for next session
```

The client sends arrivals and simulation-control requests through the session
stream. The server owns the simulation state for that session and returns the
corresponding responses. See [Client/Server Use Cases](client-server-use-cases.md)
for multi-server and digital-twin deployments.

## Building client/server support

The gRPC client/server is optional. Follow the gRPC build instructions in
[Installation](../getting-started/installation.md#cmake-configuration-options).

## Starting a server

```bash
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_server 0.0.0.0:50051
```

The address argument is optional (defaults to `0.0.0.0:50051` if
omitted). There are no other command-line options - no `--port`,
`--max_connections`, `--timeout`, `--tls_cert`, or `--verbose` flags;
the server prints one line on startup:

```
DR_EVT simulation server listening on 0.0.0.0:50051
```

and otherwise runs silently, one [`Simulation`][cpp-simulation] instance per connected
session, until stopped (e.g. `Ctrl-C`, or however your process
supervisor manages it). A streaming session starts with no job samples and
does not load the input file's data rows until requested. `InitRequest`
requires a server-readable CSV path so the simulator can validate its header;
`RunRequest` loads and executes that trace for batch or replay operation.

Bind `0.0.0.0` (shown above) so the server is reachable from other
machines; bind `127.0.0.1` instead if you only need same-machine access.

## Connecting the example client

```bash
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_client <server_host>:50051 /path/to/trace.csv
```

Both arguments are required by this *example* program (server address,
then a job data file). The client reads and streams its rows. It also sends the
path in `InitRequest`, so the current server must be able to read a CSV with a
compatible header at the same path; the server validates that header but does
not load the rows. Custom clients can use a different server-side header file
path in `InitRequest.infile`.

The API's core is the bidirectional session stream. The example client
initializes simulation settings, sends all CSV rows in one `AppendJobsRequest`,
optionally advances simulated time with `AdvanceToRequest`, and obtains status
or statistics responses.
An application can generate those messages from a live digital twin, a
database, another simulator, or any other source, so no client-side job trace
is required. The server still needs a readable CSV header during
initialization. `dr_evt_client` merely demonstrates that sequence and prints
final statistics; it is a reference for writing your own client against the
same `.proto` service. Its
[C++ source](https://github.com/LLNL/dr_evt/blob/main/src/proto/dr_evt_client.cpp)
is a complete usage example. See the full guide for what each RPC
corresponds to in the in-process [streaming API](../api/STREAMING_API.md).

For batch replay or warm start, set `InitRequest.sim_start_time` (zero for an
ordinary full run), point `infile` at the server-readable trace, and send
`RunRequest`. This is the global simulation start time, not a job's
`begin_time`. A positive simulation start time requires replay columns
including `begin_time` and `end_time`; negative and non-finite values are
rejected.

An `InitRequest` may also set `node_release_delay`, `node_release_delay_model`
(`constant`, `exponential`, or `uniform`), and `node_release_delay_spread`,
with the semantics of the matching command-line options in
[Command-Line Options](command-line.md); a zero delay keeps nodes reusable at
job end.

For a Redis-enabled server build, an `InitRequest` may set `redis_uri`,
`redis_key_prefix`, and optionally `job_flush_interval`. Both Redis strings are
required together. The simulated-job and resource-history files are then
replaced by Redis output, and `InitResponse.redis_key_prefix` identifies the
namespace. `simulated_trace_file` and `resource_trace_file` are empty in the
response. Installation, key layout, and query commands are in
[Redis Output](redis-output.md).

The Redis-enabled example client implements finalized/live status lookup:

```bash
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_client <server_host>:50051 /path/to/trace.csv \
  --redis-uri redis://127.0.0.1:6379 \
  --redis-key-prefix dr_evt:client-example \
  --advance-to 100
```

It pipelines Redis job-hash queries, asks the server only for absent IDs, and
prints the merged results in append order. The client must also be built with
`DR_EVT_WITH_REDIS=ON`; a client built without it rejects the Redis options.

## Session identity and completion

Every initialization supplies a filename-safe `session_name`. The server adds
its own unique suffix and returns the resulting session ID and report paths in
`InitResponse`. This keeps output from concurrent simulations separate even
when clients choose the same readable name.

When a client has sent its final arrival, it sends `FinishSimulationRequest`.
The server drains all submitted work, writes the session's simulated trace,
resource trace, and statistics report, then returns their paths and final
statistics in `FinishSimulationResponse`. It releases that session's
[`Simulation`][cpp-simulation] but does not stop `dr_evt_server`; the client may initialize a
new independent simulation on the same stream, or close the stream and open a
new one.

### Reusing or closing a session stream

Finish a simulation before reusing its stream. The request/response order is:

```text
InitRequest(session_name="run-a")
... AppendJob(s), AdvanceTo, and optional queries ...
FinishSimulationRequest
FinishSimulationResponse
InitRequest(session_name="run-b")   # optional: begin a new simulation on this stream
```

Each `InitRequest` after a successful finish creates a new independent
server-side [`Simulation`][cpp-simulation]. Give it a new readable `session_name`; the server
still adds a unique suffix, so names need not be globally unique. Do not send
another `InitRequest` before finishing the active simulation: the server
returns an error response for that request.

To close permanently after `FinishSimulationResponse`, use normal gRPC
bidirectional-stream shutdown: call `WritesDone()` to half-close the client
side, then call `Finish()` and check that its returned `grpc::Status` is OK.
Do not issue another request after `WritesDone()`. Closing a stream without
`FinishSimulationRequest` also ends its RPC, but it does not request report
generation or return the final simulation results; use it only to abandon a
session or after handling an earlier error.

For deployment patterns beyond one client and one server, see
[Client/Server Use Cases](client-server-use-cases.md).

## Network Configuration

### Firewall rules

Allow gRPC traffic on whatever port the server is bound to (`50051` in
the examples above):

**Linux (ufw):**
```bash
sudo ufw allow 50051/tcp
sudo ufw reload
```

**Linux (iptables):**
```bash
sudo iptables -A INPUT -p tcp --dport 50051 -j ACCEPT
sudo iptables-save
```

### SSH tunneling

The server has no built-in TLS/encryption support today - traffic is
plaintext gRPC. For access over an untrusted network, tunnel it over
SSH instead:

```bash
# On your local machine: forward local port 50051 to the remote server
ssh -L 50051:localhost:50051 user@remote-server

# In another terminal: connect the client as if the server were local
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_client localhost:50051 /path/to/trace.csv
```

## Troubleshooting

### Connection refused

**Problem:** the client reports it failed to connect.

**Solutions:**
1. Check the server is actually running: `ps aux | grep dr_evt_server`
2. Check it's listening on the port you expect: `netstat -an | grep 50051`
3. Check your firewall: `sudo ufw status`
4. Try `127.0.0.1:50051` explicitly instead of `localhost:50051`, in case of IPv4/IPv6 resolution differences

### Large trace, high memory use

If the job data file is large enough that loading it client-side (or,
separately, running the simulator in single-file batch mode) uses more
memory than you'd like, see
[`--infile_list`](command-line.md) (progressive/
multi-file loading) for the CLI/batch case - it's unrelated to the gRPC
client/server specifically, but addresses the same underlying concern.

## See Also

- [Client/Server Use Cases](client-server-use-cases.md) - bare-metal, container, multi-server, and synchronized-system patterns
- [Client/Server Reference](../CLIENT_SERVER_GUIDE.md) - service definition and wire protocol
- [Client/server examples and tests](../CLIENT_SERVER_GUIDE.md#examples-use-cases-and-tests) - runnable examples, use cases, and exact test references
- [Streaming API](../api/STREAMING_API.md) - the in-process API the gRPC service wraps
- [Command-Line Options](command-line.md) - CLI configuration options for the plain `simulator` binary
- [Python API](../api/PYTHON_API.md) - Python bindings (in-process, no network - not the same thing as a Python gRPC client)
- [Distributed client/server tests](https://github.com/LLNL/dr_evt/blob/main/tests/README.md#distributed-clientserver-tests) - exact gRPC and MPI commands and fixtures

[cpp-simulation]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4I0EN6dr_evt15BasicSimulationE

## Resource model diagnostics

Set `InitRequest.resource_model_diagnostics = true` to enable resource operation
counters for that simulation. `GetStatisticsResponse.resource_model_diagnostics`
is present only when enabled, including the statistics in `FinishSimulationResponse`.
It contains named operation call/quantity counters and `failed_fits`. Counters
are isolated between sessions and reset after a successful checkpoint load.
The existing statistics JSON file retains its aggregate-statistics schema;
read resource counters from the statistics response.
