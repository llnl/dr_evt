# Backfill Correctness Tests - Explained

## Overview

The backfill correctness test suite systematically verifies that the EASY backfilling algorithm is implemented correctly. These 10 tests cover all essential backfilling scenarios.

## Test Structure

Each test verifies a specific aspect of backfilling logic:
- **Input:** Small, focused scenario (2-4 jobs)
- **Expected behavior:** Hand-verified correct outcome
- **Verification:** Both Python reference and C++ implementation must produce identical outputs

## The 10 Tests Explained

---

### BF-1: Basic Backfill Success
**File:** `bf01_basic_success_input.csv`

**Scenario:**
```
Job 0: submit=0,  nodes=80, duration=100
Job 1: submit=10, nodes=15, duration=30
System: 100 nodes
```

**What happens:**
```
t=0:  Job 0 starts (80 nodes, 20 free)
t=10: Job 1 arrives
      - Free: 20 ≥ 15 ✓
      - FCFS head: Job 1 (only waiting job)
      - Can start immediately
      - Starts at t=10, ends at t=30
t=100: Job 0 ends
```

**What this tests:** Basic case where a small job can start while a large job runs. Note: This is technically NOT backfilling since Job 1 is the FCFS head, but it tests the fundamental "small job fits" logic.

**Expected output:**
- Job 0: start=0, end=100
- Job 1: start=10, end=30

---

### BF-2: Backfill Blocked by Time Constraint
**File:** `bf02_blocked_time_input.csv`

**Scenario:**
```
Job 0: submit=0,  nodes=80, duration=50
Job 1: submit=0,  nodes=60, duration=30  (FCFS head in queue)
Job 2: submit=10, nodes=15, duration=45
System: 100 nodes
```

**What happens:**
```
t=0:  Job 0 starts (FCFS head)
      Job 1 waits (60 > 20 free)
      Job 1 reservation: t=50

t=10: Job 2 arrives
      - Resources: 15 ≤ 20 ✓ (fits)
      - FCFS head: Job 1 (reservation at t=50)
      - Backfill check: 10 + 45 = 55 > 50 ✗
      - Would complete AFTER Job 1's reservation
      - BLOCKED from backfilling
      - Must wait

t=50: Job 0 ends, Job 1 starts
```

**What this tests:** **Core backfill constraint** - a job is blocked from backfilling if it would complete after the FCFS head's reservation, even though it physically fits in available resources.

**Expected output:**
- Job 0: start=0, end=50
- Job 1: start=50, end=80
- Job 2: start=50 or 80 (waits for resources)

---

### BF-3: Backfill Blocked by Resource Constraint
**File:** `bf03_blocked_resources_input.csv`

**Scenario:**
```
Job 0: submit=0,  nodes=90, duration=100
Job 1: submit=10, nodes=20, duration=30
System: 100 nodes
```

**What happens:**
```
t=0:  Job 0 starts (90 nodes, 10 free)
t=10: Job 1 arrives
      - Resources: 20 > 10 ✗ (doesn't fit)
      - BLOCKED by insufficient resources
      - Must wait until Job 0 ends
t=100: Job 0 ends, Job 1 starts
```

**What this tests:** Resource constraint checked first - if job doesn't fit, time constraint doesn't matter.

**Expected output:**
- Job 0: start=0, end=100
- Job 1: start=100, end=130

---

### BF-4: Multiple Jobs Backfill Simultaneously
**File:** `bf04_multiple_backfill_input.csv`

**Scenario:**
```
Job 0: submit=0,  nodes=70, duration=100
Job 1: submit=10, nodes=10, duration=30
Job 2: submit=20, nodes=10, duration=30
Job 3: submit=30, nodes=10, duration=30
System: 100 nodes
```

**What happens:**
```
t=0:  Job 0 starts (70 nodes, 30 free)
t=10: Job 1 arrives, starts (10 nodes, 20 free)
t=20: Job 2 arrives, starts (10 nodes, 10 free)
t=30: Job 3 arrives, starts (10 nodes, 0 free)

All three small jobs run concurrently while Job 0 runs.
```

**What this tests:** Multiple small jobs can backfill simultaneously as long as total resources don't exceed capacity. Tests concurrent backfilling.

**Expected output:**
- Job 0: start=0, end=100 (70 nodes)
- Job 1: start=10, end=40 (10 nodes)
- Job 2: start=20, end=50 (10 nodes)
- Job 3: start=30, end=60 (10 nodes)

Peak usage: 70+10+10+10 = 100 nodes ✓

---

### BF-5: Sequential Backfills
**File:** `bf05_sequential_backfill_input.csv`

