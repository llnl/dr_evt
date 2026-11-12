# Placement scheduling validation and performance

These commands run the main simulator on another platform. No performance
results are claimed by this script change. The earlier [standalone trials](README.md)
measure a different scope and must not be used as whole-simulator timings.

## Build and validate

Use the same Release configuration for every performance comparison. GCC,
Clang, Apple Clang and Intel compiler selection remains a CMake choice; the
scripts do not select architecture flags or change the simulator build.
For full API/checkpoint validation, configure unit testing, Ser20, Python and
gRPC, then build and install:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/install" \
  -DDR_EVT_WITH_UNIT_TESTING=ON -DDR_EVT_WITH_SER20=ON \
  -DDR_EVT_BUILD_PYTHON=ON -DDR_EVT_ENABLE_GRPC=ON \
  -DDR_EVT_LEGACY_RESERVATION_BARRIER=OFF
cmake --build build -j4
cmake --install build
ctest --test-dir build --output-on-failure
python3 -m unittest discover -s tests -p test_placement_scripts.py -v
python3 validation/flux/compare_placement_model.py \
  build/test_flux_placement_driver --all-optimizations \
  --output results/placement-fluxion.json
bash tests/run_python_tests.sh
bash tests/run_backfill_window_grpc_test.sh
bash tests/run_grpc_tests.sh
```

The oracle comparison checks exact node IDs, execution times and reservation
snapshots for all six optimization strategies and both coarse-filter settings:
720 comparisons on the saved 60-case fixture. It requires no Flux installation.
Reports retain binary/fixture hashes and failed-case context. Use a fresh output
path for each run. Use the Python interpreter matching the compiled bindings;
`PYTHON_EXECUTABLE` can select it for the Python test script.

The existing Slurm, OpenPBS and Fiction validations continue to use the same
Lassen trace and chronological selection. The copied root-level
`lassen-756-01.csv` stays untracked and is read without modification. Run the
native tools using their existing documented environments:

- [Slurm validation](../../../slurm/README.md)
- [OpenPBS validation](../../../openpbs/README.md)
- [Fiction/Fluxion validation](../../../flux/README.md)

In the prepared Fiction/Fluxion environment, the existing runner can now select
placement explicitly, with the same recorded 2,000-job input and 756 nodes:

```sh
python3 validation/flux/validate_lassen.py lassen-756-01.csv build/simulator \
  --resource-model placement --policy hybrid --scheduler-backend fluxion \
  --reservation-depth 32 --queue-depth 64 --max-jobs 2000 \
  --timeout 900 --outdir results/lassen-placement
```

`--placement-order high`, `--placement-optimizations MODE`, and
`--placement-coarse-filter` select the placement alternatives. High order also
selects the native `hinodex` matcher. Without `--resource-model placement`, the
runner preserves its existing capacity/per-pass comparison. Placement uses the
supported shared per-timestamp examination budget, while native Fluxion and
capacity/per-pass runs reset Q per pass; the manifest records this distinction.
A finite-Q workload can therefore differ even with an exact placement model.
The existing runner reports differences; it does not assert equality.

For documentation CI, install `docs/requirements.txt` in the chosen Python
environment and run the repository's no-XML Sphinx, warnings-as-errors full
build, and OpenPBS Doxygen checks described in `.github/workflows/tests.yml`.
Use the actual checkout and its existing exclusions.

## Preserve the existing benchmark input

`tests/benchmark_wait_queues.sh` keeps its existing default input:
`tests/test_traces/scale/huge_10000jobs.csv` for event-driven runs. Adding
placement does not replace that input, generate another workload, or change the
existing capacity benchmark path. An explicit TRACE_FILE remains authoritative.

```sh
SIMULATOR="$PWD/build/simulator" bash tests/benchmark_wait_queues.sh \
  --resource-model placement --runs 9 \
  --placement-output results/placement-existing-input.json
