# GitHub Actions CI/CD Workflows

This directory contains GitHub Actions workflows for automated testing.

## Workflows

### 1. `tests.yml` - Full Test Suite

**Triggers:**
- Push to `main` or `develop` branches
- Pull requests to `main` or `develop`
- Manual trigger via GitHub UI

**What it runs:**
- Scheduler correctness tests (34) - `tests/run_scheduler_correctness_tests.sh`
- Node release delay tests (34 differential + 3 option checks) -
  `tests/run_node_release_delay_tests.sh`
- Periodic backfill reference, bounded-prefix retry, and endpoint tests -
  `tests/run_periodic_backfill_tests.sh`
- Planning-horizon and backfill queue-depth tests
- Queue implementation differential tests
- Column alias tests (8)
- Run-time mode tests (8)
- Unit tests (7)
- Feature tests (8)
- Conservative backfilling comparisons (3)
- Replay tests (5: one reclamation-boundary binary and four CLI comparisons)
- Resource-history tests (5)
- Job-store tests (6)
- Config tests (14), including power-usage `trace_type`, capacity-schedule,
  and warm-start coverage
- All configured CTest registrations, including fixed periodic dispatch,
  Spur/Slurm endpoint distinctions, placement-pass accounting, planner capacity
  queries, checkpoint/restart, batch/streaming equivalence, and optional MPI
  and Python coverage
- Ser20-disabled native serialization build and tests
- Sphinx and Doxygen documentation build with warnings treated as errors
- OpenPBS harness audit tests (5, Python standard library only) and adapter
  Doxygen documentation with warnings treated as errors
- Separate OpenPBS forecast policy: native behavior/checkpoint tests and
  command-line cases in placement-pass integration, through CTest
- Python API tests
- Market package platform and job-stream tests (12)
- gRPC client/server tests (2)
- Append-job API tests and four optional gRPC checks
- FCFS/EASY backfill-window focused rerun of the four-check gRPC binary
- Synchronized single-coordinator gRPC test
- Progressive-loading tests (C++ + CLI)
- Queue-input schema test
- Redis-enabled build, isolated Redis server integration test, and real gRPC
  client Redis-first/status-fallback test
- Scale tests (7)

**Matrix:**
- GCC 14
- Clang 18
- Python 3.12

**Duration:** ~5-10 minutes

### 2. `quick-test.yml` - Quick Scheduler and Trace-Schema Check

**Triggers:**
- Push to any branch (except `main`)
- Manual trigger

**What it runs:**
- Scheduler correctness fixtures (34) - `tests/run_scheduler_correctness_tests.sh`
- Node release delay differential tests (34 + 3) - `tests/run_node_release_delay_tests.sh`
- Periodic backfill reference, bounded-prefix retry, and inclusive endpoint tests -
  `tests/run_periodic_backfill_tests.sh`
- Fixed periodic dispatch and arrival-notification tests, including the
  Spur/Slurm endpoint distinction - `tests/run_fixed_periodic_dispatch_tests.py`
- Planning-horizon reference tests - `tests/run_planning_horizon_tests.sh`
- Ser20 caller-owned-memory serialization test - installed `t_state_ser20`
- Progressive-loading C++ API test - installed `test_progressive_load`
- Queue-input schema test - installed `test_queue_input`
- Streaming append API test - installed `test_append_job_api`
- OpenPBS harness audit tests (5) -
  `python3 -m unittest discover -s validation/openpbs -p test_compare.py -v`
- Bitmap placement model and real simulator lifecycle/checkpoint integration tests, and all 60 native Fluxion allocation/reservation
  comparisons across all six strategies and both filters (`test_placement_model`,
  `compare_placement_model.py --all-optimizations`), plus portable script
  regression checks (`test_placement_scripts.py`)
- Flux placement oracle and identity audits -
  `python3 -m unittest discover -s validation/flux -p test_placement_oracle.py -v`
- OpenPBS forecast policy - installed `test_openpbs_forecast`

**Compiler:**
- GCC 14 only

**Duration:** ~2-3 minutes

**Purpose:** Fast validation for development branches

## Test Coverage

