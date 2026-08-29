# Test Traces for DR_EVT Scheduler

This directory contains test traces for the DR_EVT job scheduler simulator.

## Directory Structure

```
test_traces/
├── correctness/          # Scheduler correctness tests (23 tests)
│   ├── *_input.csv       # Input traces (simulation mode)
│   ├── *_analytical.csv  # Hand-traced analytical oracles (19 tests)
│   └── *_oracle.csv      # Python reference outputs
├── unit/                 # Basic I/O and format tests (8 tests)
├── feature/              # Policy and mode comparison tests (5 tests)
└── scale/                # Performance/scalability tests (7 tests)
```

## Test Categories

### Correctness Tests (23 tests)
**Location:** `correctness/`  
**Purpose:** Verify scheduler algorithm correctness  
**Runner:** `../run_correctness_tests.sh`

- **19 Analytical tests:** Hand-traced ground truth for small traces (2-10 jobs)
- **4 Cross-validation tests:** Large traces (50-2000 jobs) verified by comparing C++ and Python implementations

### Unit Tests (8 tests)
**Location:** `unit/`  
**Purpose:** Basic I/O, formats, simple scenarios  
**Runner:** `../run_unit_tests.sh`

- Timestamp formats (epoch, ISO)
- Simple job traces (2-3 jobs)
- Sequential execution
- Timezone handling

### Feature Tests (5 tests)
**Location:** `feature/`  
**Purpose:** Policy and mode comparisons  
**Runner:** `../run_feature_tests.sh`

- Conservative vs EASY backfilling
- Priority policy comparisons
- Replay vs simulation modes

### Scale Tests (7 tests)
**Location:** `scale/`  
**Purpose:** Performance at different scales  
**Tests:** 10, 50, 100, 200, 500, 2000 jobs

## Additional Test Types

### Replay Tests (3 tests)
**Runner:** `../run_replay_tests.sh`

Verify replay mode reproduces simulation resource usage:
1. Run simulation → job trace + resource trace
2. Replay job trace → new resource trace
3. Compare: must match exactly

### Configuration Tests (4 tests)
**Runner:** `../run_configs_tests.sh`

Verify config files match CLI options:
- minimal_config.pb
- full_config.pb
- conservative_config.pb
- distribution_config.pb

### Streaming API Tests (4 C++ tests)
**Files:** `../test_streaming_*.cpp`

Test streaming job submission APIs.

## Running Tests

```bash
# All correctness tests (23)
./tests/run_correctness_tests.sh

# All unit tests (8)
./tests/run_unit_tests.sh

# All feature tests (13: 5 traces + 4 configs + 4 C++)
./tests/run_feature_tests.sh

# Replay tests (3)
./tests/run_replay_tests.sh

# Config tests only (4)
./tests/run_configs_tests.sh
```

## Test Status

✓ **Correctness:** 23/23 passing  
✓ **Test infrastructure:** Complete  
✓ **Documentation:** Up to date

Last updated: 2026-08-28
