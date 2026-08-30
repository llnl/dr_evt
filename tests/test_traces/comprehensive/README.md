# Comprehensive Test Suite for EASY Backfilling

**Count**: 34 tests  
**Status**: ✅ 34/34 passing (100%)  
**Runner**: `./tests/test_all_dr_evt.sh`

## Overview

This directory contains **34 comprehensive tests** for FCFS + EASY backfilling scheduler validation using **dual validation** (job schedules + resource traces).

## Quick Start

```bash
./tests/test_all_dr_evt.sh
```

Expected: `🎉 ALL TESTS PASSED!`

## Test Organization

Tests ordered by complexity (01=simplest, 34=most complex):
- **Tier 1-2** (01-08): FCFS & Basic Backfilling
- **Tier 3-4** (09-15): Event Timing & Competition
- **Tier 5** (16-19): Multiple Simultaneous Backfills ⚠️ **Bug detection**
- **Tier 6-7** (20-28): Mixed Events & System Properties
- **Tier 8** (29-31): Large Scale (20-50 jobs)
- **Tier 9** (32-34): Early Completion

See `docs/testing/TEST_SUITE_ORDERED_BY_COMPLEXITY.md` for detailed breakdown.

## Verification Method

Each test validates **TWO things**:
1. ✅ Job schedules (start_time, end_time)
2. ✅ Resource traces (nodes_used, nodes_free)

Both must match for test to pass.

## References

- **Full Documentation**: `tests/README.md`
- **Test Details**: `docs/testing/TEST_SUITE_ORDERED_BY_COMPLEXITY.md`
- **Python Reference**: `scripts/python_reference_scheduler.py`

Tests are organized by complexity (Tier 1-7) and use **analytical validation** - expected outputs are calculated mathematically, not by comparing to another implementation.

## Test Organization

### Tier 1: FCFS Fundamentals (4 tests)
Basic FCFS scheduling without backfilling scenarios.

- **07_simultaneous_submit**: 4 jobs submit at t=0, all fit simultaneously
- **10_queue_drain_idle**: Jobs complete, system goes idle, resumes later
- **13_consecutive_fcfs**: Jobs use all resources, run sequentially
- **15_fcfs_partial_overlap**: Some jobs run concurrently, others wait

### Tier 2: Basic Backfilling (4 tests)
Core 3-job backfill pattern with single running job.

- **01_backfill_allowed**: Backfiller completes before reservation → success
- **02_backfill_blocked_time**: Backfiller would exceed time window → blocked
- **03_backfill_blocked_resources**: Backfiller doesn't fit physically → blocked
- **06_backfill_out_of_order**: Later job backfills before earlier job

### Tier 3: Backfill Competition (3 tests)
Multiple jobs competing to backfill.

- **04_backfill_resource_competition**: Limited resources, not all backfillers fit
- **05_multiple_backfills**: All backfillers fit simultaneously
- **14_fcfs_with_backfill**: FCFS heads alternate with backfilling

### Tier 4: Event Timing (4 tests)
Edge cases with simultaneous events and state transitions.

- **08_simultaneous_completion**: Multiple jobs end at same time
- **09_simultaneous_submit_complete**: Job arrives exactly when another completes
- **11_multiple_drains**: Multiple idle/active cycles
- **12_drain_with_backlog**: System idles while jobs waiting

### Tier 5: Complex Backfilling (3 tests)
Advanced scenarios with multiple constraints.

- **24_multiple_running_jobs**: FCFS head needs resources from 2+ running jobs
- **19_resource_fragmentation**: Odd-sized jobs causing fragmentation
- **20_fragmentation_recovery**: Small jobs utilize fragmented space

### Tier 6: System Properties (6 tests)
Large-scale tests verifying correctness properties.

- **16_starvation_prevention**: Large FCFS head + many small backfillers
- **17_late_large_priority**: Late-arriving large job gets FCFS priority
- **18_backfill_no_starvation**: Backfilling doesn't starve FCFS head
- **21_sustained_high_load**: 50 jobs, high utilization, no deadlock
- **22_bursty_load**: Periodic bursts of job arrivals
- **23_mixed_load**: Mix of large and small jobs

### Tier 7: Early Completions (3 tests)
Jobs finishing before their time_limit.