**Scenario:**
```
Job 0: submit=0,  nodes=80, duration=100
Job 1: submit=10, nodes=15, duration=20
Job 2: submit=20, nodes=15, duration=20
System: 100 nodes
```

**What happens:**
```
t=0:  Job 0 starts (80 nodes, 20 free)
t=10: Job 1 arrives
      - Resources: 15 ≤ 20 ✓
      - Starts, ends at t=30
      - Now: 80 + 15 = 95 used, 5 free

t=20: Job 2 arrives
      - Resources: 15 > 5 ✗
      - Cannot start yet
      - Waits

t=30: Job 1 ends (frees 15 nodes)
      - Now 20 free
      - Job 2 starts, ends at t=50
```

**What this tests:** Jobs backfill sequentially as resources become available. Tests dynamic resource tracking.

**Expected output:**
- Job 0: start=0, end=100
- Job 1: start=10, end=30
- Job 2: start=30, end=50

---

### BF-6: Exact Timing Boundary
**File:** `bf06_exact_timing_input.csv`

**Scenario:**
```
Job 0: submit=0,  nodes=80, duration=50
Job 1: submit=0,  nodes=60, duration=30 (FCFS head)
Job 2: submit=10, nodes=15, duration=40
System: 100 nodes
```

**What happens:**
```
t=0:  Job 0 starts
      Job 1 waits, reservation at t=50

t=10: Job 2 arrives
      - Backfill check: 10 + 40 = 50 ≤ 50 ✓
      - Completes EXACTLY at reservation time
      - Allowed to backfill (boundary case)
      - Starts at t=10
```

**What this tests:** Boundary condition - the backfill constraint uses `≤` not `<`. A job that completes exactly at the reservation time is allowed.

**Expected output:**
- Job 0: start=0, end=50
- Job 1: start=50, end=80
- Job 2: start=10, end=50

---

### BF-7: FCFS Not Backfill
**File:** `bf07_fcfs_not_backfill_input.csv`

**Scenario:**
```
Job 0: submit=0,  nodes=40, duration=50
Job 1: submit=10, nodes=30, duration=40
System: 100 nodes
```

**What happens:**
```
t=0:  Job 0 starts (40 nodes, 60 free)
t=10: Job 1 arrives
      - Is FCFS head (only waiting job)
      - Resources: 30 ≤ 60 ✓
      - Starts immediately as FCFS head
      - This is NOT backfilling!
```

**What this tests:** Distinguish between FCFS head starting (which always happens if resources available) and backfilling (which requires checking time constraint). Important conceptual distinction.

**Expected output:**
- Job 0: start=0, end=50
- Job 1: start=10, end=50 (FCFS head, not backfill)

---

### BF-8: Backfill Doesn't Affect FCFS Head Reservation
**File:** `bf08_backfill_fcfs_delayed_input.csv`

**Scenario:**
```
Job 0: submit=0,  nodes=80, duration=50
Job 1: submit=0,  nodes=60, duration=80 (FCFS head)
Job 2: submit=10, nodes=15, duration=20
System: 100 nodes
```

**What happens:**
```
t=0:  Job 0 starts
      Job 1 waits, reservation at t=50

t=10: Job 2 arrives, backfills
      - Backfill check: 10 + 20 = 30 < 50 ✓
      - Starts at t=10, ends at t=30

t=30: Job 2 ends (frees 15 nodes)
      - Job 1 still cannot fit (needs 60, only 35 free)

t=50: Job 0 ends
      - Job 1 starts (as scheduled by reservation)
      - Job 1's start time NOT affected by Job 2 completing
```

**What this tests:** FCFS head reservation is based on currently running jobs, not backfilled jobs. Backfills completing early don't advance the reservation.

**Expected output:**
- Job 0: start=0, end=50
- Job 1: start=50, end=130 (waits for reservation)
- Job 2: start=10, end=30 (backfilled)

---

### BF-9: Multiple FCFS Heads in Queue
**File:** `bf09_multiple_fcfs_input.csv`

**Scenario:**
```
Job 0: submit=0,  nodes=80, duration=50 (FCFS head, running)
Job 1: submit=0,  nodes=80, duration=50 (next FCFS head, waiting)
Job 2: submit=10, nodes=10, duration=20
System: 100 nodes
```

**What happens:**
```
t=0:  Job 0 starts (FCFS head)
      Job 1 waits (next FCFS head), reservation at t=50

t=10: Job 2 arrives
      - FCFS head: Job 1 (reservation at t=50)
      - Backfill check: 10 + 20 = 30 < 50 ✓
      - Can backfill, starts at t=10

t=30: Job 2 ends
t=50: Job 0 ends, Job 1 starts
```

