# Testing Guide for DR_EVT Scheduler

## Overview

DR_EVT uses **analytical testing** - expected outputs are calculated mathematically from first principles, not by comparing implementations.

This ensures scheduler correctness is verified against **mathematical ground truth**, not against another potentially buggy implementation.

## Test Suite Organization

### 27 Comprehensive Tests (Tier 1-7)

**Location**: `/tests/test_traces/comprehensive/`

| Tier | Focus | Tests | Complexity |
|------|-------|-------|------------|
| 1 | FCFS Fundamentals | 4 | ⭐ |
| 2 | Basic Backfilling | 4 | ⭐⭐ |
| 3 | Backfill Competition | 3 | ⭐⭐⭐ |
| 4 | Event Timing | 4 | ⭐⭐ |
| 5 | Complex Backfilling | 3 | ⭐⭐⭐⭐ |
| 6 | System Properties | 6 | ⭐⭐⭐⭐⭐ |
| 7 | Early Completions | 3 | ⭐⭐⭐ |

**Total**: 27 tests covering all EASY backfilling scenarios

See `/tests/test_traces/comprehensive/TEST_ORDER.md` for detailed test listing.

## Running Tests

### Python Reference Implementation

```bash
cd build

# Run all 27 tests in order
python3 test_ordered.py

# Expected output:
# Tier 1: FCFS ..................... 4/4 ✅
# Tier 2: Basic Backfill ........... 4/4 ✅
# ...
# RESULTS: 27 passed, 0 failed, 0 skipped
# 🎉 ALL TESTS PASSED!
```

### C++ DR_EVT Simulator (TODO)

```bash
cd build

# Run single test with diff
./compare_with_analytical.sh 01_backfill_allowed

# Expected output:
# Comparing job schedules:
# ✓ Job schedules MATCH
# Test 01_backfill_allowed: PASS
```

## Test Validation Methods

### Method 1: Programmatic (Python validator)

Uses `.answer.json` files with constraint checking:

```bash
python3 test_ordered.py
```

**Validates**:
- Exact schedules (job start/end times)
- Constraints (backfill properties, FCFS order, time windows)
- System properties (no oversubscription, all jobs complete)

### Method 2: Diff-based (Visual comparison)

Uses `.expected_output.csv` files:

```bash
./compare_with_analytical.sh TEST_ID
```

**Validates**:
- Visual diff of expected vs. actual schedules
- Exact match required
- Easy to see discrepancies

### Method 3: Manual Verification

For understanding test behavior:

```bash
# View expected schedule
cat 01_backfill_allowed.expected_output.csv

# View expected resource timeline
cat 01_backfill_allowed.expected_resources.csv

# View construction formulas
cat 01_backfill_allowed.construction.md
```

## Test File Formats

