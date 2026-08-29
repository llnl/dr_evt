# EASY Backfilling Implementation Correctness

## What Are We Verifying?

We're verifying that **our C++ implementation matches the EASY backfilling specification**.

This is different from proving EASY backfilling theory (e.g., "no starvation") - we assume the algorithm is correct, and verify our implementation matches it.

## Verification Approach

**Method:** Compare our implementation against a reference implementation (Python oracle)

**Criterion:** For identical inputs, outputs must match exactly (start_time, end_time for all jobs)

If outputs match → implementation is correct
If outputs differ → implementation has a bug

## Correctness Properties (Testable Invariants)

These are properties we can actually test and verify:

### 1. Output Matching

**Property:** Given the same input trace, our simulator produces the same output as the reference implementation.

**How to verify:**
```bash
python3 python_reference_scheduler.py input.csv --output expected.csv
./simulator input.csv --outfile actual.csv
compare(expected.csv, actual.csv)
```

**Test coverage:**
- ✅ easy_5jobs - comprehensive case
- ✅ basic_2jobs - concurrent jobs
- ✅ backfill_3jobs - backfilling scenario
- ✅ sequential_wait - FCFS ordering
- ✅ backfill_blocked - backfill constraint
- ✅ idle_gap - gap detection

**What this verifies:** If all outputs match, our implementation is behaviorally equivalent to the reference.

---

### 2. Never Over-Subscribe Resources

**Invariant:** At all times: `allocated_nodes ≤ total_nodes`

**How to verify:** 
- Track resource usage over time
- Assert at every scheduling decision
- Check output: sum of concurrent jobs never exceeds total

**Test case:** easy_5jobs
```
At t=10:
  Job 0: 80 nodes (running)
  Job 1: 15 nodes (running)
  Total: 95 ≤ 100 ✓

At t=20 if Job 2 (60 nodes) were to start:
  Total: 80 + 15 + 60 = 155 > 100 ✗ VIOLATION
```

**How we caught this bug:** Original implementation allowed Job 2 to start at t=30, causing 150 nodes allocated from 100 available.

---

### 3. Jobs Start At or After Submit Time

**Invariant:** For all jobs: `start_time ≥ submit_time`

**How to verify:** Check output CSV
```bash
awk -F',' 'NR>1 { if ($2 > $3) print "VIOLATION: Job",$1 }' output.csv
```

**Test case:** All tests implicitly verify this

**Special case:** Jobs at t=0
- Bug we found: Job with submit_time=0 started at t=10
- Violation: 10 > 0
- Root cause: Pre-loop didn't check for jobs at initial time

---

### 4. Jobs Run for Correct Duration

**Invariant:** For all jobs: `end_time = start_time + duration`

**How to verify:** Check output
```python
for job in output:
    expected_end = job.start_time + job.duration
    assert job.end_time == expected_end
```

**Test case:** All tests verify this

---

### 5. All Jobs Complete

**Invariant:** `jobs_completed == jobs_submitted`

**How to verify:** 
- Count jobs in input trace
- Count jobs in output trace
- Assert equal

**Test case:** All tests verify this

**Example:**
```
Input: 5 jobs
Output: 5 jobs ✓

If output had 4 jobs → Bug (job lost)
If output had 6 jobs → Bug (duplicate)
```

---

### 6. FCFS Head Starts at Reservation Time

**Invariant:** The first waiting job starts at or before its calculated reservation time.

**How to verify:** 
- Identify FCFS head at time T
- Calculate its reservation R
- Verify it starts at time ≤ R

**Test case:** sequential_wait
```
Job 0: submit=0, nodes=80 → FCFS head, starts immediately at t=0
Job 1: submit=0, nodes=60 → FCFS head after Job 0
  - Cannot fit at t=0 (only 20 free)
  - Reservation = 50 (when Job 0 ends)
  - Actually starts at t=50 ✓
```

**This verifies:** FCFS guarantee is maintained

---

### 7. Backfilled Jobs Don't Delay FCFS Head

**Invariant:** If job J backfills at time T with reservation R:
```
T + J.duration ≤ R
```

**How to verify:** Check that when a non-FCFS job starts:
- Identify current FCFS head
- Get FCFS head's reservation
- Verify: current_time + job.duration ≤ reservation

