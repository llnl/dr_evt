# Invariant Tests

## Purpose

Invariant tests verify that the implementation maintains critical properties that must ALWAYS hold, regardless of the specific scheduling scenario.

Unlike backfill correctness tests (which test specific algorithm logic), invariant tests ensure fundamental system properties are never violated.

## Invariant Test Suite

### INV-1: Idle System (Empty Wait Queue)
**File:** `inv01_idle_system_input.csv`

**Scenario:**
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,50,0,pbatch,30
100,40,0,pbatch,20
250,60,0,pbatch,40
```

**Timeline:**
```
t=0:     Job 0 starts (50 nodes)
t=30:    Job 0 ends → SYSTEM IDLE
         - No running jobs
         - No waiting jobs
         - All resources free
         
t=30-100: System idle for 70 seconds

t=100:   Job 1 arrives and starts immediately (40 nodes)
t=120:   Job 1 ends → SYSTEM IDLE

t=120-250: System idle for 130 seconds

t=250:   Job 2 arrives and starts immediately (60 nodes)
t=290:   Job 2 ends
```

**What this tests:**

1. **Empty wait queue handling**
   - System correctly handles wait_queue becoming empty
   - No infinite loops or hangs when no jobs waiting
   - Event loop can advance time through idle periods

2. **Idle → Busy transitions**
   - Job arriving after idle period starts correctly
   - Resources properly tracked at 0 during idle
   - No state corruption from previous jobs

3. **Resource accounting through idle periods**
   - Allocated nodes = 0 during idle
   - Free nodes = 100 during idle
   - No phantom allocations

4. **Time advancement**
   - Simulation advances correctly through idle periods
   - Next job starts at its submit_time (not delayed)
   - Current time tracking correct

**Expected output:**
```csv
job_idx,submit_time,start_time,end_time,nodes,duration
0,0.0,0.0,30.0,50,30.0
1,100.0,100.0,120.0,40,20.0
2,250.0,250.0,290.0,60,40.0
```

**Invariants checked:**
- ✅ No jobs overlap (disjoint time intervals)
- ✅ Each job starts exactly at submit_time (system idle)
- ✅ Duration correct: end = start + duration
- ✅ No resource over-subscription
- ✅ All jobs complete

**Why this is important:**

Many simulators fail when:
- Wait queue becomes empty (null pointer, empty set handling)
- System goes completely idle (resource tracking reset issues)
- Long gaps between jobs (time advancement bugs)

This test catches these bugs.

---

## Additional Invariant Tests (Future)

### INV-2: All Jobs Arrive Simultaneously
**Scenario:** N jobs all submit at t=0
**Tests:** Initial burst handling, priority ordering, no race conditions

### INV-3: Jobs Arrive Every Second
**Scenario:** Continuous stream of arrivals
**Tests:** Event queue doesn't grow unbounded, scheduling decisions efficient

### INV-4: One Node System
**Scenario:** total_nodes = 1, all jobs need 1 node
**Tests:** Strict sequential execution, no concurrency bugs

### INV-5: Full System Saturation
**Scenario:** All jobs need 100 nodes (entire system)
**Tests:** Strictly sequential, no over-subscription at 100%

### INV-6: Zero Duration Jobs
**Scenario:** Jobs with duration=0 or very small
**Tests:** Boundary handling, start == end

### INV-7: Very Long Jobs
**Scenario:** Jobs with duration >> all other jobs combined
**Tests:** Backfilling still works, no integer overflow

---

## How Invariant Tests Differ from Correctness Tests

| Aspect | Invariant Tests | Backfill Correctness Tests |
|--------|----------------|---------------------------|
| **Focus** | System properties | Algorithm logic |
| **What** | Never violate constraints | Specific scenarios handled correctly |
| **Examples** | Empty queue, idle system | Backfill blocked by time |
| **Failure** | System crash, corruption | Wrong scheduling decision |
| **Coverage** | Edge cases, boundaries | Normal operation |

**Both are needed:**
- Correctness tests: Algorithm works as designed
- Invariant tests: System doesn't break under edge cases

---

## Running Invariant Tests

```bash
cd /Users/yeom2/work/dr_evt

# Run INV-1: Idle System
./build/simulator tests/test_traces/correctness/inv01_idle_system_input.csv \
  --total_nodes 100 --trace_format simple --timestamp_format epoch \
  --duration_mode exact --outfile /tmp/inv01.out

python3 scripts/compare_with_oracle.py \
  tests/test_traces/correctness/inv01_idle_system_reference.csv /tmp/inv01.out
```

**Expected result:**
```
✅ VERIFICATION PASSED
```

---

## Invariant Checking Script

Beyond comparing outputs, we should verify invariants programmatically:

```python
def check_invariants(output, total_nodes):
    """Verify invariants hold in simulation output."""
    
    # Invariant 1: No over-subscription
    events = []
    for job in output:
        events.append((job.start_time, +job.nodes))
        events.append((job.end_time, -job.nodes))
    events.sort()
    
    allocated = 0
    for time, delta in events:
        allocated += delta
        assert 0 <= allocated <= total_nodes, \
            f"Invariant violated at t={time}: {allocated} nodes"
    
    # Invariant 2: Causality
    for job in output:
        assert job.start_time >= job.submit_time, \
            f"Job {job.id} starts before submission"
    
    # Invariant 3: Duration
    for job in output:
        assert job.end_time == job.start_time + job.duration, \
            f"Job {job.id} duration incorrect"
    
    # Invariant 4: Completeness
    assert len(output) == expected_count, "Missing jobs"
    
    # Invariant 5: Idle periods (for idle system test)
    # During idle periods, allocated == 0
    for gap_start, gap_end in idle_periods:
        jobs_in_gap = [j for j in output 
                       if j.start_time < gap_end and j.end_time > gap_start]
        assert len(jobs_in_gap) == 0, \
            f"Jobs running during idle period [{gap_start}, {gap_end}]"
    
    print("✅ All invariants hold")
```

---

## Why INV-1 (Idle System) Is Critical

**Real-world scenarios:**
1. **Night/weekend gaps** - Clusters often idle overnight or weekends
2. **Batch job completion** - All jobs finish before next batch arrives
3. **Maintenance windows** - Deliberate idle periods
4. **Low utilization** - Small clusters with sporadic workload

**Bugs it catches:**
- **Event loop hangs** - Infinite loop when wait_queue empty
- **Time advancement fails** - Can't advance past last event
- **Resource tracking reset** - Nodes stay "allocated" after idle
- **State corruption** - Previous job state affects next job
- **Null pointer** - Code assumes wait_queue non-empty

**Historical bugs in schedulers:**
- Slurm early versions: Hung when no jobs in queue
- PBS: Resource accounting wrong after idle period
- LSF: Next job delayed after idle (time advancement bug)

This test prevents these classes of bugs.

---

## Verification Status

| Test | Status | Notes |
|------|--------|-------|
| INV-1: Idle System | ✅ PASS | Both Python reference and DR_EVT pass |

---

## Integration with Test Suite

Invariant tests are part of the comprehensive verification:

```
1. Hand-traceable tests (5 tests) - Basic correctness
2. Invariant checking - Mathematical properties
3. Contradiction tests - Impossible scenarios
4. Self-consistency - Deterministic behavior
5. Backfill correctness (10 tests) - Algorithm logic
6. Invariant tests (INV-1+) - Edge cases         ← NEW
7. Cross-validation - Both implementations agree
8. Large-scale - 500+ job traces
```

All tests must pass for full verification.