- **25_early_completion_basic**: Job finishes early, FCFS head starts immediately
- **26_early_completion_cascading**: Multiple early completions trigger starts
- **27_early_vs_late_completion**: Mix of early and full-runtime jobs

## File Format

### Input Trace (`.csv`)
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit[,actual_runtime]
0,70,0,pbatch,200
10,50,0,pbatch,300
20,20,0,pbatch,50
```

- `actual_runtime` is optional (defaults to `time_limit` if missing)
- For early completion tests, `actual_runtime < time_limit`

### Expected Output (`.expected_output.csv`)
```csv
job_id,start_time,end_time
0,0,200
1,200,500
2,20,70
```

Analytically calculated expected schedule.

### Expected Resources (`.expected_resources.csv`)
```csv
time,nodes_used,nodes_free,running_jobs
0,70,30,0
20,90,10,"0,2"
70,70,30,0
```

Timeline of resource usage at each event.

### Answer File (`.answer.json`)
```json
{
  "test_id": "01_backfill_allowed",
  "exact_schedule": [[0,0,200], [1,200,500], [2,20,70]],
  "constraints": [
    {"type": "backfill", "job": 2, "description": "Job 2 backfills"},
    {"type": "time_window", "job": 2, "end_before": 200}
  ]
}
```

For programmatic validation (used by Python validator).

### Construction Documentation (`.construction.md`)
Mathematical formulas documenting how the test was constructed.

Example:
```markdown
**Formula**:
- R0 = 70 (70% of system)
- F = TOTAL - R0 = 30 (free space)
- R1 = 50 > F (FCFS head cannot fit)
- R2 = 20 <= F (backfiller fits)
```

## Validation Methods

### Method 1: Programmatic (Python)
```bash
cd build
python3 test_ordered.py
```
Uses `.answer.json` files with constraint checking.

### Method 2: Diff-based (Shell)
```bash
cd build
./compare_with_analytical.sh 01_backfill_allowed
```
Compares simulator output with `.expected_output.csv` using `diff`.

### Method 3: Visual Inspection
```bash
cat 01_backfill_allowed.expected_output.csv
./simulator 01_backfill_allowed.csv
diff expected actual
```

## Test Results

### Python Reference Implementation
```
27/27 tests PASSING ✅
```

Run tests:
```bash
cd build
python3 test_ordered.py
```

Output:
```
Tier 1: FCFS ..................... 4/4 ✅
Tier 2: Basic Backfill ........... 4/4 ✅
Tier 3: Competition .............. 3/3 ✅
Tier 4: Events ................... 4/4 ✅
Tier 5: Complex .................. 3/3 ✅
Tier 6: Properties ............... 6/6 ✅
Tier 7: Early Completion ......... 3/3 ✅
```

## Key Testing Principles

### 1. Analytical Ground Truth
Expected outputs are **calculated by hand** from EASY backfilling rules, not generated by running code.

### 2. Mathematical Formulas
Input traces constructed from formulas (e.g., `R1 = 1.67 × F`), not arbitrary numbers.

### 3. Independent Verification
Don't compare two implementations - compare implementation to mathematical truth.

### 4. Systematic Coverage
Tests organized by complexity, ensuring all edge cases covered.

## Generating Tests

### Generate Analytical Input Traces
```bash
cd build
python3 generate_analytical_traces.py
```
Creates `.csv` and `.construction.md` files from formulas.

### Generate Analytical Output Traces
```bash
cd build
python3 generate_analytical_outputs.py
```
Creates `.expected_output.csv` and `.expected_resources.csv` by hand-calculating schedules.

## Adding New Tests

1. Add formula function to `build/generate_analytical_traces.py`
2. Calculate expected schedule by hand from EASY rules
3. Add to `build/generate_analytical_outputs.py`
4. Create answer file (see existing `.answer.json` files)
5. Add to `build/test_ordered.py` in appropriate tier
6. Run and verify: `python3 test_ordered.py`

## References

- **Algorithm specification**: `/docs/EASY_BACKFILLING_ALGORITHM.md`
- **Test organization**: `TEST_ORDER.md`
- **Trace generation guide**: `/build/README_ANALYTICAL_TRACES.md`
- **Session summary**: `/docs/SESSION_SUMMARY.md`
- **Main project docs**: `/DOCUMENTATION.md` (if exists)
