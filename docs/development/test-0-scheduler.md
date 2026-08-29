# Test 0: Core Scheduler Correctness

**⚠️ WARNING: This document is MISLEADING and should not be trusted.**

This document was written under the false assumption that Test 0 validates scheduler logic. It does NOT. Test 0 validates **replay mode resource accounting**, not scheduler decision-making.

**Read instead:** [TEST_0_REPLAY_ACCOUNTING.md](TEST_0_REPLAY_ACCOUNTING.md) for accurate information.

**See also:** [test-0-reality-check.md](dev/session-notes/test-0-reality-check.md) for what went wrong.

---

## ~~Why This is Test 0~~ (INCORRECT CLAIMS BELOW)

**This is the most important test** - it validates that the scheduler actually works correctly at a fundamental level, not just that it "doesn't crash."

Most tests verify:
- ✓ Jobs were submitted
- ✓ Jobs completed
- ✓ Process didn't crash

Test 0 verifies:
- ✓ **Resource accounting is correct at every timestep**
- ✓ **No over-subscription ever occurs**
- ✓ **Resources are properly freed**
- ✓ **Backfilling actually works**
- ✓ **Behavior is deterministic**
- ✓ **System correctly handles saturation**

## What It Tests

### 1. Resource State Tracking

Parses complete resource history from simulator output:
```
Job 0 submitted at 0 (80 nodes)
  Resources allocated: 80 nodes, 20/100 remaining
Job 1 submitted at 10 (15 nodes)
  Resources allocated: 15 nodes, 5/100 remaining
Job 0 ended at 100
  Resources freed: 80 nodes, now 85/100 free
```

Creates timeline:
```python
[
    ResourceState(time=0,   free=20, allocated=80, event='start', job=0),
    ResourceState(time=10,  free=5,  allocated=95, event='start', job=1),
    ResourceState(time=100, free=85, allocated=15, event='end',   job=0),
    ...
]
```

### 2. Invariant Validation

At **every single timestep**, verifies:

**Resource Conservation:**
```python
allocated + free == total_nodes
```
If this fails, resources are being created or destroyed incorrectly.

**No Over-Subscription:**
```python
allocated <= total_nodes
free >= 0
```
If this fails, scheduler is double-allocating resources.

**Monotonic Time:**
```python
time[i] >= time[i-1]
```
If this fails, event ordering is broken.

**Accounting Correctness:**
```python
# When job starts:
new_free == old_free - job_nodes

# When job ends:
new_free == old_free + job_nodes
```
If this fails, resource tracking has bugs.

### 3. Backfilling Detection

Looks for evidence that backfilling actually occurred:
- Small jobs starting during high utilization
- Resources being used efficiently
- Queue not blocking small jobs unnecessarily

### 4. Final State Validation

```python
assert final_free_nodes == total_nodes
```
All resources must be freed when simulation ends. If not, there's a resource leak.

### 5. Determinism

Runs the **exact same trace twice**:
```python
states_1 = run_simulation(trace)
states_2 = run_simulation(trace)

assert states_1 == states_2  # Must be identical
```

If states differ, scheduler has:
- Non-deterministic behavior
- Race conditions
- Uninitialized memory
- Random number usage without seed control

## Test Variants

### Test 0.1: Oracle Mode (`limit == actual_duration`)

**Trace**: `scheduler_correctness_oracle.csv`
```csv
job_submit_time,begin_time,end_time,num_nodes,exit_status,queue,time_limit
0,0,100,80,0,pbatch,100    # limit = 100, actual = 100
10,10,30,15,0,pbatch,20    # limit = 20,  actual = 20
```

**What it tests:**
- Scheduler with perfect information
- Optimal backfilling possible
- Reservations are exact
- Best-case utilization

**Run mode:** `--runtime_mode actual`

### Test 0.2: Realistic Mode (`limit > actual_duration`)

**Trace**: `scheduler_correctness_realistic.csv`
```csv
job_submit_time,begin_time,end_time,num_nodes,exit_status,queue,time_limit
0,0,100,80,0,pbatch,200    # limit = 200, actual = 100 (2x overestimate)
10,10,30,15,0,pbatch,40    # limit = 40,  actual = 20 (2x overestimate)
```

**What it tests:**
- Scheduler with uncertainty
- Conservative reservations
- Early job completion freeing resources
- Backfilling with overestimated limits

**Run mode:** `--runtime_mode limit`

### Test 0.3: Determinism

**What it tests:**
- Runs oracle trace twice
- Compares all states
- Verifies bit-identical results