### Input Trace (`.csv`)
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit[,actual_runtime]
0,70,0,pbatch,200
10,50,0,pbatch,300
20,20,0,pbatch,50
```

- `actual_runtime` optional (defaults to `time_limit`)
- Used for early completion tests

### Expected Job Output (`.expected_output.csv`)
```csv
job_id,start_time,end_time
0,0,200
1,200,500
2,20,70
```

Hand-calculated from EASY backfilling rules.

### Expected Resource Timeline (`.expected_resources.csv`)
```csv
time,nodes_used,nodes_free,running_jobs
0,70,30,0
20,90,10,"0,2"
70,70,30,0
200,0,100,
```

Shows resource usage at each scheduling event.

### Answer File (`.answer.json`)
```json
{
  "test_id": "01_backfill_allowed",
  "exact_schedule": [[0,0,200], [1,200,500], [2,20,70]],
  "constraints": [
    {"type": "backfill", "job": 2},
    {"type": "time_window", "job": 2, "end_before": 200}
  ]
}
```

For programmatic validation.

## Analytical Test Generation

### Philosophy

Tests are **generated from mathematical formulas**, not arbitrary numbers.

**Example**:
```python
R0 = int(TOTAL * 0.7)        # 70 nodes (70% of system)
F = TOTAL - R0                # 30 free
R1 = int(F * 1.67)           # 50 > 30 (FCFS head blocked)
R2 = int(F * 0.67)           # 20 <= 30 (backfiller fits)
```

**Benefits**:
- Reproducible from formulas
- Self-documenting (WHY these values)
- Systematic edge case coverage
- Mathematically verifiable

### Generate Inputs

```bash
cd build
python3 generate_analytical_traces.py
```

Creates:
- `{test}.csv` - Input trace
- `{test}.construction.md` - Formula documentation

### Generate Expected Outputs

```bash
cd build
python3 generate_analytical_outputs.py
```

Creates:
- `{test}.expected_output.csv` - Expected schedule
- `{test}.expected_resources.csv` - Expected resource timeline

**IMPORTANT**: Expected outputs are **hand-calculated** by applying EASY rules step-by-step, NOT by running simulator code.

## Adding New Tests

### Step 1: Define Test Formula

Edit `build/generate_analytical_traces.py`:

```python
def gen_my_new_test():
    """
    Test description.
    
    Formula:
    - R0 = ...
    - F = ...
    """
    jobs = [...]
    write_trace('28_my_new_test', jobs, description)
```

### Step 2: Calculate Expected Schedule

By hand, trace through EASY algorithm:
- t=0: What happens?
- t=10: What happens?
- ...

Document in `build/generate_analytical_outputs.py`.

### Step 3: Create Answer File

Add to `.answer.json` file with exact schedule and constraints.

### Step 4: Add to Test Runner

Edit `build/test_ordered.py`:

```python
('28_my_new_test', 'Tier X: Category'),
```

### Step 5: Verify

```bash
python3 test_ordered.py
```

Should show 28/28 passing.

## Common Test Patterns

### 3-Job Backfill Pattern

```
Job 0: Running (R0 nodes, duration D0)
Job 1: FCFS head, CANNOT fit (R1 > free)
Job 2: Backfiller, CAN fit (R2 <= free)
```

**Question**: Can Job 2 backfill?
- Check: Job 2 completion < Job 1 reservation?

### Multiple Running Jobs Pattern

```
Job 0: R0 nodes, ends at T0
Job 1: R1 nodes, ends at T1 > T0
Job 2: FCFS head, needs R2 > (TOTAL - R0) and R2 > (TOTAL - R1)
```

**Reservation**: T1 (must wait for both)

### Early Completion Pattern

```
Job 0: time_limit=200, actual_runtime=50
Job 1: FCFS head, reservation calculated using 200
```

**Result**: Job 1 starts at 50 (opportunistic), not 200 (pessimistic)

## Troubleshooting

### Test Fails: Schedule Mismatch

1. Read construction.md to understand formulas
2. Manually trace through EASY algorithm step-by-step
3. Verify expected output is correct
4. Check if simulator has bug or expected output wrong

### Test Fails: Constraint Violation

1. Check constraint type and parameters
2. Verify constraint is appropriate for test
3. Check if simulator behavior is wrong

### All Tests Pass but Behavior Seems Wrong

- Tests may not cover this scenario
- Create new test case
- Hand-calculate expected behavior
- Add to test suite

## References

- **Algorithm**: `/docs/EASY_BACKFILLING_ALGORITHM.md`
- **Test README**: `/tests/test_traces/comprehensive/README.md`
- **Test Organization**: `/tests/test_traces/comprehensive/TEST_ORDER.md`
- **Analytical Testing**: `/tests/ANALYTICAL_TESTING.md`
- **Session Summary**: `/docs/SESSION_SUMMARY.md`

## Key Principles

1. **Analytical Ground Truth**: Expected outputs from mathematics, not code
2. **Mathematical Formulas**: Inputs generated systematically
3. **Independent Verification**: Don't compare implementations
4. **Hand Calculation**: Expected outputs traced by hand from EASY rules
5. **Diff Validation**: Visual comparison catches exact discrepancies
