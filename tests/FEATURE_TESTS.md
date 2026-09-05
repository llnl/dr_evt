# Feature Tests

## Overview

Feature tests verify specific features, modes, policies, and configurations. These tests check that different simulator features work correctly.

## Test Categories

### 1. Policy Comparison Tests (3 tests)

Test different scheduling policies and their variants:

1. **conservative_backfill.csv** - Conservative backfilling policy
2. **conservative_vs_easy.trace** - Compare Conservative vs EASY policies
3. **priority_comparison.trace** - Compare priority policies (SJF, LJF, FCFS)

**Purpose:** Verify that different scheduling policies are implemented and produce different results.

### 2. Mode Tests (2 tests)

Test different simulation modes:

4. **sustained_replay.csv** - Replay mode (pre-recorded times)
5. **sustained_simulation.csv** - Simulation mode (computed times)

**Purpose:** Verify that replay and simulation modes work correctly.

### 3. Configuration File Tests (4 tests)

Test that config files match CLI options:

6. **minimal_config.pb** - Minimal configuration
7. **full_config.pb** - Full configuration
8. **conservative_config.pb** - Conservative policy config
9. **distribution_config.pb** - Distribution configuration

**Purpose:** Ensure all CLI options are supported via config files and behave identically.

### 4. Streaming API Tests (3 C++ tests)

Test streaming job submission APIs:

10. **test_streaming_api.cpp** - Streaming API functionality
11. **test_streaming_vs_batch.cpp** - Streaming vs batch mode comparison
12. **test_two_stream_manual.cpp** - Two-stream job submission

**Purpose:** Verify streaming job submission works correctly.

## Total: 12 Feature Tests

- 5 trace-based tests
- 4 configuration tests
- 3 C++ streaming tests

## Running Tests

```bash
# Run all feature tests
./tests/run_feature_tests.sh

# Run only config tests
./tests/run_configs_tests.sh

# Run individual trace test
./build/simulator tests/test_traces/feature/conservative_vs_easy.trace \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --outfile /tmp/output.csv

# Run C++ test (if built)
./build/test_streaming_api
```

## Test Details

### Policy Comparison Tests

These tests verify that different scheduling policies produce different scheduling decisions:

- **Conservative**: All jobs must complete before earliest queued job
- **EASY**: Only FCFS head gets reservation, others backfill freely
- **Priority**: SJF (shortest first) vs LJF (longest first) vs FCFS

### Mode Tests

- **Replay mode**: Read begin_time/end_time from trace file
- **Simulation mode**: Scheduler computes start times, uses run_time/time_limit

### Configuration File Tests

Verify that:
```bash
# These produce identical output:
./simulator input.csv --total_nodes 100 --policy easy
./simulator input.csv --config config.pb
```

## Test Locations

```
tests/test_traces/feature/
├── conservative_backfill.csv
├── conservative_vs_easy.trace
├── priority_comparison.trace
├── sustained_replay.csv
└── sustained_simulation.csv

tests/test_configs/
├── minimal_config.pb
├── full_config.pb
├── conservative_config.pb
└── distribution_config.pb

tests/
├── test_streaming_api.cpp
├── test_streaming_vs_batch.cpp
├── test_two_stream_manual.cpp
└── test_sim_deadlock.cpp
```

## Feature vs Correctness

**Feature tests** verify that different options/modes/features work.

**Correctness tests** verify that the scheduling algorithm produces correct results.

Example:
- Testing that `--policy conservative` runs → feature test
- Testing that conservative backfilling schedules jobs correctly → correctness test

## Adding Feature Tests

To add a new feature test:

1. **For new policy/mode:**
   - Create trace: `tests/test_traces/feature/<name>.csv`
   - Add to `run_feature_tests.sh`

2. **For new config option:**
   - Create config file: `tests/test_configs/<name>.pb`
   - Add test to `run_configs_tests.sh`

3. **For new API feature:**
   - Create C++ test: `tests/test_<feature>.cpp`
   - Add to CMakeLists.txt
   - Add to `run_feature_tests.sh`

## Current Status

✓ Feature test infrastructure complete
- Test runners created
- Categories defined
- Documentation complete

Ready for:
- Running existing tests
- Adding new feature tests

Last updated: 2026-08-28
