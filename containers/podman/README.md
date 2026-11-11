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
ARCHIVE_STAMP=$(date -u +%Y%m%dT%H%M%SZ)
SERVER_ARCHIVE="$PWD/dr-evt-server-$ARCHIVE_STAMP.tar"
if [[ -e "$SERVER_ARCHIVE" ]]; then
  echo "Choose a new archive name: $SERVER_ARCHIVE already exists" >&2
elif podman save --output "$SERVER_ARCHIVE" localhost/dr-evt-server:local; then
  ls -lh "$SERVER_ARCHIVE"
else
  echo "Image save failed; the allocation shell remains open" >&2
fi
```

The output paths must not already exist. Podman does not overwrite or append
to an existing Docker archive; choose a new name instead. If the client image
was also built, save it to a separate, similarly timestamped archive.

After a successful save, an optional checksum detects archive corruption. It
rereads the entire image, can take several minutes for a multi-gigabyte archive
on shared storage, and prints nothing until it finishes:

```bash
sha256sum "$SERVER_ARCHIVE" > "$SERVER_ARCHIVE.sha256"
ls -lh "$SERVER_ARCHIVE" "$SERVER_ARCHIVE.sha256"
```

Interrupting `sha256sum` does not damage the completed image archive; remove
the incomplete checksum file or rerun the checksum later.

In a later allocation, restore the images with:

```bash
enable-podman
podman load --input /path/to/dr-evt-server-TIMESTAMP.tar
podman load --input /path/to/dr-evt-client-TIMESTAMP.tar
podman images
```

`podman image exists IMAGE` is silent. Exit status 0 means the image exists,
1 means it is absent, and 125 reports a Podman error. For visible confirmation:

```bash
if podman image exists localhost/dr-evt-server:local; then
  echo "Server image exists"
  podman images localhost/dr-evt-server:local
else
  echo "Server image is missing"
fi
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
lookups. The final server image separates DR_EVT packages from optional tool
and scheduler-specific groups.
DR_EVT build, CI, MPI, Redis, Python, and documentation dependencies are always
installed. Interactive debugging and profiling tools (Clang, Valgrind, GDB,
`perf`, `strace`, and cppcheck) are included in the standard DR_EVT image.
Dependencies used only by native scheduler studies are split per scheduler and
controlled by `WITH_SLURM`, `WITH_EPFL_SLURM`, `WITH_OPENPBS`, and
`WITH_FICTION`. An enabled group installs its dependencies, clones the pinned
upstream source, and builds the native comparison software. The Fiction group
builds Flux Core, Fluxion, and Fiction. All comparison groups are enabled by
default, producing the full development and validation image.
Availability of `perf` does not override host `perf_event` security policy; LC
may restrict hardware counters.
The supported Sphinx, theme, MyST, Mermaid, and Breathe packages from
`docs/requirements.txt` are preinstalled in `/opt/dr-evt-docs-venv`.
The server image also carries Matplotlib for comparison plots. Spur remains a
source-inspection reference because this repository has no native Spur
comparison harness or reproducible build procedure.

### Build images from a clean source clone

First confirm that Podman is rootless:

```bash
podman info --format '{{.Host.Security.Rootless}}'
```

It should print `true`. If it does not, install/configure rootless Podman for
your account before continuing (this can require an administrator to assign
subuid/subgid ranges once; the commands below themselves never require sudo).

Build from a fresh clone of the current committed revision. Do not use a working
tree that contains traces, results, core files, or image archives as the build
context; `COPY . .` can otherwise appear to hang while Podman transfers
unrelated data. `--no-local` makes a self-contained clone rather than hard-linking
Git objects from the working repository:

```bash
BUILD_SOURCE=$(mktemp -d /tmp/dr-evt-podman-source.XXXXXX)
git clone --recurse-submodules --no-local . "$BUILD_SOURCE"

podman build \
  -f "$BUILD_SOURCE/containers/podman/server/Containerfile" \
  -t dr-evt-server:local "$BUILD_SOURCE"
podman build \
  -f "$BUILD_SOURCE/containers/podman/client/Containerfile" \
  -t dr-evt-client:local "$BUILD_SOURCE"

mkdir -p results data
podman network exists dr-evt-net || podman network create dr-evt-net
podman volume exists dr-evt-redis-data || podman volume create dr-evt-redis-data
```

