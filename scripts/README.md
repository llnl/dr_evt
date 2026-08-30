# Scripts Directory

Python scripts for testing, verification, and test data generation for DR_EVT.

## Overview

This directory contains scripts that support the correctness testing infrastructure. The scripts fall into three categories:

1. **Testing & Verification** - Used actively in CI and test runners
2. **Test Data Generation** - One-time generators for creating test inputs and ground truth
3. **Development Tools** - Helper utilities for debugging and manual verification

## Testing & Verification Scripts

These scripts are actively used by the test suite and CI.

### verify_against_analytical.py

**Main correctness test runner** - verifies both C++ and Python implementations.

**Purpose:**
- Runs all 23 correctness tests
- Phase 1: Verifies 19 small tests against hand-traced analytical oracles
- Phase 2: Cross-validates 4 large tests (C++ vs Python)

**Usage:**
```bash
python3 scripts/verify_against_analytical.py
```

**Called by:** `tests/run_correctness_tests.sh`

**How it works:**
1. For each test with an `*_analytical.csv` oracle:
   - Runs DR_EVT simulator on input
   - Runs Python reference implementation on input
   - Compares both outputs against analytical oracle
   - Reports pass/fail for each

2. For large tests (no analytical oracle):
   - Runs both implementations
   - Verifies they produce identical results

**Dependencies:**
- Requires `build/simulator` executable
- Calls `python_reference_scheduler.py`
- Reads from `tests/test_traces/correctness/`

### python_reference_scheduler.py

**Pure Python EASY backfilling reference implementation.**

**Purpose:**
- Provides independent verification of EASY backfilling algorithm
- Generates oracle outputs for comparison against C++ implementation

**Usage:**
```bash
python3 scripts/python_reference_scheduler.py <input.csv> --nodes <num_nodes>
```

**Example:**
```bash
python3 scripts/python_reference_scheduler.py \
    tests/test_traces/correctness/bf01_basic_success_input.csv \
    --nodes 100
```

**Output:**
- Creates `<input>_oracle.csv` with scheduled results
- Creates `<input>_oracle_resources.csv` with resource usage over time

**Called by:** `verify_against_analytical.py`

**Algorithm:**
- Pure implementation of EASY backfilling
- No dependencies on DR_EVT code
- Single-file reference implementation for verification

## Test Data Generation Scripts

These scripts were used to generate test inputs and ground truth oracles. They are one-time generators, but kept for reproducibility.

### generate_all_analytical_oracles.py

**Generates all 19 analytical oracle CSV files from hand-traced results.**

**Purpose:**
- Contains hand-traced execution for each test scenario
- Each test has a dedicated function with expected job start/end times
- Generates ground truth `*_analytical.csv` files

**Usage:**
```bash
python3 scripts/generate_all_analytical_oracles.py
```

**Output:**
- Creates 19 `*_analytical.csv` files in `tests/test_traces/correctness/`
- Each file contains expected job scheduling results (hand-traced ground truth)

**File size:** 21 KB (contains all ground truth data)

**When to use:**
- To regenerate analytical oracles if they're corrupted
- To update ground truth when fixing test bugs
- To add new analytical test cases

**Structure:**
Each test function looks like:
```python
def generate_bf01_basic_success():
    """Basic backfilling - job can backfill successfully"""
    return [
        {'job_idx': 0, 'start_time': 0, 'end_time': 100},
        {'job_idx': 1, 'start_time': 0, 'end_time': 50},
    ]
```

### generate_correctness_traces.py

**Generates comprehensive scheduler correctness test traces.**

**Purpose:**
- Creates workloads that test specific EASY backfilling behaviors
- Generates input traces (not oracles)

**Test scenarios covered:**
- Basic 2-job scheduling
- Backfilling success/failure cases
- Resource contention
- Shadow time computation
- Queue priority handling
- Edge cases

**Usage:**
```bash
python3 scripts/generate_correctness_traces.py
```

**Output:**
- Creates test input files in `tests/test_traces/correctness/`
- Each file is a CSV trace with jobs to schedule

**When to use:**
- To regenerate test input files if they're corrupted
- To add new test scenarios
- To modify existing test workloads

### generate_large_test.py

**Generates realistic 500-job test traces for scale/stress testing.**

**Purpose:**
- Creates large traces for performance testing
- Tests simulator behavior under realistic workloads

**Usage:**
```bash
python3 scripts/generate_large_test.py
```

**Output:**
- Creates `tests/test_traces/scale/large_scale_500jobs.csv`
- 500 jobs with realistic size/duration distributions

**Characteristics:**
- Random job sizes (1-50 nodes)
- Random durations (10-1000 seconds)
- Staggered arrivals over time
- Realistic resource contention

**When to use:**
- For scale testing
- For performance benchmarking
- To test scheduler under load

## Development Tools

Helper utilities for debugging and manual verification.

### trace_by_hand.py

**Interactive manual tracing helper for debugging.**

**Purpose:**
- Step through EASY backfilling execution manually
- Display scheduler state at each event
- Verify expected behavior interactively

