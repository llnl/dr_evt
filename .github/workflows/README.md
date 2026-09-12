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
- Queue implementation differential tests
- Column alias tests (8)
- Run-time mode tests (7)
- Unit tests (7)
- Feature tests (6)
- Conservative backfilling tests (2)
- Replay tests (5: one reclamation-boundary binary and four CLI comparisons)
- Resource-history tests (5)
- Job-store tests (6)
- Config tests, including power-usage `trace_type` coverage
- Core CTest tests (4: RNG generation/state restoration and power usage)
- Python API tests (16)
- gRPC client/server tests (2)
- Append-job tests (C++ + gRPC)
- FCFS/EASY backfill-window gRPC wire test
- Progressive-loading tests (C++ + CLI)
- Queue-input schema test
- Scale tests (7, optional/`continue-on-error`)

**Matrix:**
- GCC 11
- Clang 14

**Duration:** ~5-10 minutes

### 2. `quick-test.yml` - Quick Comprehensive Check

**Triggers:**
- Push to any branch (except `main`)
- Manual trigger

**What it runs:**
- Comprehensive tests only (34) - `tests/run_scheduler_correctness_tests.sh`

**Compiler:**
- GCC 11 only

**Duration:** ~2-3 minutes

**Purpose:** Fast validation for development branches

## Test Coverage

Total tests referenced by the full suite:

| Category | Count | Verified in this doc pass? |
|----------|-------|------------------------------|
| Scheduler correctness | 34 | CI runner |
| Queue implementation differential | 34 fixtures × 4 implementations | CI runner |
| Column aliases | 8 | CI runner |
| Run-time mode | 7 | CI runner |
| Unit | 7 | CI runner |
| Feature | 6 | CI runner |
| Conservative | 2 | CI runner |
| Replay | 5 | CI runner; reclamation safety plus resource equivalence |
| Resource history | 5 | CI runner |
| Job store | 6 | CI runner |
| Config | 9 | CI runner; includes power-usage `trace_type` coverage |
| Core CTest | 4 | CI runner; RNG generation/state restoration, policy/storage, and CLI dispatch coverage |
| Python API | 16 | CI runner |
| gRPC client/server | 2 | CI runner |
| Append-job | 18 C++ + gRPC | CI runner |
| FCFS/EASY backfill-window gRPC | 1 | CI runner |
| Progressive loading | 11 C++ + 4 CLI | CI runner |
| Queue input schema | 1 binary | CI runner |
| Scale | 7 | CI runner; optional/`continue-on-error` |

The workflow summary in `tests.yml` is the authoritative CI-oriented list.
See `docs/TESTING_GUIDE.md` for the fuller test catalog and the distinction
between individual assertions, fixtures, binaries, and runner-level counts.

## Status Badges

Add to main README.md:

```markdown
[![Tests](https://github.com/LLNL/dr_evt/workflows/DR_EVT%20Test%20Suite/badge.svg)](https://github.com/LLNL/dr_evt/actions)
```

## Local Testing

Run the same tests locally before pushing:

```bash
cd build && cmake .. && make -j4
cd ..

./tests/run_scheduler_correctness_tests.sh
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
./tests/run_progressive_load_tests.sh
./tests/run_scale_tests.sh
```

There is no `run_correctness_tests.sh` in this checkout - an earlier
version of this document referenced it, but the actual scheduler_correctness/
runner is `run_scheduler_correctness_tests.sh`.

## Workflow Details

### Build Steps

1. Install dependencies (CMake, Boost, Protobuf/gRPC, MPI, Python, compilers)
2. Configure a Release build with Python bindings, Protobuf, and gRPC enabled
3. Build with all CPU cores (`make -j$(nproc)`). You may cap it to -j2
   as defense-in-depth against the gRPC/BoringSSL FetchContent OOM
   issue; see `docs/getting-started/installation.md`)
4. Verify build artifacts exist

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

- Check compiler version (CI matrix uses GCC 11 and Clang 14)
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

- Default timeout: 30 minutes per job
- Increase with `timeout-minutes: 60` if needed
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
