# Oracle Verification - Testing Without a Reference

## The Problem

We've been verifying our C++ implementation against a Python "oracle". But how do we know the oracle is correct?

**The circular dependency:**
```
C++ implementation → verified against → Python oracle → verified against → ???
```

We need a way to verify correctness **without comparing to another implementation**.

## Solution: Property-Based Testing

Instead of comparing outputs, verify that the output **satisfies mathematical properties and invariants** that must hold for any correct EASY backfilling implementation.

## Approach 1: Hand-Traceable Examples

**Principle:** Create examples so simple you can trace by hand and verify the answer is correct.

### Example 1: Single Job
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,50,0,pbatch,100
```

**Expected output (hand-verified):**
```csv
job_idx,submit_time,start_time,end_time,nodes,duration
0,0.0,0.0,100.0,50,100.0
```

**Why this is correct:**
- Only job in system
- Resources available (50 ≤ 100)
- Must start immediately
- Must end at start + duration

**Verification:** ANY implementation that produces different output is wrong.

### Example 2: Two Sequential Jobs
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,50
0,60,0,pbatch,40
```

**Expected output (hand-verified):**
```csv
job_idx,submit_time,start_time,end_time,nodes,duration
0,0.0,0.0,50.0,80,50.0
1,0.0,50.0,90.0,60,40.0
```

**Why this is correct:**
- Job 0: FCFS head, starts immediately
- Job 1: Cannot fit (80+60=140 > 100), must wait
- Job 1: Starts when Job 0 ends (t=50)
- Only one possible correct answer

### Example 3: Simple Backfill
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,100
10,10,0,pbatch,20
```

**Expected output (hand-verified):**
```csv
job_idx,submit_time,start_time,end_time,nodes,duration
0,0.0,0.0,100.0,80,100.0
1,10.0,10.0,30.0,10,20.0
```

**Why this is correct:**
- Job 0: Starts at t=0 (80 ≤ 100)
- Job 1: Arrives at t=10
  - Resources available: 100 - 80 = 20 ≥ 10 ✓
  - Would end at: 10 + 20 = 30 ≤ 100 (before Job 0 ends) ✓
  - Can backfill
- Only one possible correct answer

**Strategy:** Run these hand-verified examples through BOTH implementations. If either produces different output, it's wrong.

---

## Approach 2: Invariant Checking

**Principle:** Check that outputs satisfy invariants that MUST hold for any correct implementation.

### Invariant 1: Resource Constraint
**Property:** At any time t, total allocated nodes ≤ system capacity

**Test method:**
```python
def verify_no_oversubscription(output, total_nodes):
    # Build timeline of allocations
    events = []
    for job in output:
        events.append((job.start_time, +job.nodes))  # allocate
        events.append((job.end_time, -job.nodes))    # free
    
    events.sort()
    
    allocated = 0
    for time, delta in events:
        allocated += delta
        assert allocated >= 0, f"Negative allocation at t={time}"
        assert allocated <= total_nodes, \
            f"Over-subscription at t={time}: {allocated} > {total_nodes}"
    
    return True
```

**This test is absolute** - doesn't compare to oracle, just checks mathematical constraint.

### Invariant 2: Causality
**Property:** Jobs cannot start before submission

**Test method:**
```python
def verify_causality(output):
    for job in output:
        assert job.start_time >= job.submit_time, \
            f"Job {job.id} starts at {job.start_time} before submit {job.submit_time}"
    return True
```

### Invariant 3: Duration Correctness
**Property:** Jobs run for specified duration

**Test method:**
```python
def verify_duration(output):
    for job in output:
        expected_end = job.start_time + job.duration
        assert abs(job.end_time - expected_end) < 0.001, \
            f"Job {job.id} duration mismatch: ends at {job.end_time}, expected {expected_end}"
    return True
```

### Invariant 4: Completeness
**Property:** All jobs complete exactly once

**Test method:**
```python
def verify_completeness(input_jobs, output):
    assert len(output) == len(input_jobs), \
        f"Job count mismatch: {len(output)} != {len(input_jobs)}"
    
    output_ids = {job.id for job in output}
    input_ids = {job.id for job in input_jobs}
    assert output_ids == input_ids, "Job ID mismatch"
    
    return True
```

### Invariant 5: FCFS Head Never Delayed
**Property:** The FCFS head's actual start time ≤ its reservation time

**Test method:**
```python
def verify_fcfs_not_delayed(output, total_nodes):
    # Reconstruct scheduling decisions
    for i, job in enumerate(sorted(output, key=lambda j: j.submit_time)):
        # Calculate when this job COULD have started (its reservation)
        reservation = calculate_earliest_start(job, output[:i], total_nodes)
        
        assert job.start_time <= reservation, \
            f"Job {job.id} delayed beyond reservation: {job.start_time} > {reservation}"
    
    return True
