# Correctness Test Methodology

## Overview

DR_EVT correctness is verified through a two-phase approach:
1. **Analytical Verification** (19 tests) - Compare against hand-traced ground truth
2. **Cross-Validation** (4 tests) - Verify C++ and Python implementations agree

Both phases test **both implementations**:
- C++ implementation (DR_EVT simulator)
- Python reference implementation (scripts/python_reference_scheduler.py)

## Phase 1: Analytical Verification (19 tests)

### Approach
Small job traces (2-10 jobs) are hand-traced to produce analytical oracles - the mathematically correct ground truth for EASY backfilling.

### Test List
1. bf01_basic_success - Basic backfill success
2. bf02_blocked_time - Blocked by time constraint
3. bf03_blocked_resources - Blocked by resource constraint
4. bf04_multiple_backfill - Multiple jobs backfill
5. bf05_sequential_backfill - Sequential backfilling
6. bf06_exact_timing - Strict `<` timing rule
7. bf07_fcfs_not_backfill - FCFS head doesn't backfill
8. bf08_backfill_fcfs_delayed - Backfill when FCFS delayed
9. bf09_multiple_fcfs - Multiple FCFS jobs
10. bf10_long_duration - Long duration job
11. basic_2jobs - Two job basic case
12. backfill_3jobs - Three job backfill
13. backfill_blocked - Backfill blocked scenario
14. easy_5jobs - Five job EASY backfill
15. hand_simple_backfill - Hand-traced simple backfill
16. hand_backfill_blocked - Hand-traced blocked backfill
17. idle_gap - Idle resource gap
18. inv01_idle_system - Idle system invariant
19. sequential_wait - Sequential waiting

### Oracle Format
Each test has:
- `<testname>_input.csv` - Input trace
- `<testname>_analytical.csv` - Hand-traced ground truth
- `<testname>_oracle.csv` - Python reference output (optional)

### Verification
```bash
# Both implementations must match analytical oracle
python3 scripts/verify_against_analytical.py
```

Expected: 19/19 passed for both C++ and Python

## Phase 2: Cross-Validation (4 tests)

### Approach
Larger job traces (50-2000 jobs) are too large to hand-trace. Instead, we verify that **both implementations produce identical results**.

### Test List
1. medium_50jobs - 50 jobs
2. cross_validation_100jobs - 100 jobs
3. large_500jobs - 500 jobs
4. large_2000jobs - 2000 jobs

### Rationale
If two independent implementations (C++ and Python) produce identical results, and both correctly implement the same algorithm, this provides strong evidence of correctness.

### Verification
```bash
# C++ output must exactly match Python output
python3 scripts/verify_against_analytical.py
```

Expected: 4/4 cross-validation passed

## Test Execution

### Running All Tests
```bash
# Run complete test suite (23 tests)
cd /Users/yeom2/work/dr_evt
python3 scripts/verify_against_analytical.py
```

### Output Format
```
Phase 1: Analytical Verification (19 tests)
DR_EVT:  19 passed, 0 failed
Python:  19 passed, 0 failed

Phase 2: Cross-validation (4 tests)
medium_50jobs_input                       ✓
cross_validation_100jobs_input            ✓
large_500jobs_input                       ✓
large_2000jobs_input                      ✓

SUMMARY
Total: 23/23 tests passed
✓ ALL TESTS PASSED
```

## Test Files Location

```
tests/test_traces/correctness/
├── bf01_basic_success_input.csv
├── bf01_basic_success_analytical.csv
├── bf02_blocked_time_input.csv
├── bf02_blocked_time_analytical.csv
...
├── medium_50jobs_input.csv
├── medium_50jobs_oracle.csv
├── large_500jobs_input.csv
├── large_500jobs_oracle.csv
└── large_2000jobs_input.csv
```

## Implementation Details

### Analytical Oracle Generation
Hand-traced using EASY backfilling rules:
1. FCFS head gets reservation (earliest possible completion)
2. Other jobs backfill if they fit AND complete before reservation (strict `<`)
3. All END events at same timestamp batched before scheduling

See: `scripts/generate_all_analytical_oracles.py`

### Python Reference Implementation
- File: `scripts/python_reference_scheduler.py`
- Event-driven simulation
- Implements EASY backfilling exactly as specified
- Used for both oracle generation and cross-validation

### C++ Implementation
- File: `src/sim/scheduler.cpp`
- Production implementation
- Must match Python reference exactly

## Current Status

**All 23 tests passing** ✓

- Analytical: 19/19 ✓
- Cross-validation: 4/4 ✓
- Both implementations verified ✓

Last verified: 2026-08-28

## Adding New Tests

### For Small Tests (Analytical)
1. Create input trace: `tests/test_traces/correctness/<name>_input.csv`
2. Hand-trace execution: Document in `scripts/generate_all_analytical_oracles.py`
3. Generate oracle: Add to generation script
4. Run verification: `python3 scripts/verify_against_analytical.py`

### For Large Tests (Cross-validation)
1. Create input trace: `tests/test_traces/correctness/<name>_input.csv`
2. Generate oracle: `python3 scripts/python_reference_scheduler.py <input> <oracle>`
3. Run verification: `python3 scripts/verify_against_analytical.py`

## References

- **EASY Backfilling**: docs/verification/easy-backfilling.md
- **Test Descriptions**: docs/verification/test-descriptions.md
- **Analytical Verification Plan**: docs/verification/analytical.md
- **Algorithm**: docs/development/algorithm.md