**Test case:** backfill_blocked
```
Job 0: t=0, 80 nodes, 50s → starts t=0
Job 1: t=10, 60 nodes, 30s → FCFS head, reservation=50
Job 2: t=20, 15 nodes, 40s
  - Could fit at t=20 (20 nodes free)
  - Would end at t=60 (20+40)
  - Reservation is t=50
  - 60 > 50 → BLOCKED from backfilling
  - Must wait until t=50
```

**This verifies:** Backfill constraint is enforced

---

## What We DON'T Need to Verify

These are **algorithm properties** (theory), not implementation tests:

### ❌ "No Starvation"
- **Why not testable:** Would require infinite time or adversarial workload
- **What we test instead:** Jobs complete in expected order (via oracle comparison)

### ❌ "Work Conservation" 
- **Why not directly testable:** Need to prove no better schedule exists
- **What we test instead:** Oracle comparison (oracle is work-conserving by design)

### ❌ "Optimal Utilization"
- **Why not testable:** EASY is a heuristic, not optimal
- **What we test instead:** Output matches oracle (which implements same heuristic)

### ❌ "Fairness"
- **Why not testable:** Subjective metric
- **What we test instead:** FCFS ordering maintained (objective property)

## Test-Driven Verification Matrix

| Invariant | Test Method | Test Cases |
|-----------|-------------|------------|
| Output matching | Compare with oracle | All 6 essential tests |
| No over-subscription | Resource tracking | easy_5jobs (caught bug) |
| Start ≥ submit | Output validation | easy_5jobs (caught bug) |
| Duration correct | Output validation | All tests |
| All jobs complete | Count jobs | All tests |
| FCFS starts at reservation | Trace analysis | sequential_wait |
| Backfill constraint | Trace analysis | backfill_blocked |

## Verification Confidence

**High confidence criteria:**
1. ✅ All 6 oracle comparison tests pass
2. ✅ Tests cover all scheduling scenarios:
   - Jobs at t=0
   - Concurrent jobs
   - Backfilling
   - FCFS waiting
   - Backfill blocking
   - Idle gaps
3. ✅ No invariant violations detected

**Result:** Our implementation is correct with respect to the reference implementation.

## What Bugs Did We Catch?

### Bug 1: Resource Over-Subscription
**Invariant violated:** `allocated > total`
**Test that caught it:** easy_5jobs  
**Symptom:** Job 2 started when insufficient resources
**Fix:** Refactored scheduler logic

### Bug 2: Jobs at t=0 Not Scheduled  
**Invariant violated:** `start_time > submit_time` for Job 0
**Test that caught it:** easy_5jobs (oracle comparison)
**Symptom:** Job 0 started at t=10 instead of t=0
**Fix:** Added pre-loop check for jobs at initial time

**Both bugs were caught by oracle comparison** - demonstrating the value of reference implementation testing.

## Continuous Verification

To maintain correctness:

```bash
# Run all essential tests
for test in easy_5jobs basic_2jobs backfill_3jobs sequential_wait backfill_blocked idle_gap; do
    ./build/simulator tests/test_traces/correctness/${test}_input.csv \
      --total_nodes 100 --trace_format simple --timestamp_format epoch \
      --duration_mode exact --outfile /tmp/${test}.out
    
    python3 scripts/compare_with_oracle.py \
      tests/test_traces/correctness/${test}_reference.csv /tmp/${test}.out \
      || echo "❌ FAILED: $test"
done
```

**All tests must pass** before considering the implementation correct.

## Correctness vs Performance

**Correctness:**
- Outputs match reference implementation
- Invariants never violated
- Verified by tests

**Performance:**
- How fast it runs
- Measured separately
- Not a correctness concern

Example: A simulator that's 1000x slower but produces identical output is still **correct**.

## Summary

**Implementation correctness = behavioral equivalence to reference**

We verify this by:
1. **Oracle comparison** - outputs match for all test cases
2. **Invariant checking** - constraints never violated
3. **Bug detection** - caught 2 bugs via oracle comparison

We DON'T verify theoretical properties like "no starvation" or "optimal utilization" - those are algorithm theory, not implementation testing.

**Verification status:** ✅ All 6 essential tests pass → Implementation is correct