To omit packages not required for DR_EVT build, CI, MPI, Redis, and
documentation work, empty all optional package lists in the server build:

```bash
podman build \
  --build-arg WITH_SLURM=0 \
  --build-arg WITH_EPFL_SLURM=0 \
  --build-arg WITH_OPENPBS=0 \
  --build-arg WITH_FICTION=0 \
  -f "$BUILD_SOURCE/containers/podman/server/Containerfile" \
  -t dr-evt-server:minimal "$BUILD_SOURCE"
```

Omit a switch to retain that comparison. For example, omit `WITH_SLURM=0`
while setting the other unwanted groups to `0` for only the current native
Slurm study. `WITH_EPFL_SLURM` selects the separate historical Slurm 17.11.7
simulator. RAPS has no switch because the repository does not record a complete
pinned clone/build procedure for it.

After completing the selected builds, remove the temporary clone:

```bash
rm -rf "$BUILD_SOURCE"
```

These are multi-stage images, so package installation appears once for the
build stage and again for the runtime stage. The server-only simulator workflow
does not need the client build, network, or Redis volume; omit those three
commands when they are not needed. The clone contains committed `HEAD`; commit
the desired container changes before building.

To test modifications to already tracked files before committing them, create a
tracked-only context instead. This includes tracked working-tree modifications
but deliberately excludes untracked files:

```bash
BUILD_SOURCE=$(mktemp -d /tmp/dr-evt-podman-source.XXXXXX)
while IFS= read -r -d '' path; do
  if [[ -e "$path" || -L "$path" ]]; then
    printf '%s\0' "$path"
  fi
done < <(git ls-files -z) \
  | tar --null -T - -cf - \
  | tar -xf - -C "$BUILD_SOURCE"
```

Then use the same `podman build` commands above. Add any required new file to
version control before creating this context; never fall back to passing the
entire working tree. The existence check also excludes tracked files deleted
in the current working tree.

### Verify and preserve the full server image

After a full server build succeeds, verify the installed DR_EVT tools,
scheduler comparison builds, MPI runtime, and documentation environment before
leaving the allocation:

```bash
podman run --rm --entrypoint /bin/bash \
  localhost/dr-evt-server:local -lc '
set -euo pipefail
for tool in python3 gdb clang-18 valgrind perf mpirun; do
  command -v "$tool"
done
test -x /opt/dr-evt/bin/simulator
test -x /opt/dr-evt/bin/tests/test_progressive_load
test -x /workspace/slurm-modern-install/sbin/slurmctld
test -x /workspace/slurm-install/sbin/slurmctld
test -x /workspace/openpbs-reference
test -x /workspace/install/bin/flux
test -f /workspace/fiction-build/src/emu-jobtap.so
/opt/dr-evt-docs-venv/bin/sphinx-build --version
echo "All components verified"
'
```

This checks that the expected components were installed; it does not replace
the scheduler comparison runs described later. If the build used the temporary
tag `localhost/dr-evt-server:all`, verify that tag and then preserve the
standard local name with:

```bash
podman tag localhost/dr-evt-server:all localhost/dr-evt-server:local
```

