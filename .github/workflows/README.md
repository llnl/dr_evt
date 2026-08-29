# GitHub Actions CI/CD Workflows

This directory contains GitHub Actions workflows for automated testing.

## Workflows

### 1. `tests.yml` - Full Test Suite

**Triggers:**
- Push to `main`, `develop`, or `feature/*` branches
- Pull requests to `main` or `develop`
- Manual trigger via GitHub UI

**What it runs:**
- Correctness tests (23)
- Unit tests (8)
- Feature tests (13)
- Replay tests (3)
- Scale tests (7)

**Matrix:**
- GCC 11
- Clang 14

**Duration:** ~5-10 minutes

### 2. `quick-test.yml` - Quick Correctness Check

**Triggers:**
- Push to any branch (except `main`)
- Manual trigger

**What it runs:**
- Correctness tests only (23)

**Compiler:**
- GCC 11 only

**Duration:** ~2-3 minutes

**Purpose:** Fast validation for development branches

## Test Coverage

Total tests run in full suite:

| Category | Count | Description |
|----------|-------|-------------|
| Correctness | 23 | Scheduler algorithm correctness |
| Unit | 8 | Basic I/O and formats |
| Feature | 13 | Policies, modes, APIs, configs |
| Replay | 3 | Replay vs simulation |
| Scale | 7 | Performance (10-2000 jobs) |
| **Total** | **54** | **Complete test suite** |

## Status Badges

Add to main README.md:

```markdown
[![Tests](https://github.com/LLNL/dr_evt/workflows/DR_EVT%20Test%20Suite/badge.svg)](https://github.com/LLNL/dr_evt/actions)
```

## Local Testing

Run the same tests locally before pushing:

```bash
# Full test suite
./tests/run_correctness_tests.sh
./tests/run_unit_tests.sh
./tests/run_feature_tests.sh
./tests/run_replay_tests.sh

# Quick check (correctness only)
./tests/run_correctness_tests.sh
```

## Workflow Details

### Build Steps

1. Install dependencies (CMake, Boost, Python, compilers)
2. Configure CMake with Release build
3. Build with all CPU cores (`make -j$(nproc)`)
4. Verify build artifacts exist

### Test Steps

Each test category runs independently:
- Correctness: 23 analytical + cross-validation tests
- Unit: 8 basic I/O tests
- Feature: 5 traces + 4 configs + 4 C++ tests
- Replay: 3 simulation vs replay tests
- Scale: 7 performance tests (optional, allowed to fail)

### Artifacts

On test failure, uploads:
- Test output CSVs from `/tmp/`
- CMake test logs
- Retained for 7 days

## Adding New Tests

When you add a new test:

1. Add to appropriate test category
2. Update test runner script
3. CI will automatically run it
4. No workflow changes needed

## Troubleshooting

### Workflow fails but tests pass locally

- Check compiler version (CI uses GCC 11 / Clang 14)
- Check Boost version
- Run with same flags as CI: `-DCMAKE_BUILD_TYPE=Release`

### Build fails in CI

- Check dependencies in `tests.yml`
- Verify all submodules checked out
- Check CMakeLists.txt for platform-specific issues

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
- **Verification methodology:** `../tests/CORRECTNESS_TEST_METHODOLOGY.md`
