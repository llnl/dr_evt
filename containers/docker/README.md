# Docker containers

```
containers/docker/
├── client/Dockerfile       # interactive gRPC client shell
├── server/Dockerfile       # long-running simulation server
└── docker-compose.yml      # starts client/server plus persistent Redis
```

The containers are deliberately given narrow host filesystem access:

- `server` writes file-mode results and statistics to the host directory
  mounted at `/data` from `DR_EVT_RESULTS_DIR`. Redis-mode job and resource
  traces persist in the `redis-data` volume.
- Both containers read trace inputs from the host directory mounted read-only
  at `/input` from `DR_EVT_DATA_DIR`. The client streams the rows; the server
  reads only the CSV header during session initialization.

The server image also contains the native `simulator` executable. It can be
started with a shell instead of the gRPC server when measuring in-process
simulation performance.

The Ubuntu 24.04 build stages use GCC 13 with C++20 enabled and build Ser20
through the normal FetchContent fallback. Both images fetch and link Redis++;
the server writes Redis output and the example client performs pipelined job
lookups.

## Run the simulator directly

This workflow is independent of client/server mode: it starts a shell in the
server image and runs the installed Release-build `simulator` in-process. The
client image, Compose stack, and Redis service are not needed.

From the repository root, create the input and output directories and copy the
trace under a known name. Replace `/path/to/trace.csv` with the real host path:

```bash
mkdir -p results data
cp /path/to/trace.csv data/jobs.csv
ls -l data/jobs.csv
```

Build the server image from the repository root:

```bash
docker build -f containers/docker/server/Dockerfile \
  -t dr-evt-server:local .
docker image inspect dr-evt-server:local >/dev/null
```

Alternatively, load a previously saved Docker image archive and use the image
name printed by `docker load` in the later `docker run` command:

```bash
docker load --input /path/to/dr-evt-server.tar
docker images
```

Start an interactive shell with the host input mounted read-only at `/input`
and the host output directory mounted at `/data`. The numeric user keeps output
files owned by the current host user:

```bash
docker run --interactive --tty --rm \
  --user "$(id -u):$(id -g)" \
  --volume "$PWD/data:/input:ro" \
  --volume "$PWD/results:/data" \
  --entrypoint /bin/bash \
  dr-evt-server:local
```

The shell starts in `/data`. Confirm the mounted filename, then run the
simulator. Replace `1000` with the simulated machine's node count and add any
trace-format or scheduling options required by the experiment:

```bash
ls -l /input/jobs.csv
time /opt/dr-evt/bin/simulator /input/jobs.csv \
  --total_nodes 1000 \
  --outfile /data/jobs.out.csv \
  --resource_trace /data/resources.csv
```

The input path must match the name shown by `ls /input`; a missing or incorrect
name causes an input error. The simulator always writes a scheduled job trace
and resource trace, so the explicit `/data` paths above make both files persist
on the host. For a trace without an actual-runtime column, add
`--run_time_mode limit` to use each job's time limit as its run time.

Keep the shell open for repeated measurements so container startup is excluded.
Use the same input, output settings, and simulator options for every comparison;
writing large output traces is part of the reported elapsed time. When
finished, leave the container and inspect the host-owned results:

```bash
exit
ls -lh results/
```

## Run the client/server containers

From the repository root, create the two host directories and start the
server:

```bash
mkdir -p results data
DR_EVT_RESULTS_DIR="$PWD/results" DR_EVT_DATA_DIR="$PWD/data" \
  docker compose -f containers/docker/docker-compose.yml up --build -d server
```

Compose also starts a persistent Redis 7 service before the server. From a
gRPC client, configure:

```text
redis_uri: "redis://redis:6379"
redis_key_prefix: "dr_evt:run42"
```

The Redis port is published only on host loopback as port 6379 by default. Set
`DR_EVT_REDIS_PORT` to choose another host port. Query both CSV outputs through
the Redis container with:

```bash
docker compose -f containers/docker/docker-compose.yml exec redis \
  redis-cli GET dr_evt:run42:csv
docker compose -f containers/docker/docker-compose.yml exec redis \
  redis-cli GET dr_evt:run42:resources:csv
```

Open the interactive client shell (the server is reachable as `server:50051`):

```bash
DR_EVT_RESULTS_DIR="$PWD/results" DR_EVT_DATA_DIR="$PWD/data" \
  docker compose -f containers/docker/docker-compose.yml run --build --rm client
```

Inside that shell, files from the host's `data/` directory are available at
`/input`. For example:

```bash
/opt/dr-evt/bin/dr_evt_client server:50051 /input/my-trace.csv
```

To run the finalized/live status example through the Compose Redis service:

```bash
/opt/dr-evt/bin/dr_evt_client server:50051 /input/my-trace.csv \
  --redis-uri redis://redis:6379 \
  --redis-key-prefix dr_evt:client-example \
  --advance-to 100
```

Stop the server when finished:

```bash
docker compose -f containers/docker/docker-compose.yml down
```

The named `redis-data` volume survives `down`. Use `down --volumes` only when
you also want to delete the persisted Redis output.

Use absolute paths for `DR_EVT_RESULTS_DIR` and `DR_EVT_DATA_DIR` when running
the Compose command outside the repository root.