```

This runs EASY and HYBRID R32/Q32 with 500 nodes. It measures capacity, baseline
placement, each optimization independently, and all optimizations together.
Optional `--placement-coarse-filter both` measures every placement strategy with
filtering OFF and ON. The default is OFF. `--nodes N` only applies to the new
placement comparison; it does not change legacy benchmark node counts.

## Explicit seeded 20K comparison

The separate `benchmark_placement.py` requires either an existing trace or an
explicit `--generate-trace`. Generation uses the existing profile recipe:
seed 2718, four arrivals per second, requests of 1–8 nodes and limits of 1–30
seconds. The 20K input SHA256 is
`235c1fa1e259081dc207dea32f9cbecf6c723ac2c914a25b92220f91e4e17508`.

```sh
python3 validation/performance/simulation/placement-model/benchmark_placement.py \
  --binary build/simulator --generate-trace --nodes 500 --jobs 20000 \
  --runs 9 --compare-coarse-filter --output results/placement-20k-500.json
```

To use an already generated input, replace `--generate-trace --jobs 20000` with
`--trace /path/to/dr_evt_profile_jobs.csv`. The source trace is never rewritten.
This limit-runtime benchmark accepts whole-second simple CSVs with exactly
`job_submit_time,num_nodes,time_limit` columns. The raw Lassen replay schema
belongs in the existing native validation runners, not this benchmark.

At 500 nodes, this seeded input is lightly loaded and previously had no future
reservations. Measure the *same* input at 128 nodes separately to exercise
reservation costs. `--require-reservations` verifies this with an untimed
observer run; it does not instrument timed executions:

```sh
python3 validation/performance/simulation/placement-model/benchmark_placement.py \
  --binary build/simulator --generate-trace --nodes 128 --jobs 20000 \
  --runs 9 --compare-coarse-filter --require-reservations \
  --output results/placement-20k-128.json
```

Conservative is an explicit `--policy conservative` run, since protecting many
waiting jobs can be expensive. EASY/HYBRID are the default pair. To smoke-test
script behavior first, use `--generate-trace --jobs 20 --nodes 8 --runs 1` with a
fresh output path; these are not reportable performance measurements.

## Results and interpretation

Every report has a sibling `*-artifacts` directory containing the generated
input (when requested), last audited schedule/resource CSVs for each variant,
all execution logs, and available CMake/configuration files. JSON retains:

- Platform, Python version, source revision/dirty status, executable hash,
  available compiler/build settings, and optional supplied library hashes.
- Input path/hash/job count, resource count, exact commands and run order.
- Raw wall and child-CPU times, medians, and changes relative to capacity and
  baseline placement. One warmup and nine measured rounds are the defaults.
- Independent job-conservation, runtime and grouped-event capacity audits,
  output hashes, and untimed reservation-operation counts.

Wall time covers process startup/shutdown, parsing, scheduling, events and CSV
output. Audit/log writing is outside the timer. Runs are serial and their order
rotates. Finish all builds and validation before benchmarking; keep other load
and compiler settings consistent. Tiny percentage differences may be noise.

All placement strategy/filter variants must produce identical schedule and
aggregate resource-history hashes. Capacity is independently audited, but its
schedule is allowed to differ from placement. CSV hashes do not prove exact-ID
equality; the separate Fluxion oracle provides that coverage.

For capacity overhead before/after integration, optionally pass
`--capacity-baseline /path/to/pre-integration/simulator`. Build it with the same
compiler/settings and its own correct libraries. Only the two capacity modes
must match each other. Do not globally redirect both binaries to one shared
library. `--library PATH` records a library hash but does not change the loader;
`--cmake-cache`, `--config-header`, and `--build-settings` add explicit metadata
for nonstandard layouts.

Failure, timeout and interruption reports are retained. Existing reports and
artifact directories are refused rather than overwritten. Return the JSON and
its artifacts directory together so results remain reviewable.