**What this tests:** Backfilling works correctly when multiple large jobs are queued sequentially. The FCFS head for backfill purposes is the first waiting job, not the first running job.

**Expected output:**
- Job 0: start=0, end=50
- Job 1: start=50, end=100
- Job 2: start=10, end=30 (backfilled)

---

### BF-10: Long Duration Blocks Backfill
**File:** `bf10_long_duration_input.csv`

**Scenario:**
```
Job 0: submit=0,  nodes=80, duration=50
Job 1: submit=0,  nodes=60, duration=30 (FCFS head)
Job 2: submit=10, nodes=5,  duration=100
System: 100 nodes
```

**What happens:**
```
t=0:  Job 0 starts
      Job 1 waits, reservation at t=50

t=10: Job 2 arrives
      - Resources: 5 ≤ 20 ✓ (tiny job, fits easily)
      - Backfill check: 10 + 100 = 110 > 50 ✗
      - Long duration blocks backfill!
      - Must wait despite small node count

t=50: Job 0 ends, Job 1 starts
t=80: Job 1 ends, Job 2 starts
```

**What this tests:** Duration is as important as node count for backfilling. A job with very few nodes but long duration can be blocked from backfilling.

**Expected output:**
- Job 0: start=0, end=50
- Job 1: start=50, end=80
- Job 2: start=80, end=180 (blocked by duration)

---

## Test Coverage Summary

| Test | Scenario | Property Verified |
|------|----------|-------------------|
| BF-1 | Basic success | Small job can run with large job |
| BF-2 | Time block | Duration constraint enforced |
| BF-3 | Resource block | Resource constraint checked |
| BF-4 | Multiple concurrent | Multiple backfills coexist |
| BF-5 | Sequential | Dynamic resource tracking |
| BF-6 | Boundary | `≤` not `<` in constraint |
| BF-7 | FCFS distinction | Recognize FCFS vs backfill |
| BF-8 | Reservation integrity | Backfills don't affect FCFS |
| BF-9 | Multiple FCFS heads | Queue with multiple large jobs |
| BF-10 | Duration matters | Long duration blocks backfill |

## Why These 10 Tests Are Sufficient

**1. Core constraints tested:**
- ✅ Resource constraint (BF-3)
- ✅ Time constraint (BF-2, BF-6, BF-10)
- ✅ FCFS head priority (BF-7, BF-8, BF-9)

**2. Edge cases covered:**
- ✅ Boundary condition (BF-6: exactly at reservation)
- ✅ Multiple backfills (BF-4: concurrent, BF-5: sequential)
- ✅ Conceptual distinction (BF-7: FCFS vs backfill)

**3. Real scenarios:**
- ✅ Job blocked despite small size (BF-10: long duration)
- ✅ Job blocked despite available resources (BF-2: time constraint)
- ✅ Complex queue (BF-9: multiple FCFS heads)

## How to Run These Tests

```bash
cd /Users/yeom2/work/dr_evt

# Run all 10 backfill tests
for test in bf{01..10}_*_input.csv; do
    testname=$(basename $test _input.csv)
    echo "Testing $testname..."
    
    # Run DR_EVT
    ./build/simulator tests/test_traces/correctness/$test \
      --total_nodes 100 --trace_format simple --timestamp_format epoch \
      --duration_mode exact --outfile /tmp/${testname}.out
    
    # Compare with reference
    python3 scripts/compare_with_oracle.py \
      tests/test_traces/correctness/${testname}_reference.csv \
      /tmp/${testname}.out
done
```

## Expected Result

```
✅ bf01_basic_success - PASS
✅ bf02_blocked_time - PASS
✅ bf03_blocked_resources - PASS
✅ bf04_multiple_backfill - PASS
✅ bf05_sequential_backfill - PASS
✅ bf06_exact_timing - PASS
✅ bf07_fcfs_not_backfill - PASS
✅ bf08_backfill_fcfs_delayed - PASS
✅ bf09_multiple_fcfs - PASS
✅ bf10_long_duration - PASS

10/10 tests passed
```

If all tests pass → EASY backfilling is correctly implemented.

## What Each Test Catches

**If BF-2 fails:** Time constraint not enforced (critical bug)
**If BF-3 fails:** Resource constraint not checked
**If BF-4 fails:** Concurrent tracking broken
**If BF-5 fails:** Dynamic resource updates broken
**If BF-6 fails:** Off-by-one error in time check
**If BF-7 fails:** Confusion between FCFS and backfill
**If BF-8 fails:** Reservation calculation wrong
**If BF-9 fails:** FCFS head identification wrong
**If BF-10 fails:** Duration not considered in backfill check

Each test targets a specific aspect of the algorithm's correctness.
