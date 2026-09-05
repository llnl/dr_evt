# gRPC Client/Server Setup

DR_EVT can run as a network service, enabling remote simulation control from any language that supports gRPC.

## Architecture

```{mermaid}
graph LR
    Client[Client<br/>Any Language] -->|gRPC Stream| Server[DR_EVT Server<br/>C++]
    Server -->|Simulation| Engine[Scheduler<br/>Engine]
    Client -->|submit_job| Server
    Client -->|advance_to| Server
    Client -->|get_stats| Server
    Server -->|Events| Client
```

**Use Cases:**
- Run simulator on HPC cluster, control from laptop
- Multi-language integration (Python, Go, Java clients with C++ server)
- Distributed testing (multiple clients, different scenarios)
- Web dashboards with real-time simulation monitoring

## Building with gRPC Support

### Prerequisites

Install gRPC and Protocol Buffers:

**Ubuntu/Debian:**
```bash
sudo apt-get install -y libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc
```

**macOS (Homebrew):**
```bash
brew install grpc protobuf
```

**From Source:**
```bash
git clone --recurse-submodules -b v1.50.0 https://github.com/grpc/grpc
cd grpc
mkdir -p cmake/build && cd cmake/build
cmake -DgRPC_INSTALL=ON -DgRPC_BUILD_TESTS=OFF ../..
make -j4
sudo make install
```

### Build DR_EVT with gRPC

```bash
cd dr_evt
mkdir build && cd build

# Configure with gRPC enabled
cmake -DDR_EVT_ENABLE_PROTOBUF=ON \
      -DDR_EVT_ENABLE_GRPC=ON \
      -DBOOST_ROOT=/path/to/boost \
      -DCMAKE_INSTALL_PREFIX=/install/path \
      ..

# Build
make -j4

# Build client and server binaries
make dr_evt_server-bin dr_evt_client-bin

# Install
make install
```

Verify:
```bash
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_server --help
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_client --help
```

## Server Setup

### Start the Server

**Basic startup:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_server --port 50051
```

**With specific configuration:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_server \
    --port 50051 \
    --max_connections 10 \
    --timeout 3600
```

**Output:**
```
DR_EVT gRPC Server starting...
Listening on: 0.0.0.0:50051
Ready to accept connections
```

### Server Options

| Option | Default | Description |
|--------|---------|-------------|
| `--port` | `50051` | Port to listen on |
| `--host` | `0.0.0.0` | Host address (0.0.0.0 = all interfaces) |
| `--max_connections` | `100` | Maximum concurrent clients |
| `--timeout` | `0` | Connection timeout (seconds, 0 = no timeout) |
| `--tls_cert` | None | TLS certificate file (optional) |
| `--tls_key` | None | TLS private key file (optional) |

### Server Logs

Enable verbose logging:
```bash
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_server --port 50051 --verbose
```

Example output:
```
[2024-09-02 10:30:15] Client connected: 127.0.0.1:54321
[2024-09-02 10:30:16] Received: submit_job(job_id=1, nodes=10)
[2024-09-02 10:30:16] Event: job_started(job_id=1, time=0)
[2024-09-02 10:30:17] Received: advance_to(time=100)
[2024-09-02 10:30:17] Event: job_completed(job_id=1, time=100)
```

### Running as a Service

**systemd (Linux):**

Create `/etc/systemd/system/dr-evt-server.service`:
```ini
[Unit]
Description=DR_EVT gRPC Simulation Server
After=network.target

[Service]
Type=simple
User=druser
WorkingDirectory=/opt/dr_evt
ExecStart=/opt/dr_evt/bin/dr_evt_server --port 50051
Restart=on-failure
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Enable and start:
```bash
sudo systemctl enable dr-evt-server
sudo systemctl start dr-evt-server
sudo systemctl status dr-evt-server
```

**Docker:**

`Dockerfile`:
```dockerfile
FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    libgrpc++1.45 libprotobuf23 libboost-all-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy DR_EVT binaries
COPY build/dr_evt_server /usr/local/bin/
COPY build/libdr_evt.so /usr/local/lib/

