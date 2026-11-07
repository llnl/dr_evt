# DR_EVT Test Suite

This page is the source of truth for test categories, runner commands, and
test counts. The [Testing Guide](../docs/TESTING_GUIDE.md) explains the
validation methodology.

## Build and run

Configure, build, and install before invoking the shell runners:

```bash
export CMAKE_INSTALL_PREFIX="${PWD}/install"
cmake -S . -B build \
  -DCMAKE_INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}" \
  -DDR_EVT_WITH_UNIT_TESTING=ON
cmake --build build -j4
cmake --install build
```

Run the ordinary CTest tests enabled by the current configuration with:

```bash
ctest --test-dir build --output-on-failure
```

The focused runners below exercise fixture suites and comparisons that are not
all registered with CTest. A fixture is a small sample input, usually a short
job trace under `test_traces/`, together with the expected output it should
produce:

```bash
./tests/run_scheduler_correctness_tests.sh
./tests/run_custom_scheduler_tests.sh
./tests/run_fcfs_queue_implementation_tests.sh --correctness
./tests/run_column_alias_tests.sh
./tests/run_time_mode_tests.sh
./tests/run_unit_tests.sh
./tests/run_feature_tests.sh
./tests/run_scale_tests.sh
./tests/run_easy_vs_conservative_correctness_tests.sh
./tests/compare_cpp_python_conservative.sh
./tests/run_replay_tests.sh
./tests/run_resource_history_tests.sh
./tests/run_job_store_tests.sh
./tests/run_redis_tests.sh
./tests/run_redis_grpc_client_test.sh
./tests/run_append_job_tests.sh
./tests/run_progressive_load_tests.sh
./tests/run_configs_tests.sh
./tests/run_python_tests.sh
./tests/run_warm_start_validation_tests.sh
./tests/run_max_time_tests.sh
./tests/run_cli_input_tests.sh
./tests/run_swf_tests.sh
./tests/run_grpc_tests.sh
./tests/run_backfill_window_grpc_test.sh
python3 tests/test_grpc_single_coordinator.py \
  "${CMAKE_INSTALL_PREFIX}/bin/dr_evt_server"
```

Every shell runner ends with one machine-readable, ANSI-free status line:

```text
<<<<<<<<<<<<<<<< TEST RESULT: PASS | run_unit_tests >>>>>>>>>>>>>>>>
<<<<<<<<<<<<<<<< TEST RESULT: FAIL | run_unit_tests | exit=1 >>>>>>>>>>>>>>>>
<<<<<<<<<<<<<<<< TEST RESULT: SKIP | run_configs_tests | simulator was built without Protobuf support >>>>>>>>>>>>>>>>
```

Detailed per-case output remains above that line. The shared reporter also
covers early failures and runs each suite's temporary-file cleanup first.

Python-driven shell runners source `select_python.sh`. Unless
`PYTHON_EXECUTABLE` is set explicitly, it probes both `python` and `python3`
(plus available versioned commands) and selects the newest compatible
interpreter by its reported version. Binding runners search the configured
install library directory and both `lib/python` and `lib64/python`.

Some runners require build options or external packages for Python bindings,
Protobuf, gRPC, or MPI. Tests for unavailable optional features are not built
or are reported as skipped.

The Redis runner requires `redis-server`, `redis-cli`, and a simulator built
with `-DDR_EVT_WITH_REDIS=ON`. It starts and stops its own loopback-only Redis
server; no pre-existing Redis service is used.

## Inventory

