# Client/Server Setup

DR_EVT can run as a network service, enabling remote simulation control
from any language that supports gRPC. This page covers building and connecting
a client and server. See [Client/Server Use Cases](client-server-use-cases.md)
for bare-metal, container, multi-server, and synchronized-system patterns;
MPI is documented there only as an optional test harness. See also
the [Client/Server Reference](../CLIENT_SERVER_GUIDE.md) for the service
definition, dependency resolution, and testing.

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
        C->>S: AppendJob(s) / SubmitJob / AdvanceTo / queries
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

The gRPC client/server is optional and requires two CMake flags:

```bash
cmake .. \
  -DDR_EVT_ENABLE_PROTOBUF=ON \
  -DDR_EVT_ENABLE_GRPC=ON
make dr_evt_server-bin dr_evt_client-bin
make install
```

You do **not** need to install gRPC/Protobuf system packages first: if
they aren't found via `find_package`, the build automatically falls
back to fetching and building gRPC's own source tree (slower the first
time, but works without root/sudo access - the common case on shared
HPC/cluster environments). See the full guide's
[Dependency resolution](../CLIENT_SERVER_GUIDE.md#dependency-resolution)
section for exactly what's tried and in what order.

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

and otherwise runs silently, one `Simulation` instance per connected
session, until stopped (e.g. `Ctrl-C`, or however your process
supervisor manages it). A server starts with no job samples and does not
need, load, or have prior knowledge of the workload it will receive.

Bind `0.0.0.0` (shown above) so the server is reachable from other
machines; bind `127.0.0.1` instead if you only need same-machine access.

## Connecting the example client

```bash
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_client <server_host>:50051 /path/to/trace.csv
```

Both arguments are required by this *example* program (server address,
then a job data file). The file is simply a convenient way to give the
client a set of job samples to send. It is read by the client only; it
is not a requirement of the client API and is not an input the server is
expected to load or know about.

The API's core is the bidirectional session stream. A client initializes
simulation settings, sends each arrival with `AppendJobRequest` (which
immediately enqueues it), advances simulated time with
`AdvanceToRequest`, and obtains events or statistics from the responses.
An application can generate those messages from a live digital twin, a
database, another simulator, or any other source--no input trace is
needed. `dr_evt_client` merely demonstrates that sequence and prints
final statistics; it is a reference for writing your own client against
the same `.proto` service. See the full guide for what each RPC
corresponds to in the in-process [streaming API](../api/STREAMING_API.md).

## Session identity and completion

Every initialization supplies a filename-safe `session_name`. The server adds
its own unique suffix and returns the resulting session ID and report paths in
`InitResponse`. This keeps output from concurrent simulations separate even
when clients choose the same readable name.

When a client has sent its final arrival, it sends `FinishSimulationRequest`.
The server drains all submitted work, writes the session's simulated trace,
resource trace, and statistics report, then returns their paths and final
statistics in `FinishSimulationResponse`. It releases that session's
`Simulation` but does not stop `dr_evt_server`; the client may initialize a
new independent simulation on the same stream, or close the stream and open a
new one.

### Reusing or closing a session stream

Finish a simulation before reusing its stream. The request/response order is:

```text
InitRequest(session_name="run-a")
... AppendJob(s), SubmitJob, AdvanceTo, and optional queries ...
FinishSimulationRequest
FinishSimulationResponse
InitRequest(session_name="run-b")   # optional: begin a new simulation on this stream
```

Each `InitRequest` after a successful finish creates a new independent
server-side `Simulation`. Give it a new readable `session_name`; the server
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
- [Client/Server Reference](../CLIENT_SERVER_GUIDE.md) - service definition, dependency resolution, and testing
- [Streaming API](../api/STREAMING_API.md) - the in-process API the gRPC service wraps
- [Command-Line Options](command-line.md) - CLI configuration options for the plain `simulator` binary
- [Python API](../api/PYTHON_API.md) - Python bindings (in-process, no network - not the same thing as a Python gRPC client)