# Expose gRPC port
EXPOSE 50051

# Run server
CMD ["dr_evt_server", "--port", "50051", "--host", "0.0.0.0"]
```

Build and run:
```bash
docker build -t dr-evt-server .
docker run -d -p 50051:50051 --name dr-evt dr-evt-server
```

## Client Usage

### C++ Client

**Basic simulation:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_client \
    --server localhost:50051 \
    --trace trace.csv \
    --total_nodes 1000
```

**With configuration:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_client \
    --server localhost:50051 \
    --config sim_config.textproto \
    --trace workload.csv \
    --outfile results.csv
```

**Output:**
```
Connected to server: localhost:50051
Loading trace: workload.csv (5000 jobs)
Submitting jobs...
Progress: [=====>    ] 2500/5000 jobs processed
Simulation completed in 12.3s
Results written to: results.csv
```

### Client Options

| Option | Required | Description |
|--------|----------|-------------|
| `--server` | Yes | Server address (host:port) |
| `--trace` | Yes | Input trace file |
| `--config` | No | Protobuf config file |
| `--total_nodes` | No | Override total nodes |
| `--outfile` | No | Output file (default: stdout) |
| `--timeout` | No | Request timeout (seconds, default: 60) |
| `--tls` | No | Enable TLS (requires server TLS) |

### Python Client

Install gRPC Python:
```bash
pip install grpcio grpcio-tools
```

Generate Python stubs:
```bash
python -m grpc_tools.protoc \
    -I src/proto \
    --python_out=. \
    --grpc_python_out=. \
    src/proto/dr_evt.proto
```

**Example client:**
```python
import grpc
import dr_evt_pb2
import dr_evt_pb2_grpc

# Connect to server
channel = grpc.insecure_channel('localhost:50051')
stub = dr_evt_pb2_grpc.SimulatorStub(channel)

# Initialize simulation
config = dr_evt_pb2.SimulationConfig(
    total_nodes=1000,
    backfill_policy="easy",
    priority_policy="fcfs"
)
response = stub.Initialize(config)
print(f"Session ID: {response.session_id}")

# Submit job
job = dr_evt_pb2.Job(
    job_id=1,
    submit_time=0,
    time_limit=100,
    num_nodes=10
)
stub.SubmitJob(job)

# Advance simulation
stub.AdvanceTo(dr_evt_pb2.TimeRequest(time=100))

# Get statistics
stats = stub.GetStatistics(dr_evt_pb2.Empty())
print(f"Jobs completed: {stats.jobs_completed}")
print(f"Average wait time: {stats.avg_wait_time}")

channel.close()
```

### Streaming API Example

**Bidirectional streaming** for real-time event monitoring:

```python
import grpc
import dr_evt_pb2
import dr_evt_pb2_grpc

channel = grpc.insecure_channel('localhost:50051')
stub = dr_evt_pb2_grpc.SimulatorStub(channel)

# Start streaming session
def request_generator():
    # Initialize
    yield dr_evt_pb2.SimulationRequest(
        config=dr_evt_pb2.SimulationConfig(total_nodes=1000)
    )
    
    # Submit jobs
    for i in range(10):
        yield dr_evt_pb2.SimulationRequest(
            job=dr_evt_pb2.Job(
                job_id=i,
                submit_time=i*10,
                time_limit=100,
                num_nodes=10
            )
        )
    
    # Advance simulation
    yield dr_evt_pb2.SimulationRequest(
        advance_to=dr_evt_pb2.TimeRequest(time=1000)
    )

# Receive events
for response in stub.RunSimulation(request_generator()):
    if response.HasField('job_started'):
        print(f"Job {response.job_started.job_id} started at t={response.job_started.time}")
    elif response.HasField('job_completed'):
        print(f"Job {response.job_completed.job_id} completed at t={response.job_completed.time}")
    elif response.HasField('statistics'):
        print(f"Stats: {response.statistics.jobs_completed} jobs completed")
