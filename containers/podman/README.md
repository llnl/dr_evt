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
```

Start the server. Its `/data` directory is a bind mount of `results/`, so
simulated traces and statistics persist on the host under your UID:

```bash
podman run --detach --rm --name dr-evt-server \
  --network dr-evt-net --userns=keep-id \
  --publish 50051:50051 \
  --volume "$PWD/results:/data" \
  dr-evt-server:local
```

Open an interactive client shell. CSV inputs in the host `data/` directory are
available in the container at `/data`:

```bash
podman run --interactive --tty --rm \
  --network dr-evt-net --userns=keep-id \
  --volume "$PWD/data:/data" \
  dr-evt-client:local
```

Inside the shell, run:

```bash
/opt/dr-evt/bin/dr_evt_client dr-evt-server:50051 /data/my-trace.csv
```

Stop the server when finished:

```bash
podman stop dr-evt-server
```

On SELinux-enforcing hosts, append `:Z` to each bind mount (for example,
`--volume "$PWD/results:/data:Z"`) so Podman can relabel it for container use.