| Category | Count | Runner or registration | Coverage |
|---|---:|---|---|
| Scheduler correctness | 34 | `run_scheduler_correctness_tests.sh` | C++/Python schedule and resource-trace consistency |
| Custom FCFS | 8 | `run_custom_scheduler_tests.sh` | Six focused API checks plus two golden schedules, including warm-start accounting and 2,000 jobs |
| Queue implementation differential | 34 × 4 | `run_fcfs_queue_implementation_tests.sh --correctness` | Equivalent schedules across deque, multimap, block, and circular queues |
| Column aliases | 8 | `run_column_alias_tests.sh` | Accepted runtime-column aliases and missing-column rejection |
| Run-time mode | 8 | `run_time_mode_tests.sh` | Actual, limit, distribution, capping, the 1 s sampling floor, and planning behavior |
| Unit | 7 | `run_unit_tests.sh` | Basic parsing, formats, and execution |
| Feature | 8 | `run_feature_tests.sh` | Policies, modes, rejection, output formats, time-varying capacity, and replay-based warm start, including inherited waiting jobs and no-checkpoint self-consistency |
| Scale | 7 | `run_scale_tests.sh` | Workloads from 10 to 10,000 jobs |
| Conservative backfilling | 2 | two conservative runners above | Behavioral and C++/Python comparisons |
| Replay | 5 | `run_replay_tests.sh` | Resource equivalence and reclamation safety |
| Resource history | 5 | `run_resource_history_tests.sh` | Circular-buffer output and capacity handling |
| Job store | 6 | `run_job_store_tests.sh` | Capacity, growth/abort, reclamation, and statistics |
| Redis output | 1 integration runner | `run_redis_tests.sh` | Isolated server startup, CSV/hash output, time/resource indexes, namespace replacement, finalized-versus-unfinished visibility, pipelined bulk lookup, and byte-identical job and resource outputs for Redis/file runs of 200 jobs. Redis coverage runs with and without Ser20; the checkpoint portion runs only in the Ser20-enabled configuration. |
| Redis gRPC example | 1 integration runner | `run_redis_grpc_client_test.sh` | Batch append, advance, pipelined finalized-job lookup, one server fallback query, and original-order merged reporting |
| Append-job | 34 | `run_append_job_tests.sh` | 30 in-process C++ checks plus 4 optional gRPC checks, including integral streaming-limit validation, known actual runtimes, capacity-aware instantaneous/aggregate utilization and reservations, all-waiting-job status forecasts across backfill queue-depth and planning-horizon limits, periodic backfill, the event-driven planning horizon, warm start, and validation |
| Checkpoint/restart | 7 groups | CTest (`test_checkpoint_restart`) and `run_redis_tests.sh` | Exact scheduler continuation plus byte-identical file, progressive-loading, and Redis job/resource output after archive-and-stitch recovery. |
| Progressive loading | 18 | `run_progressive_load_tests.sh` | 12 C++ checks plus 6 CLI checks for multi-file loading, bounded storage, block-queue integration, periodic backfill across file boundaries, and memory checks |
| Protobuf configuration | 14 | `run_configs_tests.sh` | Configuration/CLI parity, capacity/simulation-start-time validation, and documented examples |
| Python API | 21 | `run_python_tests.sh` | 15 test functions recording 21 checks: bindings, callbacks, streaming, checkpoint/restart, monitoring, policy APIs, warm-start execution, and periodic backfill and planning options |
| gRPC client/server | 2 | `run_grpc_tests.sh` | A single server/client pair, and optionally two servers driven by two MPI clients in lockstep: each round every client reports its next job's arrival time and no client advances past the earliest one, so the two job streams keep a consistent global order |
| Multi-cluster dispatch | 2 | CTest (`test_python_performance_dispatch`, `test_mpi_performance_dispatch`) | Python/gRPC policy and native MPI coverage for app/workload sampling, CPU/GPU compatibility, runtime scaling, Turnaround, RelPerfOnly, and WaitTimeOnly placement, missing-prediction wait fallback and audit flags, adapted-limit and actual-duration wall-time policies, evaluation metrics, machine-size eligibility, and oversized-request truncation |
| Backfill-window gRPC | 4 repeated checks | `run_backfill_window_grpc_test.sh` | Focused rerun of the gRPC streaming binary; one check targets the backfill window |
| Single-coordinator gRPC | 1 | `test_grpc_single_coordinator.py` | Synchronized independent simulation servers |
| Queue input schema | 1 binary | CTest or installed `test_queue_input` | Legacy queue names or numeric queue IDs, plus accepted and rejected replay/simulation runtime invariants |
| Ser20-disabled serialization | 2 binaries | `t_state_rngen` and `t_state` | Native state serialization without Ser20 |
| Trace tools | 4 | `test_trace_tools.py` via CTest | Capacity inference, simulator-format conversion, direct schedule loading, checkpoint output stitching, and warm-start boundary/output behavior |
| Maximum time | 3 CLI cases | `run_max_time_tests.sh` | Inclusive cutoff behavior in simulation, replay, and warm-start execution |
| Warm start | 1 native binary + 9 CLI cases | CTest (`test_warm_start`, `test_warm_start_validation`) | Boundary classification, two-stage execution, runtime modes, capacity transitions, policies/queues, accounting/output, numeric/ISO simulation-start times, zero-start replay, inclusive maximum time, per-file timestamp encoding, periodic backfill anchored at the boundary, and invalid configurations |
| Node release delay | 1 native binary + 34 differential + 3 option checks | CTest (`test_node_release_delay`, `test_node_release_delay_reference`) | Planning values and sampling of the per-job delay, streaming behavior, replay and warm-start boundaries, checkpoint continuation; the 34 scheduler-correctness workloads with a constant delay against the Python reference, plus zero-delay identity, replay exclusion, and rejected values |
| Periodic backfill | 34 × 2 policies × 2 intervals differential + 2 option checks | CTest (`test_periodic_backfill_reference`) | The 34 scheduler-correctness workloads under hybrid (depth 1) and conservative backfill with 7 s and 30 s ticks against the Python conservative reference, comparing schedules and settled resource traces; plus zero-interval identity and rejected values |
| Planning horizon | 9 differential + 34 identity + 1 option check | CTest (`test_planning_horizon_reference`) | Event-driven `--planning_horizon` for easy, hybrid, conservative, and sjf against the Python references, with and without a horizon; an explicit unlimited horizon byte-identical to the default on the 34 workloads; rejected values. Periodic horizon cases live in the configuration suite. |
| SWF input | 8 CLI cases | `run_swf_tests.sh` (CTest `test_swf`) | Standard Workload Format parsing: equivalence with simple-format CSV, header-derived and overridden cores-per-node, MaxNodes as the default machine size, skipped records, malformed lines, and option consistency |
| Native CTest | 21 native, 2 Python, plus 2 with MPI | CTest | RNG and binary serialization, exact checkpoint/restart, trace policies, replay reclamation, custom scheduling, append/streaming APIs, maximum-time and warm-start coverage, node release delay, capacity parsing, queue implementations, CLI dispatch, command-line input selection, and SWF input; CTest also registers the Python trace-tools and multi-cluster dispatch tests, and the MPI streaming and MPI dispatch tests when MPI is available |

