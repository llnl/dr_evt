# gRPC Client/Server Setup

DR_EVT can run as a network service, enabling remote simulation control
from any language that supports gRPC. This page is a quick-start
summary; see the [gRPC Client/Server Guide](../CLIENT_SERVER_GUIDE.md)
for the full picture (service definition, dependency resolution, the
MPI multi-client/multi-server harness, testing).

## Architecture

```{mermaid}
graph LR
    Client[dr_evt_client<br/>or your own gRPC client] -->|gRPC Stream| Server[dr_evt_server<br/>C++]
    Server -->|Simulation| Engine[Scheduler<br/>Engine]
    Client -->|AppendJobRequest| Server
    Client -->|SubmitJobRequest| Server
    Client -->|AdvanceToRequest| Server
    Server -->|Events, statistics| Client
```

**Use cases:**
- Run the simulator on an HPC cluster, control it from a laptop
- Multi-language integration (a client in any language gRPC supports, talking to the C++ server)
- Modeling multiple independent job-arrival streams that need to stay wall-clock coordinated (see the MPI harness in the full guide)

## Building with gRPC Support

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

## Running the Server

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
supervisor manages it).

Bind `0.0.0.0` (shown above) so the server is reachable from other
machines; bind `127.0.0.1` instead if you only need same-machine access.

## Running the Client

```bash
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_client <server_host>:50051 /path/to/trace.csv
```

Both arguments are required (server address, then a job data file).
There are no other command-line options. `dr_evt_client` is a minimal
example client demonstrating genuine streaming: it reads job data
(`submit_time`, `num_nodes`, `queue`, `time_limit`) from the file
client-side only - the server never loads this file itself - appends
each job to the server via `AppendJobRequest`, submits each with
`SubmitJobRequest`, advances the simulation, and prints final
statistics. It's a reference for writing your own client against the
same `.proto` service, not a general-purpose tool with its own
configuration surface. See the full guide for what each RPC
corresponds to in the in-process [streaming API](../api/STREAMING_API.md).

There is no Python client or Python gRPC bindings shipped with this
project today - the [Python API](../api/PYTHON_API.md) is a separate,
in-process (no network) binding, unrelated to the gRPC service. Writing
a Python gRPC client is possible (gRPC supports Python), but would mean
generating your own stubs from `src/proto/dr_evt_service.proto` with
`grpc_tools.protoc` - not something this project provides out of the box.

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
[`--infile_list`](command-line.md#-l---infile_list-filename) (progressive/
multi-file loading) for the CLI/batch case - it's unrelated to the gRPC
client/server specifically, but addresses the same underlying concern.

## See Also

- [gRPC Client/Server Guide](../CLIENT_SERVER_GUIDE.md) - the full guide: service definition, dependency resolution, the MPI multi-client/multi-server harness, testing
- [Streaming API](../api/STREAMING_API.md) - the in-process API the gRPC service wraps
- [Command-Line Options](command-line.md) - CLI configuration options for the plain `simulator` binary
- [Python API](../api/PYTHON_API.md) - Python bindings (in-process, no network - not the same thing as a Python gRPC client)