Save the verified image with the unique-name procedure in
[LC/Dane setup](#lcdane-setup) before leaving the compute node. After confirming
that the archive and optional checksum are on shared storage, dangling build
images can be removed without deleting tagged images:

```bash
podman image prune --force
```

`podman system prune --force --build` is broader: it also removes unused
containers, networks, and abandoned build objects. Use it only for a dedicated
Podman store with no other state that must be retained.

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
using the clean-clone procedure in
[Build images from a clean source clone](#build-images-from-a-clean-source-clone); the
client-image build can be omitted for this workflow.

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

### Run repository benchmarks

The server image contains the installed simulator and tools needed by the shell
and Python benchmark runners, but it does not install the repository's
`tests/benchmark*.sh` source scripts. Mount the repository read-only and give
`/tmp` a persistent host directory for logs and generated comparisons. A
container started without this mount must be exited and restarted because a
volume cannot be added to a running container:

```bash
mkdir -p results/wait-queue-benchmark
podman run --interactive --tty --rm \
  --userns=keep-id \
  --env CMAKE_INSTALL_PREFIX=/opt/dr-evt \
  --env SIMULATOR=/opt/dr-evt/bin/simulator \
  --volume "$PWD:/workspace:ro" \
  --volume "$PWD/results/wait-queue-benchmark:/tmp" \
  --workdir /workspace \
  --entrypoint /bin/bash \
  localhost/dr-evt-server:local
```

Inside the container, for example, run the 2,000-job wait-queue benchmark
against the installed Release simulator:

```bash
SIMULATOR=/opt/dr-evt/bin/simulator \
  ./tests/benchmark_wait_queues.sh \
  --backfill-policy easy \
  tests/test_traces/scale/huge_2000jobs.csv
```

The default invocation uses the 10,000-job trace and runs EASY plus hybrid
comparisons:

```bash
SIMULATOR=/opt/dr-evt/bin/simulator ./tests/benchmark_wait_queues.sh
```

The explicit `SIMULATOR` path takes precedence in the benchmark helper, so do
not also set `CMAKE_INSTALL_PREFIX`. As an equivalent alternative, omit
`SIMULATOR` and export `CMAKE_INSTALL_PREFIX=/opt/dr-evt` before running the
script.

The broader `tests/run_*.sh` shell suites also locate installed native test
binaries, clients, and servers through `CMAKE_INSTALL_PREFIX`. The container
command above sets both required paths for every command in the shell. Before
running the suites, verify the particular installed artifacts they require:

```bash
ls -l \
  /opt/dr-evt/bin/tests/test_progressive_load \
  /opt/dr-evt/bin/tests/test_append_job_api \
  /opt/dr-evt/bin/tests/test_grpc_streaming_api \
  /opt/dr-evt/bin/dr_evt_server
```

If any file is missing, stop: that image cannot run the corresponding suite
and must be rebuilt from the current Containerfile. Otherwise, use inline
values as well, so copied commands remain correct even in a container that was
started without the environment options:

```bash
CMAKE_INSTALL_PREFIX=/opt/dr-evt SIMULATOR=/opt/dr-evt/bin/simulator \
  bash tests/run_progressive_load_tests.sh
CMAKE_INSTALL_PREFIX=/opt/dr-evt SIMULATOR=/opt/dr-evt/bin/simulator \
  bash tests/run_append_job_tests.sh
```

The runner's `Expected:` diagnostic must begin with `/opt/dr-evt/`. An
`Expected: ./install/` path means `CMAKE_INSTALL_PREFIX` was unset, while
`Expected: /workspace/install/` means it was set to the wrong installation.

The server installation is configured with `DR_EVT_WITH_UNIT_TESTING=ON`, and
the image build verifies that the native progressive-loading test was
installed. CTest registration remains associated with the CMake build tree;
the multi-stage runtime image does not retain that tree. To rerun the complete
CTest/CI configuration, create the writable `/work/build` and `/work/install`
tree as shown under
[Run MPI-enabled tests](#run-mpi-enabled-tests), then run CTest against
`/work/build` and shell suites with `CMAKE_INSTALL_PREFIX=/work/install`.

An archive made before these checks were added may lack a complete verified
test installation. Use the preflight above; rebuild from the current
Containerfile when it fails, then replace the shared archive with `podman save`
before loading it in another allocation.

### Run MPI-enabled tests

Start MPI test containers with both OpenMPI 4 and OpenMPI 5/PRRTE
oversubscription settings. MPI oversubscription is a launcher policy, so pass
these environment variables to the container; it is not controlled by
`--userns` or another Podman namespace option. Use a new writable directory for
the CMake build while keeping the source mount read-only:

```bash
mkdir -p results
MPI_WORK=$(mktemp -d "$PWD/results/mpi-tests.XXXXXX")
podman run --interactive --tty --rm \
  --userns=keep-id \
  --env OMPI_MCA_rmaps_base_oversubscribe=1 \
  --env PRTE_MCA_rmaps_default_mapping_policy=:oversubscribe \
  --volume "$PWD:/workspace:ro" \
  --volume "$MPI_WORK:/work" \
  --workdir /workspace \
  --entrypoint /bin/bash \
  localhost/dr-evt-server:local
```

Inside the container, configure with the CI feature set plus Redis for a single
all-feature build, then build, install, and run the two MPI tests:

```bash
python3 -m venv /work/venv
source /work/venv/bin/activate
python -m pip install --upgrade pip
python -m pip install grpcio grpcio-tools protobuf numpy scipy

cmake -S /workspace -B /work/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/work/install \
  -DDR_EVT_BUILD_PYTHON=ON \
  -DDR_EVT_ENABLE_PROTOBUF=ON \
  -DDR_EVT_ENABLE_GRPC=ON \
  -DDR_EVT_WITH_SER20=ON \
  -DDR_EVT_WITH_REDIS=ON \
  -DDR_EVT_WITH_UNIT_TESTING=ON \
  -DPython3_EXECUTABLE="$(command -v python)"
cmake --build /work/build -j2
cmake --install /work/build
ctest --test-dir /work/build --output-on-failure \
  -R '^(test_mpi_streaming|test_mpi_performance_dispatch)$'
```

CMake enables the MPI targets automatically when it finds OpenMPI; there is no
separate DR_EVT MPI option. GitHub CI applies these same OpenMPI and PRRTE
oversubscription values to the individual MPI test commands. Setting them on
the container additionally makes manual `mpirun` commands behave consistently.

### Build the documentation

Mount the repository read-write so Doxygen XML and Sphinx HTML can be written
under `docs/`, then activate the preinstalled documentation environment. Do
not reuse the benchmark or MPI-test shell: those commands deliberately mount
`/workspace` read-only, and an existing container's mount mode cannot be
changed. Exit that container and start this one from the repository root:

```bash
podman run --interactive --tty --rm \
  --userns=keep-id \
  --volume "$PWD:/workspace:rw" \
  --workdir /workspace \
  --entrypoint /bin/bash \
  localhost/dr-evt-server:local
```

Inside the container:

```bash
cd /workspace/docs
test -w . || {
  echo "ERROR: /workspace is not writable; exit and use the documentation container command above" >&2
  exit 1
}
test -x /opt/dr-evt-docs-venv/bin/sphinx-build || {
  echo "ERROR: the image does not contain the documentation environment" >&2
  exit 1
}
make clean
make html
```

The generated site is available on the host under `docs/_build/html`.
Images built from the current Containerfile set `SPHINXBUILD` automatically.
For an older archive where `make html` reports `sphinx-build: not found`, set
it once in that shell and rerun the build:

```bash
export SPHINXBUILD=/opt/dr-evt-docs-venv/bin/sphinx-build
make html
```

If Doxygen reports that it cannot write `docs/doxygen/xml/compound.xsd`, the
repository is read-only or the container UID does not own the mounted files.
Confirm that the launch command includes both `--userns=keep-id` and the
`:rw` mount above; do not try to repair a host checkout with `chmod` or
`chown` from the container.

### Scheduler comparison sources

Enabled comparison groups clone, retain, and build their pinned source trees:

- Current native Slurm is installed under `/workspace/slurm-modern-install`; its
  pristine source and patched validation export are retained under
  `/slurm-modern-source` and `/workspace/slurm-modern-source`.
- The historical EPFL Slurm simulator is installed under
  `/workspace/slurm-install`.
- OpenPBS is built under `/workspace/openpbs-builds`, including the native
  scheduler adapter at `/workspace/openpbs-reference`.
- Flux Core and Fluxion are installed under `/workspace/install`; pristine sources are
  retained under `/sources` and the validation-overlay sources under `/workspace`.

The corresponding `validation/*/README.md` files describe the trace inputs and
commands required to execute a comparison after the native software is built.
Two references do not yet have complete automatic build support:

- RAPS requires an `external/RAPS` checkout and its project-managed
  `external/RAPS/venv`; `scripts/compare_with_raps.py` deliberately uses that
  interpreter. Its DR_EVT comparison also requires a separate build configured
  with `-DDR_EVT_LEGACY_RESERVATION_BARRIER=ON`.
- The repository models the inspected Spur scheduling loop in its fixed-dispatch
  reference tests. A native Spur build additionally requires its pinned source
  revision and any Rust version or ROCm dependencies required by that revision;
  those external components are not vendored into this image.

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