The Python reference schedulers have no capacity schedule, so the
capacity-aware reservations behind `--capacity_schedule` are covered by the
native append-job, warm-start, and feature tests rather than by a differential
comparison.

The gRPC portion of the append-job runner is skipped when gRPC support was not
built. The backfill-window runner executes the same four-check gRPC test binary
as the append-job runner, so it is a focused rerun rather than four additional
unique checks. Counts describe the checks performed by each runner; the native
CTest and focused-runner rows intentionally overlap.

## Fixture locations

- `test_traces/scheduler_correctness/`: small FCFS/EASY comparison fixtures;
- `test_traces/unit/`: parsing and basic execution fixtures;
- `test_traces/feature/`: policy, replay, and buffer fixtures;
- `test_traces/grpc/`: traces for the gRPC client/server and MPI lockstep tests;
- `test_traces/progressive/`: multi-file progressive-loading fixtures;
- `test_traces/replay/`: replay-mode fixtures;
- `test_traces/tools/`: capacity-analysis and warm-start fixtures;
- `test_traces/swf/`: Standard Workload Format fixtures and their CSV equivalents;
- `test_traces/scale/`: larger workloads; and
- `test_configs/`: Protobuf configuration fixtures.

The scheduler-correctness layout is described in its
[fixture README](test_traces/scheduler_correctness/README.md). Input and output
schemas are defined in [Input Trace Files](../docs/user-guide/trace-formats.md)
and [Output Trace Files](../docs/user-guide/output-traces.md).

## Notable suites

### Scheduler-correctness tests

Scheduler-correctness fixtures compare both the scheduled-job trace and the
resource trace with outputs from `scripts/python_reference_scheduler.py`.
These comparisons test consistency between implementations, not independent
proof of the algorithm.

### Fixed periodic EASY with strict reservation admission

