# Rootless Podman containers

```
containers/podman/
├── client/Containerfile
└── server/Containerfile
```

These images run without `sudo`. Podman builds containers in a user namespace;
the runtime commands use `--userns=keep-id` so processes use your host UID/GID.
Consequently, files written into a bind mount are owned by your regular host
user instead of by root.

The server image also contains the native `simulator` executable. It can be
started with a shell instead of the gRPC server when measuring in-process
simulation performance.

## LC/Dane setup

Build and run images in an allocated compute-node shell, not on a Dane login
node. Configure Podman once per LC enclave, then verify both the shell identity
and the rootless runtime:

```bash
enable-podman
id
podman info --format '{{.Host.Security.Rootless}}'
```

The LCWEG helper may also be invoked by its full path:

```bash
/collab/usr/gapps/lcweg/containers/scripts/enable-podman.sh
```

On current LC systems this script and `/usr/bin/enable-podman` are identical;
run either one, not both.

The primary GID printed by `id` should be the account's normal primary group,
not only a project group, and the Podman command should print `true`. A primary
GID mismatch can make `newuidmap` reject the rootless user namespace.

An allocated shell may not provide a systemd user bus. Warnings about failing
to add the pause process to a systemd sandbox cgroup or move `slirp4netns` to
`user.slice` are nonfatal when the requested image or container operation
otherwise succeeds.

LC's rootless configuration maps only the invoking UID/GID. The Containerfiles
therefore run APT downloads as container root rather than switching to APT's
`_apt` sandbox user. The image remains rootless from the host's perspective.

Podman's image store is local to the node where the image was built. An image
built on a login node or a previous compute node is not automatically visible
from a new allocation. Before leaving that node, save any image that must
survive to an archive on a shared filesystem:

```bash
podman save --output dr-evt-server.tar localhost/dr-evt-server:local
podman save --output dr-evt-client.tar localhost/dr-evt-client:local
```

In a later allocation, restore the images with:

```bash
enable-podman
podman load --input dr-evt-server.tar
podman load --input dr-evt-client.tar
podman images
```

Use the fully qualified local names `localhost/dr-evt-server:local` and
`localhost/dr-evt-client:local` when running the images. If an unqualified
image is absent from the current node, Podman may apply its short-name registry
rules and try to pull an unrelated name such as
`registry.access.redhat.com/dr-evt-server:local`. A `Repo not found` error in
that case means the local image still needs to be loaded or rebuilt.

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

These are multi-stage images, so package installation appears once for the
build stage and again for the smaller runtime stage. If the working tree has
large untracked results or archives, avoid sending them to Podman by creating a
tracked-only context. This includes current modifications to tracked files but
intentionally excludes all untracked files:

```bash
TASK_CONTEXT=$(mktemp -d /tmp/dr-evt-podman-context.XXXXXX)
git ls-files -z \
  | tar --null -T - -cf - \
  | tar -xf - -C "$TASK_CONTEXT"

podman build \
  -f "$TASK_CONTEXT/containers/podman/server/Containerfile" \
  -t dr-evt-server:local "$TASK_CONTEXT"
podman build \
  -f "$TASK_CONTEXT/containers/podman/client/Containerfile" \
  -t dr-evt-client:local "$TASK_CONTEXT"
```

## Run the simulator directly (including LC performance runs)

This workflow is independent of client/server mode: it starts a shell in the
server image and runs the installed Release-build `simulator` in-process. The
client image, Podman network, and Redis container are not needed.

Run these commands from the repository root in an allocated LC compute-node
shell. Create the input and output directories on the shared filesystem and
copy the trace under a known name. Replace `/path/to/trace.csv` with the real
host path:

```bash
enable-podman
```

At the `bash-4.4$` shell prompt opened by `enable-podman`, continue with:

```bash
mkdir -p results data
cp /path/to/trace.csv data/jobs.csv
ls -l data/jobs.csv
```

Load a previously saved server image archive and confirm its exact name:

```bash
podman load --input /path/to/dr-evt-server.tar
podman image exists localhost/dr-evt-server:local
podman images localhost/dr-evt-server:local
```

`Loaded image: localhost/dr-evt-server:local` confirms a successful load. The
systemd user-bus warning described in [LC/Dane setup](#lcdane-setup) does not
invalidate that result. If no archive is available, build the server image
from the repository root instead:

```bash
podman build -f containers/podman/server/Containerfile \
  -t dr-evt-server:local .
```

Start an interactive shell with the host input mounted read-only at `/input`
and the host output directory mounted at `/data`:

```bash
podman run --interactive --tty --rm \
  --userns=keep-id \
  --volume "$PWD/data:/input:ro" \
  --volume "$PWD/results:/data" \
  --entrypoint /bin/bash \
  localhost/dr-evt-server:local
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
Use the same allocation, CPU binding, input, output settings, and simulator
options for every comparison; writing large output traces is part of the
reported elapsed time. When finished, leave the container and inspect the
host-owned results:

```bash
exit
ls -lh results/
```

## Run the client/server containers

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
  localhost/dr-evt-server:local
```

Open an interactive client shell. CSV inputs in the host `data/` directory are
available in the container at `/input`:

```bash
podman run --interactive --tty --rm \
  --network dr-evt-net --userns=keep-id \
  --volume "$PWD/data:/input:ro" \
  localhost/dr-evt-client:local
```

Inside the shell, run:

```bash
/opt/dr-evt/bin/dr_evt_client dr-evt-server:50051 /input/my-trace.csv
```

For a non-interactive smoke test, mount the repository's three-job gRPC trace
into both containers. The server reads the trace header during initialization,
while the client streams its job rows. Use a separate server name and network
alias so this test does not interfere with the server above:

```bash
SMOKE_RESULTS=$(mktemp -d /tmp/dr-evt-results.XXXXXX)
podman run --detach --rm \
  --name dr-evt-smoke-server \
  --network dr-evt-net --network-alias dr-evt-smoke-server \
  --userns=keep-id \
  --volume "$SMOKE_RESULTS:/data" \
  --volume "$PWD/tests/test_traces/grpc:/input:ro" \
  localhost/dr-evt-server:local

podman run --rm \
  --network dr-evt-net --userns=keep-id \
  --volume "$PWD/tests/test_traces/grpc:/input:ro" \
  --entrypoint /opt/dr-evt/bin/dr_evt_client \
  localhost/dr-evt-client:local \
  dr-evt-smoke-server:50051 /input/trace_a.csv

podman stop dr-evt-smoke-server
```

Successful output reports three completed jobs and a makespan of 40.

If stopping a rootless container reports that its network namespace is busy,
do not immediately reuse that network and alias for a diagnostic retry. Create
a fresh network and use a new container name and network alias; stale rootless
network state can otherwise connect a client to the terminated endpoint.

Clients requesting Redis output use `redis://dr-evt-redis:6379` as the URI and
choose a key prefix. Run the finalized/live status example with:

```bash
/opt/dr-evt/bin/dr_evt_client dr-evt-server:50051 /input/my-trace.csv \
  --redis-uri redis://dr-evt-redis:6379 \
  --redis-key-prefix dr_evt:client-example \
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