**Usage:**
```bash
python3 scripts/trace_by_hand.py <input.csv>
```

**Example:**
```bash
python3 scripts/trace_by_hand.py tests/test_traces/correctness/bf01_basic_success_input.csv
```

**Output:**
- Displays job queue state at each time step
- Shows what jobs are running/waiting
- Shows backfilling decisions

**When to use:**
- Debugging test failures
- Understanding scheduler behavior
- Creating new hand-traced oracles

## Obsolete/Unused Scripts

These scripts may be obsolete or have functionality integrated elsewhere.

### compare_with_oracle.py

**Status:** Likely obsolete - functionality in `verify_against_analytical.py`

Compares DR_EVT output with oracle output. This functionality appears to be integrated into the main verification script.

### compare_simulation_outputs.py

**Status:** Unknown - may be standalone tool

Compares C++ vs Python simulation outputs. Not called by test runners, but may be useful standalone.

### create_analytical_oracles.py

**Status:** Likely obsolete - replaced by `generate_all_analytical_oracles.py`

Helper for oracle generation, appears to be superseded by the more complete script.

## Workflow Diagram

```
Test Execution Flow:
====================

tests/run_correctness_tests.sh
    |
    v
verify_against_analytical.py
    |
    +---> Reads: tests/test_traces/correctness/*_input.csv
    +---> Reads: tests/test_traces/correctness/*_analytical.csv (ground truth)
    |
    +---> Runs: build/simulator (C++ implementation)
    |
    +---> Runs: python_reference_scheduler.py (Python reference)
    |
    +---> Compares outputs vs analytical oracle
    |
    +---> Reports: PASS/FAIL for each test


Test Data Generation Flow:
===========================

generate_correctness_traces.py
    |
    v
Creates: *_input.csv files

generate_all_analytical_oracles.py
    |
    v
Creates: *_analytical.csv files (ground truth)

python_reference_scheduler.py
    |
    v
Creates: *_oracle.csv files (computed by Python reference)
```

## File Naming Conventions

Test files follow a naming pattern:

```
tests/test_traces/correctness/
├── bf01_basic_success_input.csv          ← Input: jobs to schedule
├── bf01_basic_success_input_analytical.csv   ← Ground truth: expected results (hand-traced)
├── bf01_basic_success_input_oracle.csv       ← Python output: computed by python_reference_scheduler.py
└── /tmp/bf01_basic_success_input_drevt.csv   ← C++ output: computed by DR_EVT simulator
```

**Verification process:**
1. Compare `*_drevt.csv` vs `*_analytical.csv` (C++ vs ground truth)
2. Compare `*_oracle.csv` vs `*_analytical.csv` (Python vs ground truth)
3. Both must match for test to PASS

## Adding New Correctness Tests

To add a new correctness test:

1. **Create test input** in `generate_correctness_traces.py`:
   ```python
   def generate_my_new_test():
       jobs = [
           {'submit': 0, 'nodes': 10, 'duration': 100},
           {'submit': 10, 'nodes': 20, 'duration': 50},
       ]
       write_csv('my_new_test_input.csv', jobs)
   ```

2. **Hand-trace expected execution** and add to `generate_all_analytical_oracles.py`:
   ```python
   def generate_my_new_test():
       """Description of what this test checks"""
       return [
           {'job_idx': 0, 'start_time': 0, 'end_time': 100},
           {'job_idx': 1, 'start_time': 0, 'end_time': 50},
       ]
   ```

3. **Regenerate files**:
   ```bash
   python3 scripts/generate_correctness_traces.py
   python3 scripts/generate_all_analytical_oracles.py
   ```

4. **Run verification**:
   ```bash
   ./tests/run_correctness_tests.sh
   ```

The new test will automatically be picked up by `verify_against_analytical.py`.

## Requirements

All scripts require Python 3.6+. No external dependencies.

**For testing scripts:**
- `verify_against_analytical.py` requires `build/simulator` to exist
- Tests expect trace files in `tests/test_traces/correctness/`

## Maintenance

**If tests fail:**
1. Check which test failed in `verify_against_analytical.py` output
2. Use `trace_by_hand.py` to step through execution
3. Compare against analytical oracle to find discrepancy
4. Fix either:
   - C++ implementation bug (in `src/`)
   - Python reference bug (in `python_reference_scheduler.py`)
   - Ground truth bug (in `generate_all_analytical_oracles.py`)

**If adding new test scenarios:**
1. Update `generate_correctness_traces.py`
2. Hand-trace execution and update `generate_all_analytical_oracles.py`
3. Regenerate files and run verification

## See Also

- `tests/README.md` - Complete test suite documentation
- `tests/CORRECTNESS_TEST_METHODOLOGY.md` - Testing methodology
- `docs/EASY_BACKFILLING_PROPERTIES.md` - Algorithm properties
- `docs/VERIFICATION_COMPLETE.md` - Verification status

---

**Last updated:** 2026-08-28