The periodic-dispatch and strict-reservation runners import shared scheduling
functions from [`python_periodic_scheduler.py`](../scripts/python_periodic_scheduler.py).
The script is also a standalone reference; see the
[reference script guide](../scripts/README.md#reference-schedulers). Hand-computed
fixtures remain in the runners, and `test_python_periodic_reference` checks
the CLI's input ordering, actual runtimes, epoch times, and policy limits.

[`run_legacy_reservation_validation.py`](run_legacy_reservation_validation.py) checks fixed-session
FCFS/EASY with strict completion-before-reservation admission. It covers a
4-node head-protection example, rejected long and equal-boundary jobs, time-limit
planning, early releases, cadence, candidate limits, and 96 randomized
comparisons with an independent reference. The head-protection fixture uses
a 30-minute protected job. These tests validate the specified policy set;
their evidence scope is described in
[Realism and Known Gaps](../docs/user-guide/REALISM_AND_GAPS.md#validation-scope).

```bash
cmake -S . -B build-legacy -DCMAKE_BUILD_TYPE=Release \
  -DDR_EVT_LEGACY_RESERVATION_BARRIER=ON -DDR_EVT_WITH_UNIT_TESTING=ON
cmake --build build-legacy --target sim-bin -j2
ctest --test-dir build-legacy -R '^test_legacy_reservation_validation$' --output-on-failure
```

The CTest is registered only in legacy-barrier builds. Passing
`--flexible-simulator PATH` directly to the script adds a control against a
default-rule build, verifying that only the strict build rejects a safe overlap.
The script checks the generated `dr_evt_config.hpp` before running cases:
legacy checks run when the main binary records ON, and the flexible control
runs when its binary records OFF. Inapplicable checks are skipped.
Headers are found beside build binaries or in the installation's `include`
directory; use `--config-header` and `--flexible-config-header` for other layouts.

### Replay tests

Replay tests contain four simulation/replay CLI comparisons and the
`test_replay_reclamation` binary. The latter covers explicit, periodic, and
capacity-triggered flushing; final output; equal-time departures;
out-of-order completion; front-prefix blocking; and exactly-once output. The
CLI coverage also verifies the default and explicitly enabled replay report
sets, including byte-for-byte report contents.

Run the complete replay suite under Valgrind with:

```bash
./tests/run_replay_tests.sh --valgrind
```

### Progressive-loading tests

Progressive-loading tests exercise `--infile_list`, `Trace::load_next_file()`,
and `Simulation::run_progressive()`. Buffer behavior is documented in
[Output-Trace Buffers](../docs/dev/OUTPUT_TRACE_BUFFERS.md), and streaming API
semantics in the [C++ Streaming API](../docs/api/STREAMING_API.md).

### Wait-queue tests

```bash
./tests/run_fcfs_queue_implementation_tests.sh --correctness
./tests/benchmark_wait_queues.sh
${CMAKE_INSTALL_PREFIX}/bin/tests/test_block_queue
```

The [queue implementation runner](run_fcfs_queue_implementation_tests.sh)
compares the
scheduled-job and resource traces produced by the `circular`, `deque`,
`multimap`, and `block` FCFS queues. [`test_block_queue.cpp`](test_block_queue.cpp)
directly exercises the block queue at every supported block size.
[`benchmark_wait_queues.sh`](benchmark_wait_queues.sh)
runs the whole-simulation performance and output-equivalence benchmark documented
in [Wait Queues](../docs/dev/WAIT_QUEUES.md#benchmark-record). It measures
complete simulator runs—not isolated wait-queue operations—and its workload is
[`test_traces/scale/huge_10000jobs.csv`](test_traces/scale/huge_10000jobs.csv);
it byte-compares every C++ queue output with `deque` and compares the normalized
Python EASY and hybrid schedules with their respective C++ `deque` baselines
using a `0.001` time tolerance. The
differential runner uses the
[`scheduler_correctness`](test_traces/scheduler_correctness/) fixtures.

### Fixed periodic dispatch benchmark

[`benchmark_wait_queues.sh`](benchmark_wait_queues.sh) accepts fixed dispatch
options alongside the existing `easy`, `hybrid`, and `conservative` policy
choices; `none` is also supported for fixed dispatch. The first example uses
conservative backfill, one-second ticks plus arrival passes, and queue depth
10,000. Use a Release build with the
default `DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`:

```bash
./tests/benchmark_wait_queues.sh --backfill-policy conservative \
  --dispatch-interval 1 --dispatch-on-events --dispatch-queue-depth 10000
# EASY with strict reservation admission: use a barrier-ON binary.
SIMULATOR="$PWD/build-legacy/simulator" ./tests/benchmark_wait_queues.sh \
  --backfill-policy easy --dispatch-interval 5
```

The default trace has 2,000 jobs on a 500-node pool. One warmup precedes three
measured runs; the script reports their median, checks repeated schedule and
resource outputs are identical, and retains logs, traces, and `results.json`
in a temporary directory. Use fractional submission times to exercise arrival
passes between one-second ticks. These are simulator timings, not measurements
of production schedulers. Fixed-dispatch runs use a circular queue and unlimited
horizon. `--reservation-depth` controls hybrid protection, and `--runs` controls
the number of measured runs. Omit `--dispatch-on-events` for timer-only dispatch.
The runner automatically reads and records the barrier mode from
`dr_evt_config.hpp`; the binary determines which reservation rule is benchmarked.
Reports identify the applied priority and backfill policies, queue, and dispatch
settings, without vendor scheduler labels.
Use `--config-header PATH` for a nonstandard installation layout. Older headers
that do not record the setting require rebuilding.

### Streaming API tests

```bash
./tests/run_append_job_tests.sh
./tests/run_progressive_load_tests.sh
${CMAKE_INSTALL_PREFIX}/bin/tests/test_batch_vs_streaming
```

The [append-job runner](run_append_job_tests.sh) provides direct C++ and gRPC
`append_job()` coverage. Its C++ checks also validate Custom-FCFS
time-accounted resource area, capacity-aware instantaneous and aggregate
utilization (including a non-preemptive overcommit drain), and
prediction-horizon estimation. The cases include multiple same-time
allocations and releases, successive completion-event boundaries, the
post-replay full-capacity tail, the `U=0` fallback, future-job exclusion, and
invalid inputs. The warm-start binary independently checks integrated
effective capacity across capacity changes at and after the start boundary.
[`test_batch_vs_streaming.cpp`](test_batch_vs_streaming.cpp) compares batch and
incremental execution, while
[`run_progressive_load_tests.sh`](run_progressive_load_tests.sh) covers the
separate progressive-file input path. The runnable Python streaming example is
[`python/example_streaming.py`](../python/example_streaming.py).

### Warm-start tests

```bash
ctest --test-dir build --output-on-failure \
  -R '^(test_warm_start|test_warm_start_validation)$'
```

[`test_warm_start.cpp`](test_warm_start.cpp) verifies the replay-based two-stage
path: historical jobs seed occupancy without entering scheduled-job accounting,
ordinary jobs follow the selected runtime policy, and execution switches to
the ordinary event loop after the last warmup job completes. Its cases cover
all supported priority/backfill/queue combinations, randomized differential
workloads, fractional and simultaneous events, empty histories and tails,
capacity overcommit, a capacity change at the final warmup departure, the
zero-start full-replay path, and inclusive `max_time` cutoffs in simulation,
replay, and warm-start modes. The `actual`, `limit`, and deterministic
`distribution` cases confirm that runtime selection remains independent of
warm-start classification.

[`run_warm_start_validation_tests.sh`](run_warm_start_validation_tests.sh)
checks CLI rejection of negative and non-finite start times, invalid maximum
times, non-replay input, and progressive file lists. It requires the simulator
path when invoked directly; CTest supplies that argument automatically.

### Python API tests

After building with `DR_EVT_BUILD_PYTHON=ON`, run:

```bash
./tests/run_python_tests.sh
```

[`test_python_api.py`](test_python_api.py) exercises
configuration, single and batched job append,
time advancement, checkpoint/restart, statistics, warm-start execution,
output, and error handling.

### Checkpoint/restart tests

[`test_checkpoint_restart.cpp`](test_checkpoint_restart.cpp) exercises exact
Ser20 continuation for every standard scheduler backend and callback-based
Custom FCFS. Each scheduler runs a deterministic 256-job workload, checkpoints
with completed, running, waiting, and future jobs present, appends another 256
jobs to both the uninterrupted and restored branches, then compares their
completed schedules and statistics. Additional cases preserve the distinction
between loaded and submitted jobs, resume reclaimed job/resource output in
append mode with byte-for-byte comparison against uninterrupted output, and
reject mismatched configuration. The test is built and registered with CTest
only when Ser20 is available.

### Distributed client/server tests

After building with `DR_EVT_ENABLE_GRPC=ON`, run:

```bash
python3 -m pip install grpcio grpcio-tools protobuf
./tests/run_grpc_tests.sh
python3 tests/test_grpc_single_coordinator.py \
  "${CMAKE_INSTALL_PREFIX}/bin/dr_evt_server"
```

The [gRPC runner](run_grpc_tests.sh) covers
the example client/server pair and, when MPI is available, the multi-client,
multi-server harness. [`run_append_job_tests.sh`](run_append_job_tests.sh) also
tests `AppendJobRequest` over an actual gRPC connection, and
[`test_grpc_single_coordinator.py`](test_grpc_single_coordinator.py) covers the
synchronized independent-systems use case. The related fixtures are in
[`test_traces/grpc/`](test_traces/grpc/).

## Adding tests

Add a fixture to the directory for the behavior it covers and register it in
the corresponding runner. Keep temporary and generated output outside the
repository root.

For a scheduler-correctness fixture:

1. Add `test_traces/scheduler_correctness/<name>.csv`.
2. Add the name to `scripts/generators/generate_all_expected_outputs.py`.
3. Generate and review `<name>.expected_output.csv` and, when applicable,
   `<name>.expected_resources.csv`.
4. Add a small hand-derived `.construction.md` or `.answer.json` when direct
   inspection provides independent confidence.
5. Run `run_scheduler_correctness_tests.sh` and any other affected suite.

Regenerate expected outputs only for an intentional behavior change, and
review the generated diff before committing it.
