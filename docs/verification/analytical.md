# Analytical Verification Plan

## Goal
Create hand-traced analytical oracles for 20 small test scenarios, then verify both DR_EVT and Python reference implementations match these oracles exactly.

## Current Status
- Both implementations currently match each other on all 23 tests
- Need to verify correctness against hand-traced ground truth
- `ANALYTICAL_VERIFICATION.md` has some tests documented but doesn't match actual input files

## Approach

### Phase 1: Create Analytical Oracles (Manual Work Required)
For each of the 20 small tests, manually trace through EASY backfilling algorithm:

1. Read input CSV
2. Hand-trace execution:
   - Track wait queue (FCFS order)
   - Track resources (free/allocated)
   - Apply EASY rules:
     - FCFS head gets reservation
     - Backfill if: fits AND completes < reservation
   - Record each job's start_time and end_time
3. Write `*_analytical.csv` file with ground truth

### Phase 2: Automated Verification
Create verification script that:
1. Runs DR_EVT on input → compare with analytical
2. Runs Python on input → compare with analytical
3. Reports mismatches for investigation

### Phase 3: Cross-Validation (Large Tests)
For the 3 large tests (100, 500, 2000 jobs):
- Too large to hand-trace
- Verify DR_EVT and Python produce identical results
- Assumption: if both pass small analytical tests, agreement on large tests validates both

## Test List

### Small Tests (Need Analytical Oracles) - 20 tests
1. backfill_3jobs
2. backfill_blocked
3. basic_2jobs
4. bf01_basic_success
5. bf02_blocked_time
6. bf03_blocked_resources
7. bf04_multiple_backfill
8. bf05_sequential_backfill
9. bf06_exact_timing
10. bf07_fcfs_not_backfill
11. bf08_backfill_fcfs_delayed
12. bf09_multiple_fcfs
13. bf10_long_duration
14. easy_5jobs
15. hand_backfill_blocked
16. hand_simple_backfill
17. idle_gap
18. inv01_idle_system
19. medium_50jobs
20. sequential_wait

### Large Tests (Cross-Validation Only) - 3 tests
21. cross_validation_100jobs (100 jobs)
22. large_500jobs (500 jobs)
23. large_2000jobs (2000 jobs)

## Next Steps

1. **For now**: Both implementations agree on all 23 tests, which is a strong signal of correctness
2. **Manual tracing**: Incrementally add analytical oracles by hand-tracing small inputs
3. **Verification**: As each analytical oracle is created, verify both implementations match it
4. **Documentation**: Update `ANALYTICAL_VERIFICATION.md` with traces that match actual input files

## Decision Point

Given that:
- Both implementations now use identical algorithm (verified by code review)
- Both pass all 23 tests with identical results
- Creating 20 analytical oracles is significant manual work

**Recommended approach:**
1. Create analytical oracles for ~5 key scenarios (different backfill cases)
2. Verify both implementations match these
3. Trust cross-validation for remaining tests
4. Add more analytical oracles incrementally as needed for debugging

**Alternative (full verification):**
- Hand-trace all 20 small tests
- More confidence but significant time investment
