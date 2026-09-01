# GitHub Actions CI/CD Workflows

This directory contains GitHub Actions workflows for automated testing.

## Workflows

### 1. `tests.yml` - Full Test Suite

**Triggers:**
- Push to `main`, `develop`, or `feature/*` branches
- Pull requests to `main` or `develop`
- Manual trigger via GitHub UI

**What it runs:**
- Comprehensive tests (34) - `tests/test_all_dr_evt.sh`
- Unit tests (7, 2 known-broken - see `tests/README.md`)
- Feature tests (3)
- Replay tests (3)
- Config tests (4, skipped without `-DDR_EVT_ENABLE_PROTOBUF=ON`)
- Python API tests (9, requires `-DDR_EVT_BUILD_PYTHON=ON` - not verified
  as part of this documentation pass, since that build option wasn't set
  up in the environment used to check these workflows)
- Streaming API tests (4, requires building the `test_streaming_api`
  target - also not verified as part of this pass)
- Scale tests (7, optional/`continue-on-error`; 4 working, 3 have
  known-corrupted input data - see `tests/README.md`)

**Matrix:**
- GCC 11
- Clang 14 (untested as part of this pass - only GCC was verified)

**Duration:** ~5-10 minutes

### 2. `quick-test.yml` - Quick Comprehensive Check

**Triggers:**
- Push to any branch (except `main`)
- Manual trigger

**What it runs:**
- Comprehensive tests only (34) - `tests/test_all_dr_evt.sh`

**Compiler:**
- GCC 11 only

**Duration:** ~2-3 minutes

**Purpose:** Fast validation for development branches

## Test Coverage

Total tests referenced by the full suite:

| Category | Count | Verified in this doc pass? |
|----------|-------|------------------------------|
| Comprehensive | 34 | Yes - 34/34 pass |
| Unit | 7 | Yes - 5/7 pass, 2 known-broken |
| Feature | 3 | Yes - 3/3 pass |
| Replay | 3 | Yes - 3/3 pass |
| Config | 4 | No - requires protobuf build |
| Python API | 9 | No - requires Python bindings build |
| Streaming API | 4 | No - requires building `test_streaming_api` |
| Scale | 7 | Yes - 4/7 pass, 3 known-corrupted input |

This table reflects a single manual verification pass, not a
continuously re-run check - there's no guarantee these numbers stay
accurate as the code changes. See `tests/README.md` for the underlying
detail and known limitations.

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

./tests/test_all_dr_evt.sh       # comprehensive/ (34 tests)
./tests/run_unit_tests.sh        # unit/ (7 tests, 2 known-broken)
./tests/run_feature_tests.sh     # feature/ (3 tests)
./tests/run_replay_tests.sh      # replay methodology (3 tests)
./tests/run_configs_tests.sh     # config tests (requires protobuf build)
./tests/run_python_tests.sh      # Python API (requires -DDR_EVT_BUILD_PYTHON=ON)
./tests/run_streaming_tests.sh   # C++ streaming API (requires test_streaming_api target)
```

There is no `run_correctness_tests.sh` in this checkout - an earlier
version of this document referenced it, but the actual comprehensive/
runner is `test_all_dr_evt.sh`.

## Workflow Details

### Build Steps

1. Install dependencies (CMake, Boost, Python, compilers)
2. Configure CMake with Release build
3. Build with all CPU cores (`make -j$(nproc)`)
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
2. For `comprehensive/`, add the test name to `test_all_dr_evt.sh`'s
   `TESTS` array and to `scripts/generators/generate_all_expected_outputs.py`'s
   `TESTS` list (to generate its expected output); for `scale/`, use
   `scripts/generators/generate_scale_expected_outputs.py`
3. CI will automatically pick it up via the existing runner scripts - no
   workflow file changes needed unless you're adding a wholly new
   category

## Troubleshooting

### Workflow fails but tests pass locally

- Check compiler version (CI uses GCC 11 / Clang 14 - only GCC has
  actually been verified as part of the most recent documentation pass)
- Check Boost version
- Run with same flags as CI: `-DCMAKE_BUILD_TYPE=Release`

### Build fails in CI

- Check dependencies in `tests.yml`
- Check CMakeLists.txt for platform-specific issues

### `28_simultaneous_completions_backfill` fails in `test_all_dr_evt.sh`

If you're running an older copy of `test_all_dr_evt.sh`: this was a real,
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
- [ ] Actually verify the Clang build and the Python/Streaming API test
      steps, none of which were checked as part of the most recent
      documentation pass

## References

- **GitHub Actions docs:** https://docs.github.com/en/actions
- **Test documentation:** `../tests/README.md`
- **Testing methodology and known limitations:** `../docs/TESTING_GUIDE.md`
