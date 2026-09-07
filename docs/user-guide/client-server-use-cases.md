# Client/Server Use Cases

This page describes ways to deploy DR_EVT clients and servers. The underlying
transport is gRPC, but the important boundary is the client/server session:
the client supplies arrivals and controls simulated time; a server owns one
independent simulation per session. For installation and the streaming
request sequence, see [Client/Server Setup](grpc-setup.md).

## Recommended deployment models

Run `dr_evt_server` directly under your normal process supervisor on bare
metal, or run it as a container and expose its gRPC port. In either case,
clients connect to the server address and read their workload data themselves;
the server needs only a writable directory for its result files. The
[container setup](https://github.com/LLNL/dr_evt/blob/main/containers/docker/README.md) includes a server result
mount and an interactive client with a host input-data mount.

MPI is not required for, or a primary deployment mechanism of, the gRPC
client/server service. The optional MPI launcher below is a convenience for
test and experiment orchestration.

## One client, multiple servers

One controller can open a separate session to each server. This works well for
independent sites or queues, and for digital twins that route live arrivals to
the appropriate simulation. Scheduler state and nodes are not shared between
servers.

```{mermaid}
graph LR
    Client[Client or digital-twin controller] -->|Session| ServerA[Server A]
    Client -->|Session| ServerB[Server B]
    Client -->|Session| ServerN[Server N]
    ServerA --> SimulationA[Simulation A]
    ServerB --> SimulationB[Simulation B]
    ServerN --> SimulationN[Simulation N]
```

`python/grpc_multi_client.py` demonstrates this arrangement. It reads the
sample CSV on the client, partitions rows among repeated `--server` options,
and prints one statistics row per server. The CSV is only a convenient source
of example arrivals: servers do not load it and require no prior knowledge of
the workload.

```bash
python3 -m pip install grpcio grpcio-tools protobuf
python3 python/grpc_multi_client.py \
  --jobs /shared/jobs.csv \
  --session-name twin-west \
  --server server-a:50051 \
  --server server-b:50051
```

Round-robin is the default partitioning and preserves submit-time ordering
within each server's partition. Use `--distribution contiguous` to give each
server a contiguous range instead.

## Optional: MPI test and experiment launcher

`python/grpc_mpi_launcher.py` starts one client on rank 0 and one server on
each remaining MPI rank. It discovers server addresses through MPI, passes
them to the root client, and stops the servers when the client finishes. It is
useful for test orchestration or controlled experiments; production deployments
normally start independent bare-metal processes or containers instead.

```bash
python3 -m pip install mpi4py grpcio grpcio-tools protobuf
mpirun -np 4 python3 python/grpc_mpi_launcher.py \
  --server-binary /path/to/dr_evt_server --base-port 50051 -- \
  --jobs /shared/jobs.csv --total-nodes 1000
```

This launch starts three independent servers on ports `50051` through `50053`.
The rank count is one client plus the number of servers. MPI determines
placement and endpoint discovery; it does not share scheduler state. The root
client reads the example job file and streams the arrivals, so server ranks do
not need access to that file.

Use your MPI launcher's host or hostfile options to distribute ranks across
nodes. Ranks must resolve one another's hostnames, selected TCP ports must be
reachable, and the server binary must be available on every server node.

### Test-only Slurm validation

From an allocation that permits `srun`, first build the server and install the
Python/MPI dependencies:

```bash
cmake -S . -B build -DDR_EVT_ENABLE_PROTOBUF=ON -DDR_EVT_ENABLE_GRPC=ON
cmake --build build --target dr_evt_server-bin
python3 -m venv .venv-grpc
.venv-grpc/bin/python -m pip install mpi4py grpcio grpcio-tools protobuf
```

Then run one client rank and one server rank on a single allocated node:

```bash
srun -N 1 -n 2 .venv-grpc/bin/python python/grpc_mpi_launcher.py \
  --server-binary ./build/dr_evt_server --base-port 50151 -- \
  --jobs tests/test_traces/grpc/trace_a.csv --total-nodes 100
```

For multiple nodes, request enough nodes and choose `-n` as one client plus
the requested servers--for example, `srun -N 2 -n 3` for one client and two
servers. The job path needs to be accessible only to the client rank. This is
an MPI testing workflow, not a required runtime architecture.

## Synchronized independent systems

`python/grpc_sync_coordinator.py` is a small experiment controller for
multiple independent schedulers. It reads one ordinary-job trace per system
and a long-form composite-job trace, streams ordinary jobs to their servers,
advances all servers to each composite event time, then submits the composite
fragments concurrently.

```bash
# Start one server for each address in systems.csv.
./build/dr_evt_server 127.0.0.1:50061
./build/dr_evt_server 127.0.0.1:50062

.venv-grpc/bin/python python/grpc_sync_coordinator.py \
  --systems python/examples/sync_systems.csv \
  --composites python/examples/composite_jobs.csv \
  --output composite-results.jsonl
```

`systems.csv` contains `system_id,address,trace` and optional `total_nodes`.
Its trace is a controller-side arrival source, not a server-side workload.
`composite_jobs.csv` contains one fragment per row with
`composite_id,submit_time,system_id,num_nodes,queue,time_limit`; a composite
ID must name at least two systems.

The JSON Lines output records node counts before and after each fragment, and
marks `partial_start` when only some fragments appear to start immediately.
It is observational: the coordinator does not reserve, cancel, or roll back
work on any server.
