# Hand-Traceable Correctness Tests

These tests are simple enough to verify by hand, providing absolute verification without circular dependencies.

## Purpose

**Problem:** How do we know the oracle is correct?  
**Solution:** Create tests so simple we can manually verify the only possible correct answer.

## Tests

### Test 1: Single Job
**File:** (Use any 1-job test)

**Input:**
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,50,0,pbatch,100
```

**Hand verification:**
- Only job in system
- Resources available: 50 ≤ 100 ✓
- Must start immediately at t=0
- Must end at t=100 (0 + 100)

**Expected output:**
```csv
job_idx,submit_time,start_time,end_time,nodes,duration
0,0.0,0.0,100.0,50,100.0
```

**Only ONE possible answer.** Any other output is wrong.

---

### Test 2: Sequential Jobs (FCFS Without Backfill)
**File:** `sequential_wait_input.csv`

**Input:**
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,50
0,60,0,pbatch,40
```

**Hand verification:**
- Both arrive at t=0
- Job 0 is FCFS head (first in submission order)
- Job 0 fits: 80 ≤ 100 ✓, starts at t=0
- Job 1 cannot fit: 80+60=140 > 100 ✗
- Job 1 must wait for Job 0 to complete
- Job 0 ends at t=50
- Job 1 starts at t=50, ends at t=90

**Expected output:**
```csv
job_idx,submit_time,start_time,end_time,nodes,duration
0,0.0,0.0,50.0,80,50.0
1,0.0,50.0,90.0,60,40.0
```

**Only ONE possible answer.** Job 1 cannot start at any time except t=50.

---

### Test 3: Concurrent Jobs
**File:** `basic_2jobs_input.csv`

**Input:**
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,30,0,pbatch,50
0,40,0,pbatch,60
```

**Hand verification:**
- Both arrive at t=0
- Combined: 30+40=70 ≤ 100 ✓
- Both can run concurrently
- Both start at t=0

**Expected output:**
```csv
job_idx,submit_time,start_time,end_time,nodes,duration
0,0.0,0.0,50.0,30,50.0
1,0.0,0.0,60.0,40,60.0
```

**Only ONE possible answer.** Both must start at t=0.

---

### Test 4: Simple Backfill
**File:** `hand_simple_backfill_input.csv`

**Input:**
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,100
10,10,0,pbatch,20
```

**Hand verification:**
- t=0: Job 0 starts (80 ≤ 100)
- t=10: Job 1 arrives
  - Free: 100-80=20 ≥ 10 ✓
  - Job 1 is FCFS head (only waiting job)
  - Can start immediately
  - Starts at t=10, ends at t=30

**Expected output:**
```csv
job_idx,submit_time,start_time,end_time,nodes,duration
0,0.0,0.0,100.0,80,100.0
1,10.0,10.0,30.0,10,20.0
```

**Note:** This is NOT actually backfilling - Job 1 is the FCFS head!  
**Only ONE possible answer.**

---

### Test 5: Backfill with Waiting FCFS Head
**File:** `hand_backfill_blocked_input.csv`

**Input:**
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,50
0,60,0,pbatch,30
10,10,0,pbatch,30
```

**Hand verification:**
- t=0: Job 0 starts (FCFS head), Job 1 waits
  - Job 1 needs 60 > 20 free nodes
  - Job 1 reservation: t=50
  
- t=10: Job 2 arrives
  - Free: 20 ≥ 10 ✓
  - FCFS head: Job 1 (reservation at t=50)
  - Backfill check: 10 + 30 = 40 ≤ 50 ✓
  - Job 2 CAN backfill, starts at t=10
  
- t=40: Job 2 ends
- t=50: Job 0 ends, Job 1 starts
- t=80: Job 1 ends

**Expected output:**
```csv
job_idx,submit_time,start_time,end_time,nodes,duration
0,0.0,0.0,50.0,80,50.0
1,0.0,50.0,80.0,60,30.0
2,10.0,10.0,40.0,10,30.0
```

**Only ONE possible answer.** This demonstrates true backfilling.

---

## What We Learned

### Mistake in Original Test Design

I initially tried to create a 2-job backfill-blocked test:
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,50
10,10,0,pbatch,60
```

I expected Job 1 to wait until t=50, but the oracle correctly showed it starts at t=10.

**Why I was wrong:**
- Once Job 0 is running, it's not in the wait queue
- Job 1 becomes the FCFS head when it arrives
- As FCFS head, it can start if it fits (10 ≤ 20)
- This is NOT backfilling - it's FCFS!

**Lesson:** Need at least 3 jobs to demonstrate backfilling:
1. Job running (Job 0)
2. Job waiting as FCFS head (Job 1)  
3. Job that backfills (Job 2)

### Why Hand-Traced Tests Matter

These tests verify correctness **without circular dependency:**

```
Hand-traced test → Can verify by inspection → Absolute truth
                        ↓
                   Run oracle → Must match hand trace
                        ↓
                   Run DR_EVT → Must match oracle
```

If the oracle fails a hand-traced test, the oracle is wrong.  
If DR_EVT fails but oracle passes, DR_EVT is wrong.

### Verification Status

✅ All 5 hand-traced tests pass for both oracle and DR_EVT  
✅ Outputs match hand-verified expectations  
✅ Oracle is verified correct (at least for these scenarios)  
✅ DR_EVT is verified correct (matches oracle)  

## Running These Tests

```bash
# Verify oracle
for test in hand_simple_backfill hand_backfill_blocked sequential_wait basic_2jobs; do
    echo "Testing $test..."
    python3 scripts/python_reference_scheduler.py \
      test_traces/correctness/${test}_input.csv --nodes 100
done

# Verify DR_EVT
for test in hand_simple_backfill hand_backfill_blocked sequential_wait basic_2jobs; do
    ./build/simulator test_traces/correctness/${test}_input.csv \
      --total_nodes 100 --trace_format simple --timestamp_format epoch \
      --duration_mode exact --outfile /tmp/${test}.out
    
    python3 scripts/compare_with_oracle.py \
      test_traces/correctness/${test}_reference.csv /tmp/${test}.out
done
```

## Confidence Level

With these hand-traced tests verified:
- **High confidence** the oracle is correct for basic scenarios
- **High confidence** DR_EVT matches the oracle
- **Can now use oracle** for more complex tests with confidence

The hand-traced tests break the circular dependency and provide a foundation of absolute verification.
