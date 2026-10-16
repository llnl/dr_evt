# Docker client/server containers

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

The Ubuntu 24.04 build stages use GCC 13 with C++20 enabled and build Ser20
through the normal FetchContent fallback. Both images fetch and link Redis++;
the server writes Redis output and the example client performs pipelined job
lookups.

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
  --redis-key-prefix dr_evt:run42 \
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
