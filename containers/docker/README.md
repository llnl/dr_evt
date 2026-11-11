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
Availability of `perf` does not override host `perf_event` security policy.
The supported Sphinx, theme, MyST, Mermaid, and Breathe packages from
`docs/requirements.txt` are preinstalled in `/opt/dr-evt-docs-venv`.
The server image also carries Matplotlib for comparison plots. Spur remains a
source-inspection reference because this repository has no native Spur
comparison harness or reproducible build procedure.

## Build images from a clean source clone

Build from a fresh clone of the current committed revision. Do not use a working
tree that contains traces, results, core files, or image archives as the build
context; `COPY . .` otherwise transfers unrelated data. `--no-local` makes a
self-contained clone rather than hard-linking Git objects from the working
repository:

```bash
BUILD_SOURCE=$(mktemp -d /tmp/dr-evt-docker-source.XXXXXX)
git clone --recurse-submodules --no-local . "$BUILD_SOURCE"

docker build \
  -f "$BUILD_SOURCE/containers/docker/server/Dockerfile" \
  -t dr-evt-server:local "$BUILD_SOURCE"
docker build \
  -f "$BUILD_SOURCE/containers/docker/client/Dockerfile" \
  -t dr-evt-client:local "$BUILD_SOURCE"
```

To omit packages not required for DR_EVT build, CI, MPI, Redis, and
documentation work, empty all optional package lists in the server build:

```bash
docker build \
  --build-arg WITH_SLURM=0 \
  --build-arg WITH_EPFL_SLURM=0 \
  --build-arg WITH_OPENPBS=0 \
  --build-arg WITH_FICTION=0 \
  -f "$BUILD_SOURCE/containers/docker/server/Dockerfile" \
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

The direct-simulator workflow needs only the server build. The clone contains
committed `HEAD`; commit the desired container changes before building.

To test modifications to already tracked files before committing them, create a
tracked-only context instead:

```bash
BUILD_SOURCE=$(mktemp -d /tmp/dr-evt-docker-source.XXXXXX)
while IFS= read -r -d '' path; do
  if [[ -e "$path" || -L "$path" ]]; then
    printf '%s\0' "$path"
  fi
done < <(git ls-files -z) \
  | tar --null -T - -cf - \
  | tar -xf - -C "$BUILD_SOURCE"
```

Then use the same `docker build` commands above. Add required new files to
version control before creating this context; never pass the entire working
tree. The existence check also excludes tracked files deleted in the current
working tree.

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

Build the server image with the clean-clone procedure in
[Build images from a clean source clone](#build-images-from-a-clean-source-clone); the
client-image build can be omitted. Confirm the resulting image with
`docker image inspect dr-evt-server:local`.

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

### Run repository benchmarks

The server image contains the installed simulator and tools needed by the shell
and Python benchmark runners, but it does not install the repository's
`tests/benchmark*.sh` source scripts. Mount the repository read-only and give
`/tmp` a persistent host directory for logs and generated comparisons. A
container started without this mount must be exited and restarted because a
volume cannot be added to a running container:

```bash
mkdir -p results/wait-queue-benchmark
docker run --interactive --tty --rm \
  --user "$(id -u):$(id -g)" \
  --env CMAKE_INSTALL_PREFIX=/opt/dr-evt \
  --env SIMULATOR=/opt/dr-evt/bin/simulator \
  --volume "$PWD:/workspace:ro" \
  --volume "$PWD/results/wait-queue-benchmark:/tmp" \
  --workdir /workspace \
  --entrypoint /bin/bash \
  dr-evt-server:local
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
and must be rebuilt from the current Dockerfile. Otherwise, use inline values
as well, so copied commands remain correct even in a container that was started
without the environment options:

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
test installation. Use the preflight above; rebuild from the current Dockerfile
when it fails before exporting a replacement archive.

### Run MPI-enabled tests

Start MPI test containers with both OpenMPI 4 and OpenMPI 5/PRRTE
oversubscription settings. MPI oversubscription is a launcher policy, so pass
these environment variables to the container. Use a new writable directory for
the CMake build while keeping the source mount read-only:

```bash
mkdir -p results
MPI_WORK=$(mktemp -d "$PWD/results/mpi-tests.XXXXXX")
docker run --interactive --tty --rm \
  --user "$(id -u):$(id -g)" \
  --env OMPI_MCA_rmaps_base_oversubscribe=1 \
  --env PRTE_MCA_rmaps_default_mapping_policy=:oversubscribe \
  --volume "$PWD:/workspace:ro" \
  --volume "$MPI_WORK:/work" \
  --workdir /workspace \
  --entrypoint /bin/bash \
  dr-evt-server:local
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
under `docs/`, then activate the preinstalled documentation environment:

```bash
docker run --interactive --tty --rm \
  --user "$(id -u):$(id -g)" \
  --volume "$PWD:/workspace" \
  --workdir /workspace \
  --entrypoint /bin/bash \
  dr-evt-server:local
```

Inside the container:

```bash
source /opt/dr-evt-docs-venv/bin/activate
make -C docs html
```

The generated site is available on the host under `docs/_build/html`.

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

After building both images with the clean-clone procedure above, create the
two host directories and start the server without asking Compose to rebuild
from the full repository context:

```bash
mkdir -p results data
DR_EVT_RESULTS_DIR="$PWD/results" DR_EVT_DATA_DIR="$PWD/data" \
  docker compose -f containers/docker/docker-compose.yml up -d server
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
