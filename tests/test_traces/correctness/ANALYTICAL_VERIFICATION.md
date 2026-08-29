# Analytical Verification of Correctness Tests

This document contains hand-traced expected behavior for each correctness test case.
Both DR_EVT and Python oracle implementations must match these analytical expectations.

## Test Philosophy

1. **Analytical ground truth**: Manually trace expected behavior using EASY backfilling rules
2. **Verify both implementations**: Check DR_EVT and Python oracle match the analytical trace  
3. **Cross-validate on large traces**: Only after both pass analytical tests

## EASY Backfilling Algorithm

**Rules:**
1. First job in queue (FCFS order) gets a reservation
2. Other jobs can backfill if:
   - They fit in currently available resources
   - They will complete before the FCFS head's reservation time
3. FCFS order: by submit_time, tie-breaker by job index (file order)

## Test Cases

### basic_2jobs

**Input:**
- Total nodes: 100
- Job 0: submit=0, nodes=10, duration=50
- Job 1: submit=0, nodes=20, duration=50

**Analytical trace:**
```
t=0:  Jobs 0,1 arrive simultaneously
      FCFS order: [Job 0, Job 1] (file order tie-breaker)
      Job 0 is FCFS head, fits (10 ≤ 100) → START Job 0
      Resources: 90 free, 10 allocated
      Job 1 is new FCFS head, fits (20 ≤ 90) → START Job 1
      Resources: 70 free, 30 allocated

t=50: Both jobs complete
      Resources: 100 free, 0 allocated
```

**Expected output:**
- Job 0: start=0, end=50
- Job 1: start=0, end=50

**Status:** ✓ Both implementations match

---

### bf01_basic_success (Basic backfill success)

**Input:**
- Total nodes: 100
- Job 0: submit=0, nodes=80, duration=100
- Job 1: submit=10, nodes=15, duration=30

**Analytical trace:**
```
t=0:  Job 0 arrives
      Job 0 is FCFS head, fits (80 ≤ 100) → START Job 0
      Resources: 20 free, 80 allocated

t=10: Job 1 arrives
      Job 0 is FCFS head (still running)
      Reservation for Job 0: already running, completes at t=100
      Backfill check for Job 1:
        - Fits? 15 ≤ 20 ✓
        - Completes before reservation? 10+30=40 < 100 ✓
        → BACKFILL Job 1
      Resources: 5 free, 95 allocated

t=40: Job 1 completes (backfilled)
      Resources: 20 free, 80 allocated

t=100: Job 0 completes
       Resources: 100 free, 0 allocated
```

**Expected output:**
- Job 0: start=0, end=100
- Job 1: start=10, end=40 (backfilled)

**Status:** ✓ Both implementations match

---

### bf02_blocked_time (Backfill blocked by time constraint)

**Input:**
- Total nodes: 100
- Job 0: submit=0, nodes=80, duration=50
- Job 1: submit=0, nodes=60, duration=30
- Job 2: submit=10, nodes=15, duration=45

**Analytical trace:**
```
t=0:  Jobs 0,1 arrive
      FCFS order: [Job 0, Job 1] (file order)
      Job 0 is FCFS head, fits (80 ≤ 100) → START Job 0
      Resources: 20 free, 80 allocated
      Job 1 is new FCFS head, cannot fit (60 > 20)
      Reservation for Job 1: 
        Job 0 completes at t=50
        After Job 0: 100 free ≥ 60 needed
        → Reservation at t=50

t=10: Job 2 arrives
      Job 1 is FCFS head, reservation at t=50
      Backfill check for Job 2:
        - Fits? 15 ≤ 20 ✓
        - Completes before reservation? 10+45=55 > 50 ✗
        → BLOCKED (would delay FCFS head)

t=50: Job 0 completes
      Job 1 is FCFS head, fits (60 ≤ 100) → START Job 1
      Resources: 40 free, 60 allocated
      Job 2 is new FCFS head, fits (15 ≤ 40) → START Job 2
      Resources: 25 free, 75 allocated

t=80: Job 1 completes
      Resources: 85 free, 15 allocated

t=95: Job 2 completes
      Resources: 100 free, 0 allocated
```

**Expected output:**
- Job 0: start=0, end=50
- Job 1: start=50, end=80 (FCFS, waited for reservation)
- Job 2: start=50, end=95 (blocked from backfilling, started as FCFS head)

**Key insight:** Job 2 CANNOT backfill at t=10 because it would complete at t=55, after Job 1's reservation at t=50.

**Status:** ✓ Both implementations match

---

### bf04_multiple_backfill (Multiple jobs can backfill)

**Input:**
- Total nodes: 100
- Job 0: submit=0, nodes=80, duration=100
- Job 1: submit=0, nodes=50, duration=200
- Job 2: submit=10, nodes=15, duration=30
- Job 3: submit=20, nodes=15, duration=30

**Analytical trace:**
```
t=0:  Jobs 0,1 arrive
      Job 0 is FCFS head, fits (80 ≤ 100) → START Job 0
      Resources: 20 free, 80 allocated
      Job 1 is FCFS head, cannot fit (50 > 20)
      Reservation for Job 1: Job 0 completes at t=100, then 100 ≥ 50 → t=100

t=10: Job 2 arrives
      Job 1 is FCFS head, reservation at t=100
      Backfill check for Job 2:
        - Fits? 15 ≤ 20 ✓
        - Completes before reservation? 10+30=40 < 100 ✓
        → BACKFILL Job 2
      Resources: 5 free, 95 allocated

t=20: Job 3 arrives
      Job 1 is FCFS head, reservation at t=100
      Backfill check for Job 3:
        - Fits? 15 > 5 ✗
        → Cannot fit, waits

t=40: Job 2 completes
      Resources: 20 free, 80 allocated
      Job 1 is FCFS head, cannot fit (50 > 20)
      Backfill check for Job 3:
        - Fits? 15 ≤ 20 ✓
        - Completes before reservation? 40+30=70 < 100 ✓
        → BACKFILL Job 3
      Resources: 5 free, 95 allocated

t=70: Job 3 completes
      Resources: 20 free, 80 allocated

t=100: Job 0 completes
       Job 1 is FCFS head, fits (50 ≤ 100) → START Job 1
       Resources: 50 free, 50 allocated

t=300: Job 1 completes
       Resources: 100 free, 0 allocated
```

**Expected output:**
- Job 0: start=0, end=100
- Job 1: start=100, end=300 (FCFS, waited)
- Job 2: start=10, end=40 (backfilled)
- Job 3: start=40, end=70 (backfilled after Job 2 completes)

**Status:** ✓ Both implementations match

---

## Cross-Validation on Large Traces

After all small analytical tests pass:
1. Run both implementations on medium_50jobs, large_500jobs, large_2000jobs
2. Compare outputs between implementations
3. Any differences indicate a bug in one or both implementations
4. Debug by examining first mismatch in detail

## Current Status

**Small tests (analytical verification):** ✓ 19/19 pass
**Medium test (50 jobs):** ✓ Pass
**Large tests (100+ jobs):** ✗ Mismatches found
  - cross_validation_100jobs: Job 12 starts at wrong time
  - large_500jobs: Multiple mismatches

**Next step:** Investigate why large traces diverge when small tests pass.