```

## Network Configuration

### Firewall Rules

Allow gRPC traffic:

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

### TLS/SSL Encryption

Generate self-signed certificate (development):
```bash
openssl req -x509 -newkey rsa:4096 -keyout server.key -out server.crt -days 365 -nodes
```

**Start server with TLS:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_server \
    --port 50051 \
    --tls_cert server.crt \
    --tls_key server.key
```

**Connect client with TLS:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_client \
    --server localhost:50051 \
    --tls \
    --tls_ca_cert server.crt \
    --trace trace.csv
```

**Python client with TLS:**
```python
import grpc

# Read certificate
with open('server.crt', 'rb') as f:
    trusted_certs = f.read()

# Create secure channel
credentials = grpc.ssl_channel_credentials(root_certificates=trusted_certs)
channel = grpc.secure_channel('localhost:50051', credentials)
stub = dr_evt_pb2_grpc.SimulatorStub(channel)
```

### SSH Tunneling

Access remote server securely via SSH tunnel:

```bash
# On local machine: create tunnel
ssh -L 50051:localhost:50051 user@remote-server

# In another terminal: use client as if local
${CMAKE_INSTALL_PREFIX}/bin/dr_evt_client --server localhost:50051 --trace trace.csv
```

## Troubleshooting

### Connection Refused

**Problem:** `Error: failed to connect to server: Connection refused`

**Solutions:**
1. Check server is running: `ps aux | grep dr_evt_server`
2. Check correct port: `netstat -an | grep 50051`
3. Check firewall: `sudo ufw status`
4. Try explicit host: `--server 127.0.0.1:50051` instead of `localhost:50051`

### Connection Timeout

**Problem:** `Error: Deadline exceeded`

**Solutions:**
1. Increase timeout: `--timeout 300`
2. Check network latency: `ping remote-server`
3. Server may be overloaded - check server logs
4. Large trace file - use streaming API instead of batch

### TLS Handshake Failed

**Problem:** `Error: SSL handshake failed`

**Solutions:**
1. Verify certificate paths: `ls -l server.crt server.key`
2. Check certificate expiration: `openssl x509 -in server.crt -noout -dates`
3. Ensure client and server both use TLS (or both don't)
4. Check hostname matches certificate CN

### Proto Version Mismatch

**Problem:** `Error: incompatible protobuf version`

**Solutions:**
1. Regenerate Python stubs from current proto files
2. Rebuild client and server from same source
3. Check protobuf version: `protoc --version`

## Performance Optimization

### Batch Job Submission

Instead of submitting jobs one-by-one:
```python
# Slow: 1000 RPCs
for job in jobs:
    stub.SubmitJob(job)

# Fast: 1 RPC with batch
stub.SubmitJobBatch(dr_evt_pb2.JobBatch(jobs=jobs))
```

### Connection Pooling

Reuse gRPC channels:
```python
# Bad: creates new connection every call
def simulate():
    channel = grpc.insecure_channel('localhost:50051')
    stub = dr_evt_pb2_grpc.SimulatorStub(channel)
    # ... use stub ...
    channel.close()

# Good: reuse channel
channel = grpc.insecure_channel('localhost:50051')
stub = dr_evt_pb2_grpc.SimulatorStub(channel)

def simulate():
    # ... use stub ...
    
# Close when done
channel.close()
```

### Compression

Enable gRPC compression for large data:
```python
channel = grpc.insecure_channel(
    'localhost:50051',
    options=[
        ('grpc.default_compression_algorithm', grpc.Compression.Gzip),
        ('grpc.grpc.default_compression_level', grpc.CompressionLevel.High),
    ]
)
```

## See Also

- [Streaming API](../api/STREAMING_API.md) - Streaming API concepts and examples
- [Command-Line Options](command-line.md) - CLI configuration options
- [Python API](../api/PYTHON_API.md) - Python bindings (in-process, no network)