The maintained counts and runner coverage are in the
[Test Suite inventory](../../tests/README.md#inventory). CTest registration
counts depend on optional build features; inspect the configured suite with
`ctest --test-dir build -N`.

Periodic backfill has 143 checks with default intervals; fixed periodic dispatch
has 90. Both full and quick workflows run the retry and endpoint regressions.
The append API includes 41 test groups, one additional group with Ser20, and
four wire-level checks when gRPC is enabled. Full CI also runs checkpoint and
batch/streaming continuation tests. The workflow summaries report each runner's
outcome without treating overlapping checks as distinct tests.

The OpenPBS audits check the harness's capacity accounting, arrival and
duration validation, trace conversion, and forecast-fit calculation. They do
not build or run OpenPBS. The full native replay requires the pinned external
source and the supplied Lassen trace and runs separately; see the
[OpenPBS harness](../../validation/openpbs/README.md). On the original converted 2,000-job input, `fcfs_1s_evt_group` matches all
starts and completions; default `fcfs` has 1,433 differing schedules. No time
scaling is used in these comparisons.

## Status Badges

Add to main README.md:

```markdown
[![Tests](https://github.com/LLNL/dr_evt/workflows/DR_EVT%20Test%20Suite/badge.svg)](https://github.com/LLNL/dr_evt/actions)
```

## Local Testing

Run the same tests locally before pushing:

```bash
export CMAKE_INSTALL_PREFIX="${PWD}/install"
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}" \
  -DDR_EVT_BUILD_PYTHON=ON \
  -DDR_EVT_ENABLE_GRPC=ON \
  -DDR_EVT_WITH_SER20=ON \
  -DDR_EVT_WITH_UNIT_TESTING=ON
cmake --build build -j4
cmake --install build

python3 -m pip install grpcio grpcio-tools protobuf

./tests/run_scheduler_correctness_tests.sh
./tests/run_custom_scheduler_tests.sh
./tests/run_fcfs_queue_implementation_tests.sh --correctness
./tests/run_column_alias_tests.sh
./tests/run_time_mode_tests.sh
./tests/run_unit_tests.sh
./tests/run_feature_tests.sh
./tests/run_easy_vs_conservative_correctness_tests.sh
./tests/run_replay_tests.sh
./tests/run_resource_history_tests.sh
./tests/run_job_store_tests.sh
./tests/run_configs_tests.sh
./tests/run_python_tests.sh
./tests/run_grpc_tests.sh
./tests/run_append_job_tests.sh
./tests/run_backfill_window_grpc_test.sh
python3 tests/test_grpc_single_coordinator.py \
  "${CMAKE_INSTALL_PREFIX}/bin/dr_evt_server"
./tests/run_progressive_load_tests.sh
./tests/run_scale_tests.sh
ctest --test-dir build --output-on-failure

# Verify the native binary-state path remains usable without Ser20.
cmake -S . -B build-no-ser20 \
  -DDR_EVT_WITH_SER20=OFF \
  -DDR_EVT_ENABLE_PROTOBUF=OFF \
  -DDR_EVT_ENABLE_GRPC=OFF
cmake --build build-no-ser20 \
  --target t_state_rngen-bin t_state-bin -j4
./build-no-ser20/t_state_rngen
./build-no-ser20/t_state 10 42 4 2

# Validate the documentation with the same strictness as CI.
python3 -m pip install -r docs/requirements.txt
make -C docs html SPHINXOPTS="-W --keep-going"
```

There is no `run_correctness_tests.sh` in this checkout - an earlier
version of this document referenced it, but the actual scheduler_correctness/
runner is `run_scheduler_correctness_tests.sh`.

## Workflow Details

### Build Steps

1. Install dependencies (CMake, Boost, Protobuf/gRPC, MPI, Python, compilers)
2. Configure a C++20 Release build with Ser20, Python bindings, Protobuf, and
   gRPC enabled. CI exercises Ser20's FetchContent fallback because no system
   Ser20 package is installed.
3. Build with all CPU cores (`make -j$(nproc)`). You may cap it to -j2
   as defense-in-depth against the gRPC/BoringSSL FetchContent OOM
   issue; see `docs/getting-started/installation.md`)
4. Verify build artifacts exist
5. Build without Ser20 and run the native serialization tests
6. Build the Sphinx and Doxygen documentation with warnings as errors

### Test Steps

Each test category runs independently - see "Test Coverage" above for
what each actually covers and what's been verified.

### Artifacts

On test failure, uploads:
- Test output CSVs from `/tmp/`
- CMake test logs
- Retained for 7 days

## Adding New Tests

When you add a new test:

1. Add to the appropriate test category and directory
2. For `scheduler_correctness/`, add the test name to `run_scheduler_correctness_tests.sh`'s
   `TESTS` array and to `scripts/generators/generate_all_expected_outputs.py`'s
   `TESTS` list (to generate its expected output); for `scale/`, use
   `scripts/generators/generate_scale_expected_outputs.py`
3. CI will automatically pick it up via the existing runner scripts - no
   workflow file changes needed unless you're adding a wholly new
   category

## Troubleshooting

### Workflow fails but tests pass locally

- Check compiler version (the full CI matrix uses GCC 14 and Clang 18; the
  quick workflow uses GCC 14)
- Check Boost version
- Run with same flags as CI: `-DCMAKE_BUILD_TYPE=Release`

### Build fails in CI

- Check dependencies in `tests.yml`
- Check CMakeLists.txt for platform-specific issues

### `28_simultaneous_completions_backfill` fails in `run_scheduler_correctness_tests.sh`

If you're running an older copy of `run_scheduler_correctness_tests.sh`: this was a real,
known false-failure in the script's resource-trace comparison (too strict
about the internal order of simultaneous end/start events within the
same timestamp, not an actual scheduling bug) - fixed by consolidating to
the settled state per timestamp before comparing. If you still see this
on a current copy of the script, it may indicate a genuine regression -
don't assume it's the same already-fixed issue without checking.

### Tests timeout

- The `redis-integration` job sets `timeout-minutes: 15`; the other jobs use
  GitHub Actions' six-hour default.
- Add a job-level `timeout-minutes` value to the other jobs if a tighter limit
  is needed.
- Consider splitting into more jobs

## Future Enhancements

Potential additions:

- [ ] Code coverage reporting (lcov/gcov)
- [ ] Performance benchmarking
- [ ] Nightly builds with extended tests
- [ ] Docker-based builds for reproducibility
- [ ] Multi-platform testing (macOS, Windows)
- [ ] Memory leak detection (valgrind)
- [ ] Static analysis (clang-tidy, cppcheck)

## References

- **GitHub Actions docs:** https://docs.github.com/en/actions
- **Test documentation:** `../tests/README.md`
- **Testing methodology and known limitations:** `../docs/TESTING_GUIDE.md`