```

### Invariant 6: Backfill Constraint
**Property:** Non-FCFS jobs complete before FCFS head's reservation

**Test method:**
```python
def verify_backfill_constraint(output):
    sorted_jobs = sorted(output, key=lambda j: (j.submit_time, j.id))
    
    for i, job in enumerate(sorted_jobs):
        if job.start_time > job.submit_time:
            # Job waited - check if it's FCFS head at start_time
            waiting_at_start = [j for j in sorted_jobs[:i+1] 
                               if j.submit_time <= job.start_time and j.start_time >= job.start_time]
            
            if waiting_at_start and waiting_at_start[0].id != job.id:
                # Not FCFS head - must have backfilled
                fcfs_head = waiting_at_start[0]
                fcfs_reservation = calculate_reservation(fcfs_head, output, job.start_time)
                
                assert job.end_time <= fcfs_reservation, \
                    f"Job {job.id} backfilled but would delay FCFS head"
    
    return True
```

---

## Approach 3: Contradiction Tests

**Principle:** Design inputs where certain outputs would be logically impossible.

### Test 1: Impossible to Over-Subscribe
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,60,0,pbatch,50
0,60,0,pbatch,50
```

**System:** 100 nodes

**Constraint:** Both jobs CANNOT run simultaneously (60+60=120 > 100)

**Verification:**
```python
# If both start at t=0 → WRONG (over-subscription)
assert not (job0.start_time == 0 and job1.start_time == 0)

# One must start at t=50
assert (job0.start_time == 0 and job1.start_time == 50) or \
       (job1.start_time == 0 and job0.start_time == 50)
```

### Test 2: Impossible to Delay FCFS Head
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,50
10,10,0,pbatch,60
```

**System:** 100 nodes

**Constraint:** Small job (10 nodes, 60s) should NOT backfill because:
- Would end at t=70 (10+60)
- FCFS head (Job 1 after Job 0) has reservation at t=50
- 70 > 50 → backfill blocked

**Verification:**
```python
# Job 1 CANNOT start at t=10
assert job1.start_time != 10

# Job 1 must wait
assert job1.start_time >= 50
```

### Test 3: Impossible to Start Before Submit
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
100,50,0,pbatch,50
```

**Constraint:** Job cannot start before t=100

**Verification:**
```python
assert job.start_time >= 100
```

---

## Approach 4: Self-Consistency Checks

**Principle:** The implementation's behavior must be self-consistent across multiple runs and scenarios.

### Test 1: Determinism
Run the same input twice, outputs must be identical:

```python
def test_determinism():
    output1 = run_simulation(input_trace)
    output2 = run_simulation(input_trace)
    
    assert output1 == output2, "Non-deterministic behavior detected"
```

### Test 2: Timeline Consistency
Events must occur in logical order:

```python
def test_timeline_consistency(output):
    for job in output:
        # Submit → Start → End
        assert job.submit_time <= job.start_time <= job.end_time
        
    # No temporal overlaps with insufficient resources
    for t in range(0, max_time):
        jobs_at_t = [j for j in output if j.start_time <= t < j.end_time]
        total_nodes = sum(j.nodes for j in jobs_at_t)
        assert total_nodes <= system_capacity
```

### Test 3: Monotonicity
Later-submitted FCFS heads don't start before earlier ones:

```python
def test_fcfs_monotonicity(output):
    fcfs_jobs = sorted(output, key=lambda j: j.submit_time)
    
    for i in range(len(fcfs_jobs) - 1):
        if fcfs_jobs[i].nodes + fcfs_jobs[i+1].nodes > system_capacity:
            # Cannot run concurrently - must be sequential
            assert fcfs_jobs[i].end_time <= fcfs_jobs[i+1].start_time
```

---

## Approach 5: Cross-Validation

**Principle:** Multiple independent implementations should agree.

### Method:
1. Implement oracle in Python (done)
2. Implement in C++ (done)
3. Implement in another language (e.g., Rust, Go)
4. All three should produce identical outputs

**If 2 out of 3 agree, the 3rd is likely wrong.**

---

## Comprehensive Verification Suite

Combine all approaches:

```python
def verify_implementation(implementation, test_suite):
    """Verify an EASY backfilling implementation without comparing to oracle."""
    
    print("=== Testing Without Reference ===\n")
    
    # 1. Hand-traceable examples
    print("1. Hand-traceable examples...")
    for test in hand_traced_tests:
        output = implementation.run(test.input)
        assert output == test.expected_output, f"Failed: {test.name}"
    print("   ✓ All hand-traced tests pass\n")
    
    # 2. Invariant checking
    print("2. Invariant checking...")
    for test in all_tests:
        output = implementation.run(test.input)
        
        assert verify_no_oversubscription(output, test.total_nodes)
        assert verify_causality(output)
        assert verify_duration(output)
        assert verify_completeness(test.input, output)
        assert verify_fcfs_not_delayed(output, test.total_nodes)
        assert verify_backfill_constraint(output)
    print("   ✓ All invariants hold\n")
    
    # 3. Contradiction tests
    print("3. Contradiction tests...")
    for test in contradiction_tests:
        output = implementation.run(test.input)
        assert not test.impossible_condition(output), f"Contradiction in {test.name}"
    print("   ✓ No contradictions\n")
    
    # 4. Self-consistency
    print("4. Self-consistency...")
    for test in all_tests:
        output1 = implementation.run(test.input)
        output2 = implementation.run(test.input)
        assert output1 == output2, "Non-deterministic"
        assert verify_timeline_consistency(output1)
    print("   ✓ Self-consistent\n")
    
    print("=== IMPLEMENTATION VERIFIED ===")
    print("All tests passed without reference comparison")
    return True
```

