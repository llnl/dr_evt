# DR_EVT Test Suite

Comprehensive test suite for the DR_EVT HPC Job Scheduler Simulator.

## Overview

DR_EVT has 51+ tests organized by purpose:
- **Correctness (23)** - Verify scheduler algorithm correctness
- **Unit (8)** - Basic I/O and format tests
- **Feature (13)** - Policies, modes, APIs, configurations
- **Replay (3)** - Verify replay reproduces simulation
- **Scale (7)** - Performance tests (10-2000 jobs)

## Quick Start

```bash
# Build first
cd build && cmake .. && make -j4 && cd ..

# Run all correctness tests (most important)
./tests/run_correctness_tests.sh

# Run unit tests
./tests/run_unit_tests.sh

# Run feature tests
./tests/run_feature_tests.sh

# Run replay tests
./tests/run_replay_tests.sh
```

## Test Categories

### 1. Correctness Tests (23 tests)

**Purpose:** Verify EASY backfilling scheduler produces correct results

**Location:** `test_traces/correctness/`

**Documentation:** [CORRECTNESS_TEST_METHODOLOGY.md](CORRECTNESS_TEST_METHODOLOGY.md)

**Runner:** `./tests/run_correctness_tests.sh`

**Method:**
- **19 Analytical tests:** Hand-traced ground truth for small traces (2-10 jobs)
- **4 Cross-validation tests:** Large traces verified by C++ vs Python comparison

**Status:** ✓ 23/23 passing

### 2. Unit Tests (8 tests)

**Purpose:** Verify basic I/O, parsing, and simple execution

**Location:** `test_traces/unit/`

**Documentation:** [UNIT_TESTS.md](UNIT_TESTS.md)

**Runner:** `./tests/run_unit_tests.sh`

**Tests:**
- Timestamp formats (epoch, ISO)
- Simple job traces (2-3 jobs)
- Sequential execution
- Timezone handling

### 3. Feature Tests (13 tests)

**Purpose:** Verify specific features, policies, modes, and APIs

**Location:** 
- `test_traces/feature/` (5 trace tests)
- `test_configs/` (4 config tests)
- `*.cpp` (4 C++ streaming tests)

**Documentation:** [FEATURE_TESTS.md](FEATURE_TESTS.md)

**Runner:** `./tests/run_feature_tests.sh`

**Tests:**
- Policy comparisons (Conservative vs EASY)
- Mode tests (Replay vs Simulation)
- Configuration file equivalence
- Streaming API functionality

### 4. Replay Tests (3 tests)

**Purpose:** Verify replay mode reproduces simulation resource usage

**Documentation:** [REPLAY_TESTS.md](REPLAY_TESTS.md)

**Runner:** `./tests/run_replay_tests.sh`

**Method:**
1. Run simulation → generates job trace + resource trace
2. Replay job trace → generates new resource trace
3. Compare: must match exactly

### 5. Scale Tests (7 tests)

**Purpose:** Test performance at different scales

**Location:** `test_traces/scale/`

**Tests:**
- small_10jobs.csv
- medium_50jobs.csv
- large_100jobs.csv, large_200jobs.csv
- xlarge_500jobs.csv
- huge_2000jobs.csv

## Test Infrastructure

### Test Runners

All test runners are in `tests/`:

```bash
run_correctness_tests.sh   # 23 correctness tests
run_unit_tests.sh          # 8 unit tests
run_feature_tests.sh       # 13 feature tests (calls run_configs_tests.sh)
run_configs_tests.sh       # 4 configuration tests
run_replay_tests.sh        # 3 replay tests
```

### Verification Scripts

Core verification in `scripts/`:

```bash
verify_against_analytical.py    # Main correctness verification
minimal_easy_oracle.py          # Python reference implementation
generate_all_analytical_oracles.py  # Generates analytical oracles
```

### Documentation

Test documentation in `tests/`:

```
CORRECTNESS_TEST_METHODOLOGY.md  # How correctness tests work
UNIT_TESTS.md                    # Unit test details
FEATURE_TESTS.md                 # Feature test details
REPLAY_TESTS.md                  # Replay test methodology
test_traces/README.md            # Test trace organization
```

## Running Tests

### Run Everything

```bash
# All correctness tests (most important)
./tests/run_correctness_tests.sh

# All other tests
./tests/run_unit_tests.sh
./tests/run_feature_tests.sh
./tests/run_replay_tests.sh
```

### Run Individual Test

```bash
# Correctness test (analytical)
./build/simulator tests/test_traces/correctness/bf01_basic_success_input.csv \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --duration_mode exact \
    --outfile /tmp/output.csv

# Compare against analytical oracle
python3 scripts/verify_against_analytical.py

# Unit test
./build/simulator tests/test_traces/unit/simple_basic.csv \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --duration_mode exact \
    --outfile /tmp/output.csv
```

## Test Status

| Category | Count | Status | Notes |
|----------|-------|--------|-------|
| Correctness | 23 | ✓ Passing | Analytically verified |
| Unit | 8 | ✓ Ready | Basic functionality |
| Feature | 13 | ✓ Ready | Policies, modes, APIs |
| Replay | 3 | ✓ Ready | Resource trace matching |
| Scale | 7 | ✓ Ready | 10-2000 jobs |
| **Total** | **54** | **✓** | **All infrastructure complete** |

## Prerequisites

### Build Requirements

```bash
# Build DR_EVT first
cd build
cmake ..
make -j4
cd ..
```

### Python Requirements

```bash
# For verification scripts
python3 -m pip install --upgrade pip

# No additional packages required (uses standard library)
```

## Adding New Tests

### Correctness Test (Analytical)

1. Create input: `tests/test_traces/correctness/<name>_input.csv`
2. Hand-trace execution
3. Create analytical oracle: `<name>_analytical.csv`
4. Add to `scripts/verify_against_analytical.py`
5. Run: `./tests/run_correctness_tests.sh`

### Unit Test

1. Create trace: `tests/test_traces/unit/<name>.csv`
2. Run: `./tests/run_unit_tests.sh`

### Feature Test

1. Create trace: `tests/test_traces/feature/<name>.csv`
2. Run: `./tests/run_feature_tests.sh`

## Troubleshooting

### Build Not Found

```bash
cd build
cmake ..
make -j4
cd ..
```

### Test Fails

1. Check output:
   ```bash
   ./build/simulator <input> --total_nodes 100 \
       --trace_format simple --timestamp_format epoch \
       --duration_mode exact --outfile /tmp/test.csv
   cat /tmp/test.csv
   ```

2. Compare with oracle:
   ```bash
   diff /tmp/test.csv tests/test_traces/correctness/<name>_analytical.csv
   ```

3. Check for:
   - Resource over-subscription
   - Incorrect event ordering
   - Jobs not completing

### All Tests Fail

- Rebuild: `cd build && make clean && make -j4`
- Check simulator runs: `./build/simulator --help`

## References

- **Main Documentation:** `docs/`
- **Verification:** `docs/verification/`
- **Algorithm:** `docs/development/algorithm.md`
- **User Guide:** `docs/user-guide/`

## Contributing

When adding new features:

1. Write tests first (test-driven development)
2. Add to appropriate test category
3. Run full test suite: all run_*_tests.sh scripts
4. Document in appropriate *_TESTS.md file
5. Update this README if adding new test category

## License

MIT License - See LICENSE file

Last updated: 2026-08-28
