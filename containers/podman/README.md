# Rootless Podman client/server containers

```
containers/podman/
├── client/Containerfile
└── server/Containerfile
```

These images run without `sudo`. Podman builds containers in a user namespace;
the runtime commands use `--userns=keep-id` so processes use your host UID/GID.
Consequently, files written into a bind mount are owned by your regular host
user instead of by root.

The Ubuntu 24.04 build stages use GCC 13 with C++20 enabled and build Ser20
through the normal FetchContent fallback. Both images fetch and link Redis++;
the server writes Redis output and the example client performs pipelined job
lookups.

First confirm that Podman is rootless:

```bash
podman info --format '{{.Host.Security.Rootless}}'
```

It should print `true`. If it does not, install/configure rootless Podman for
your account before continuing (this can require an administrator to assign
subuid/subgid ranges once; the commands below themselves never require sudo).

From the repository root, build the images and make host directories:

```bash
mkdir -p results data
podman build -f containers/podman/server/Containerfile -t dr-evt-server:local .
podman build -f containers/podman/client/Containerfile -t dr-evt-client:local .
podman network exists dr-evt-net || podman network create dr-evt-net
podman volume exists dr-evt-redis-data || podman volume create dr-evt-redis-data
```

Start Redis on the private network. Its port is published only on host
loopback so `redis-cli` can query results locally:

```bash
podman run --detach --rm --name dr-evt-redis \
  --network dr-evt-net \
  --publish 127.0.0.1:6379:6379 \
  --volume dr-evt-redis-data:/data \
  docker.io/library/redis:7-alpine redis-server --appendonly yes
```

Start the server. Its `/data` directory is a bind mount of `results/`, so
file-mode results and statistics persist on the host under your UID. Redis-mode
job and resource traces persist in `dr-evt-redis-data`:

```bash
podman run --detach --rm --name dr-evt-server \
  --network dr-evt-net --userns=keep-id \
  --publish 50051:50051 \
  --volume "$PWD/results:/data" \
  --volume "$PWD/data:/input:ro" \
  dr-evt-server:local
```

Open an interactive client shell. CSV inputs in the host `data/` directory are
available in the container at `/input`:

```bash
podman run --interactive --tty --rm \
  --network dr-evt-net --userns=keep-id \
  --volume "$PWD/data:/input:ro" \
  dr-evt-client:local
```

Inside the shell, run:

```bash
/opt/dr-evt/bin/dr_evt_client dr-evt-server:50051 /input/my-trace.csv
```

Clients requesting Redis output use `redis://dr-evt-redis:6379` as the URI and
choose a key prefix. Run the finalized/live status example with:

```bash
/opt/dr-evt/bin/dr_evt_client dr-evt-server:50051 /input/my-trace.csv \
  --redis-uri redis://dr-evt-redis:6379 \
  --redis-key-prefix dr_evt:run42 \
  --advance-to 100
```

Job and resource CSV data can then be queried from the host:

```bash
podman exec dr-evt-redis redis-cli GET dr_evt:run42:csv
podman exec dr-evt-redis redis-cli GET dr_evt:run42:resources:csv
```

Stop the server when finished:

```bash
podman stop dr-evt-server dr-evt-redis
```

On SELinux-enforcing hosts, append `:Z` to each bind mount (for example,
`--volume "$PWD/results:/data:Z"`) so Podman can relabel it for container use.