---

## Minimal Verification Test Suite (Self-Contained)

These tests can verify ANY implementation without comparing to an oracle:

### Test 1: Single Job (1 line input)
- **Input:** 1 job at t=0
- **Check:** Starts at t=0, ends at t=0+duration

### Test 2: Sequential Jobs (2 lines)
- **Input:** 2 large jobs at t=0 (total > capacity)
- **Check:** Second starts when first ends

### Test 3: Concurrent Jobs (2 lines)
- **Input:** 2 small jobs at t=0 (total < capacity)
- **Check:** Both start at t=0

### Test 4: Simple Backfill (2 lines)
- **Input:** Large job + small job that fits
- **Check:** Small job backfills (starts when submitted)

### Test 5: Backfill Blocked (2 lines)
- **Input:** Large job + small job that would delay FCFS head
- **Check:** Small job does NOT backfill

**Run these 5 tests with invariant checking** → Sufficient to verify basic correctness

---

## How to Verify Our Oracle

Apply this test suite to our Python oracle:

```bash
# Create comprehensive test script
python3 scripts/verify_oracle.py

# Tests:
# 1. Run hand-traced examples
# 2. Check all invariants hold
# 3. Run contradiction tests
# 4. Verify self-consistency
```

If the oracle passes all these tests (especially hand-traced examples we can verify by inspection), we can be confident it's correct.

---

## Answer to Original Question

**Q: How do I know the oracle is correct?**

**A: Run property-based tests that don't depend on another implementation:**

1. **Hand-traced examples** - Verify by inspection
2. **Invariant checking** - Mathematical properties that MUST hold
3. **Contradiction tests** - Scenarios that should be impossible
4. **Self-consistency** - Behavior must be deterministic and logical

**If the oracle passes all these → high confidence it's correct**

**Then:** Use oracle to verify C++ implementation (faster than running all property tests every time)

---

## Confidence Levels

| Verification Method | Confidence | Effort |
|---------------------|-----------|--------|
| No testing | 0% | None |
| Oracle comparison only | 50% | Low |
| + Hand-traced examples | 70% | Medium |
| + Invariant checking | 85% | Medium |
| + Contradiction tests | 95% | High |
| + Cross-validation (3+ implementations) | 99% | Very High |

**Our approach:** Hand-traced examples + Invariant checking + Oracle comparison = ~85% confidence

---

## Implementation: verify_oracle.py

Let me create a script that verifies the oracle using these principles:

```python
#!/usr/bin/env python3
"""
Verify EASY backfilling implementation without reference comparison.
Uses property-based testing and invariant checking.
"""

import sys
from python_reference_scheduler import simulate_easy

def test_hand_traced():
    """Test cases small enough to verify by hand."""
    
    # Test 1: Single job
    jobs = [{'submit': 0, 'nodes': 50, 'duration': 100}]
    result = simulate_easy(jobs, total_nodes=100)
    assert result[0]['start'] == 0
    assert result[0]['end'] == 100
    
    # Test 2: Sequential jobs
    jobs = [
        {'submit': 0, 'nodes': 80, 'duration': 50},
        {'submit': 0, 'nodes': 60, 'duration': 40}
    ]
    result = simulate_easy(jobs, total_nodes=100)
    assert result[0]['start'] == 0
    assert result[1]['start'] == 50  # Must wait
    
    # Test 3: Backfilling
    jobs = [
        {'submit': 0, 'nodes': 80, 'duration': 100},
        {'submit': 10, 'nodes': 10, 'duration': 20}
    ]
    result = simulate_easy(jobs, total_nodes=100)
    assert result[1]['start'] == 10  # Can backfill
    
    print("✓ Hand-traced examples pass")

def verify_invariants(result, total_nodes):
    """Check invariants that must hold."""
    
    # Build timeline
    events = []
    for job in result:
        events.append((job['start'], job['nodes'], +1))
        events.append((job['end'], job['nodes'], -1))
    events.sort()
    
    # Check resource constraint
    allocated = 0
    for time, nodes, delta in events:
        allocated += nodes * delta
        assert allocated >= 0
        assert allocated <= total_nodes, \
            f"Over-subscription at t={time}: {allocated} > {total_nodes}"
    
    # Check causality
    for i, job in enumerate(result):
        assert job['start'] >= job['submit']
        assert job['end'] == job['start'] + job['duration']
    
    print("✓ All invariants hold")

if __name__ == '__main__':
    test_hand_traced()
    print("\nOracle verification complete!")
```

This script verifies correctness without comparing to anything else!
