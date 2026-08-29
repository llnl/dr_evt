# Test 0: Replay Resource Accounting Validation

**⚠️ IMPORTANT:** This test validates **trace replay mode** resource accounting, NOT scheduler logic.

## What This Test Actually Does

The simulator reads traces with pre-recorded `begin_time` and `end_time` values and **replays** them. This test validates that during replay:

✅ **Resource accounting is correct** at every timestep  
✅ **No resource leaks** occur  
✅ **No over-subscription** happens  
✅ **Replay is deterministic** (same trace → identical states)

## What This Test Does NOT Do

❌ **Does NOT test scheduler decision-making** - The scheduler doesn't compute start times in replay mode  
❌ **Does NOT test backfilling algorithm** - Jobs start at their pre-recorded times from the trace  
❌ **Does NOT test priority policies** - Job order comes from the trace, not from scheduler  
❌ **Does NOT validate scheduling logic** - The scheduler's `submit_jobs()` method is bypassed

## Why This Distinction Matters

The scheduler implementation exists ([src/sim/scheduler.cpp](../src/sim/scheduler.cpp)) with full EASY/Conservative backfilling, but it's **not exercised** because:

1. Current trace format requires `begin_time` and `end_time` columns
2. When those columns are present, simulator runs in **replay mode**
3. Replay mode uses recorded times, not scheduler-computed times
4. Scheduler code exists but its decision-making logic never runs

See [test-0-reality-check.md](dev/session-notes/test-0-reality-check.md) for full analysis.

## What Test 0 Validates

### 1. Resource State Tracking During Replay

Parses complete resource history from simulator output:
```
Job 0 submitted at 0 (80 nodes)
  Resources allocated: 80 nodes, 20/100 remaining
Job 1 submitted at 10 (15 nodes)
  Resources allocated: 15 nodes, 5/100 remaining
Job 0 ended at 100
  Resources freed: 80 nodes, now 85/100 free
```

Creates timeline of resource states for validation.

### 2. Invariant Validation at Every Timestep

**Resource Conservation:**
```python
allocated + free == total_nodes  # Must always be true
```

**No Over-Subscription:**
```python
allocated <= total_nodes
free >= 0
```

**Monotonic Time:**
```python
time[i] >= time[i-1]  # No time travel
```

**Accounting Correctness:**
```python
# When job starts: free decreases by job_nodes
# When job ends:   free increases by job_nodes
```

### 3. Final State Validation

```python
assert final_free_nodes == total_nodes  # All resources returned
```

### 4. Determinism

Runs the same trace twice and verifies identical resource states.

### 5. Trace Pattern Detection

Checks if the trace shows patterns that look like backfilling occurred (NOT that the scheduler computed backfilling).

## Test Variants

### Test 0.1: Oracle Mode Trace
- Trace has `limit == actual_duration`
- Perfect information scenario
- Validates replay with exact time limits

### Test 0.2: Realistic Mode Trace
- Trace has `limit > actual_duration` (overestimated)
- More realistic user behavior
- Validates replay with overestimated limits

### Test 0.3: Determinism
- Runs oracle trace twice
- Compares all states
- Verifies bit-identical replay results

## What Bugs This Catches

### Bug Example 1: Resource Leak During Replay
```python
# Bug: only free on success
def free_resources(job):
    if job.status == SUCCESS:
        self.free_nodes += job.nodes
# Test 0 catches: Final state not 100/100 free
```

### Bug Example 2: Double Allocation
```python
# Bug: subtract twice
def allocate(job):
    self.free_nodes -= job.nodes
    self.free_nodes -= job.nodes
# Test 0 catches: Resource conservation violated
```

### Bug Example 3: Over-Subscription
```python
# Bug: no capacity check
def can_start(job):
    return True
# Test 0 catches: allocated > total
```

## Running Test 0

```bash
# Run standalone
python3 tests/test_scheduler_correctness.py

# Expected output:
# ✅ PASS  Replay Resource Accounting (Oracle Mode Trace)
# ✅ PASS  Replay Resource Accounting (Realistic Mode Trace)
# ✅ PASS  Determinism
# ✅ ALL TESTS PASSED - Replay resource accounting is correct!
```

## Value of This Test

Even though it doesn't test scheduler logic, Test 0 is valuable because:

1. **Validates core resource tracking** - The replay engine must track resources correctly
2. **Catches accounting bugs** - Resource leaks, double-free, over-subscription
3. **Ensures determinism** - Critical for reproducibility
4. **Foundation for future work** - If replay is broken, scheduler tests would be meaningless

Think of it as a **sanity check** that the simulator's resource tracking infrastructure works before adding scheduler testing on top.

## What Would Actually Test The Scheduler

To test the scheduler, we would need:

1. **Simulation-only trace format** without `begin_time`/`end_time`:
   ```csv
   job_submit_time,num_nodes,queue,time_limit
   0,80,pbatch,100
   10,15,pbatch,20
   ```

2. **Modified trace parser** that doesn't require those columns

3. **Scheduler tests** that validate:
   - Backfilling decisions are correct
   - Priority policies are respected
   - Resources are allocated efficiently
   - Reservations work as expected

This is not currently implemented.

## Summary

**Test 0 = Replay Mode Resource Accounting Validation**

- ✅ Tests that replay doesn't break resource invariants
- ✅ Catches bugs in resource tracking during replay
- ❌ Does NOT test scheduler decision-making
- ❌ Does NOT validate backfilling algorithm
- ❌ Does NOT test scheduling policies

**Rename proposal:** `test_replay_accounting.py` would be more accurate than `test_scheduler_correctness.py`.

See [test-0-reality-check.md](dev/session-notes/test-0-reality-check.md) for complete analysis of what went wrong.
