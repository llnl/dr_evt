# Test Organization Summary

Complete overview of DR_EVT test infrastructure after reorganization.

## Test Organization (54 tests)

### By Category

```
tests/
├── test_traces/
│   ├── correctness/     # 23 tests - Scheduler correctness
│   ├── unit/            # 8 tests - Basic I/O and formats
│   ├── feature/         # 5 tests - Policy/mode comparisons
│   └── scale/           # 7 tests - Performance (10-2000 jobs)
├── test_configs/        # 4 config files
└── *.cpp                # 4 C++ streaming tests
```

### Test Counts

| Category | Count | Purpose |
|----------|-------|---------|
| Correctness | 23 | Verify scheduler algorithm correctness |
| Unit | 8 | Basic I/O, parsing, formats |
| Feature (traces) | 5 | Policy and mode comparisons |
| Feature (configs) | 4 | Config file equivalence |
| Feature (C++) | 4 | Streaming API |
| Replay | 3 | Replay reproduces simulation |
| Scale | 7 | Performance tests |
| **Total** | **54** | **Complete test suite** |

## Test Runners

All runners in `tests/`:

```bash
run_correctness_tests.sh   # 23 correctness tests
run_unit_tests.sh          # 8 unit tests
run_feature_tests.sh       # 13 feature tests
  └─ calls run_configs_tests.sh  # 4 config tests
run_replay_tests.sh        # 3 replay tests
```

## Test Documentation

| File | Purpose |
|------|---------|
| `tests/README.md` | Complete test suite overview |
| `tests/CORRECTNESS_TEST_METHODOLOGY.md` | Analytical + cross-validation methodology |
| `tests/UNIT_TESTS.md` | Unit test details |
| `tests/FEATURE_TESTS.md` | Feature test details |
| `tests/REPLAY_TESTS.md` | Replay test methodology |
| `tests/test_traces/README.md` | Test trace organization |
| `tests/TEST_ORGANIZATION_SUMMARY.md` | This file |

## Verification Scripts

In `scripts/`:

```bash
verify_against_analytical.py           # Main verification (23 tests)
python_reference_scheduler.py                 # Python reference implementation
generate_all_analytical_oracles.py     # Generate analytical oracles
```

## Test Status

✓ **All infrastructure complete**
- All test categories defined
- All runners created
- All documentation written
- All tests passing

## Quick Reference

### Run All Tests

```bash
# Correctness (most important)
./tests/run_correctness_tests.sh

# Unit
./tests/run_unit_tests.sh

# Feature
./tests/run_feature_tests.sh

# Replay
./tests/run_replay_tests.sh
```

### Test Purposes

**Correctness** - Does the scheduler produce correct results?
- 19 analytical tests (hand-traced)
- 4 cross-validation tests (C++ vs Python)

**Unit** - Does basic I/O work?
- Timestamp formats
- Simple scenarios
- File parsing

**Feature** - Do features work?
- Policy comparisons (Conservative vs EASY)
- Mode tests (Replay vs Simulation)
- Configuration files
- Streaming APIs

**Replay** - Does replay match simulation?
- Simulate → job trace + resource trace
- Replay → new resource trace
- Compare: must match

**Scale** - Performance at different scales
- 10, 50, 100, 200, 500, 2000 jobs

## Changes Made

### Reorganization (Aug 28, 2026)

**Moved:**
- 11 tests from feature/ → correctness/ (backfill, resource, priority tests)
- Removed again (were replay traces, not correctness tests)

**Created:**
- `run_correctness_tests.sh` - Correctness test runner
- `run_unit_tests.sh` - Unit test runner
- `run_feature_tests.sh` - Feature test runner
- `run_configs_tests.sh` - Config test runner (was run_protobuf_tests.sh)
- `run_replay_tests.sh` - Replay test runner
- Test methodology documentation (4 files)
- Updated all README files

**Deleted:**
- Old broken test scripts (referenced non-existent paths)
- Stale test results directory
- Improperly categorized tests

**Result:**
- Clean organization by purpose
- All tests properly documented
- All runners working
- All documentation up to date

## Test Development Workflow

### Adding New Test

1. **Correctness test:**
   - Create `correctness/<name>_input.csv`
   - Hand-trace execution
   - Create `<name>_analytical.csv`
   - Update `verify_against_analytical.py`

2. **Unit test:**
   - Create `unit/<name>.csv`
   - Run with `run_unit_tests.sh`

3. **Feature test:**
   - Create `feature/<name>.csv` or `*.cpp`
   - Update `run_feature_tests.sh` if needed

4. **Configuration test:**
   - Create `test_configs/<name>.pb`
   - Add to `run_configs_tests.sh`

### Testing New Feature

Before committing:
```bash
# Run all test categories
./tests/run_correctness_tests.sh
./tests/run_unit_tests.sh
./tests/run_feature_tests.sh
./tests/run_replay_tests.sh

# All should pass
```

## References

- **Main README:** `tests/README.md`
- **Verification:** `docs/verification/summary.md`
- **Algorithm:** `docs/development/algorithm.md`
- **Root verification summary:** `VERIFICATION_SUMMARY.md`

## Maintenance

This document updated: 2026-08-28

Update this file when:
- Adding new test category
- Changing test organization
- Adding/removing test runners
- Major test infrastructure changes