## What Bugs This Catches

### Bug Example 1: Resource Leak
```python
# Code with bug:
def free_resources(job):
    if job.status == SUCCESS:  # Bug: only free on success
        self.free_nodes += job.nodes

# Test 0 catches this:
# Final state: 80/100 free (expected 100/100)
# ❌ FAIL: Resources not fully freed
```

### Bug Example 2: Double Allocation
```python
# Code with bug:
def allocate(job):
    self.free_nodes -= job.nodes
    self.running_jobs.append(job)
    self.free_nodes -= job.nodes  # Bug: subtracted twice!

# Test 0 catches this:
# State[5]: 50 allocated + (-10) free != 100 total
# ❌ FAIL: Resource conservation violated
```

### Bug Example 3: Over-Subscription
```python
# Code with bug:
def can_start(job):
    return True  # Bug: always returns true!

# Test 0 catches this:
# State[8]: 120 allocated > 100 total
# ❌ FAIL: Over-subscription detected
```

### Bug Example 4: Broken Backfilling
```python
# Code with bug:
def try_backfill(job):
    return False  # Bug: never backfills!

# Test 0 catches this:
# Warning: No small jobs started during high utilization
# ❌ FAIL: Backfilling never occurred
```

## Example Output

```
============================================================
TEST 0.1: Scheduler Correctness (Oracle Mode)
============================================================
Testing with limit == actual duration (perfect information)

Parsed 13 resource state changes
Jobs submitted: 10
Jobs completed: 10

============================================================
Scheduler Correctness Validation Report
============================================================
Total events analyzed: 13
Maximum utilization: 95.0%
Validation errors: 0

✅ All invariants validated successfully!
============================================================
```

## Comparison with Other Tests

### Other Tests (e.g., test_basic_execution):
```python
def test_basic_execution():
    metrics = run_simulator("trace.csv")
    assert metrics['jobs_submitted'] == 3  # Just checks count
    assert metrics['jobs_completed'] == 3  # Just checks count
```

**What this proves:** Simulator didn't crash, counted to 3.

**What this doesn't prove:**
- Were resources accounted correctly?
- Did jobs actually get resources?
- Were resources freed?
- Did backfilling work?
- Is behavior deterministic?

### Test 0:
```python
def test_scheduler_correctness():
    output = run_simulator("trace.csv")
    states = parse_resource_trace(output)
    
    for state in states:
        assert state.allocated + state.free == 100
        assert state.allocated <= 100
        assert state.free >= 0
    
    assert final_state.free == 100
    assert backfilling_occurred(states)
    assert is_deterministic(trace)
```

**What this proves:** The scheduler **actually works correctly** at every step.

## When to Run Test 0

**Always run Test 0 first** when:
- Modifying scheduler logic
- Changing resource accounting
- Adding new policies
- Debugging scheduling issues
- Validating a new feature
- Investigating reports of incorrect behavior

If Test 0 passes, the scheduler fundamentals are correct.
If Test 0 fails, **stop** - other tests don't matter if fundamentals are broken.

## Extending Test 0

To test new scenarios:

1. **Create trace with desired behavior:**
   ```csv
   # Example: Test priority inversion
   0,0,100,90,0,pbatch,100     # Large low-priority
   10,10,15,5,0,priority,10    # Small high-priority
   ```

2. **Run Test 0:**
   ```bash
   python3 tests/test_scheduler_correctness.py
   ```

3. **Verify invariants hold:**
   - Resource conservation
   - No over-subscription
   - Expected behavior occurs

## Why This Approach Works

Traditional testing:
- ❌ Tests count outputs ("3 jobs completed")
- ❌ Doesn't verify internal state
- ❌ Bugs can hide behind correct counts
- ❌ Race conditions may pass sometimes

Test 0 approach:
- ✅ Tests complete state history
- ✅ Validates at every timestep
- ✅ Catches bugs immediately when they occur
- ✅ Determinism check catches non-determinism
- ✅ Proves correctness, not just "didn't crash"

## Summary

**Test 0 answers the question:** "Does the scheduler actually work correctly?"

Not:
- "Did it crash?" (that's a smoke test)
- "Were jobs counted?" (that's an integration test)
- "Was output formatted correctly?" (that's a formatting test)

But:
- "Are resources tracked correctly at every moment?"
- "Does backfilling actually work?"
- "Is behavior deterministic and predictable?"
- "Can I trust this scheduler with real workloads?"

**If Test 0 passes, you have a correct scheduler.**  
**If Test 0 fails, fix it before anything else matters.**
