# 34_backfill_overallocation

## Scenario
**CRITICAL TEST**: Exposes bug where backfill loop selects multiple jobs without updating available_nodes between selections, causing over-allocation.

**System**: 100 nodes total

## Jobs
- J0: submit=0, nodes=70, duration=100
- J1: submit=10, nodes=40, duration=200 (FCFS head, will wait)
- J2: submit=10, nodes=30, duration=50 (candidate for backfill)
- J3: submit=10, nodes=30, duration=50 (candidate for backfill)

## Timeline

### t=0
- J0 starts (70 nodes) → 70 used, 30 free

### t=10
- J1, J2, J3 arrive
- J1 needs 40 (only 30 free) → WAITS, becomes FCFS head
- J1 gets reservation for t=100 when J0 completes
- J2 needs 30 (30 free) → Could backfill?
  - Check: J2 duration 50, ends at t=60 < t=100 (J1's reservation) ✓
  - Check: J2 needs 30, available 30 ✓
  - **J2 is eligible**
- J3 needs 30 (30 free) → Could backfill?
  - Check: J3 duration 50, ends at t=60 < t=100 ✓
  - Check: J3 needs 30, available 30 ✓
  - **J3 is eligible**

## The Bug

**Buggy backfill loop** (selects both J2 and J3):
```cpp
available = 30;
vector<Job*> to_backfill;

for (job in waiting_queue) {
    if (job == fcfs_head) continue;  // Skip J1
    
    // Check J2
    if (job.nodes <= available) {  // 30 <= 30 ✓
        if (job.end_time < reservation_time) {  // 60 < 100 ✓
            to_backfill.push_back(job);
            // BUG: available is NOT updated here!
        }
    }
    
    // Check J3 with SAME available value!
    if (job.nodes <= available) {  // 30 <= 30 ✓
        if (job.end_time < reservation_time) {  // 60 < 100 ✓
            to_backfill.push_back(job);
            // BUG: Now we have 60 nodes committed but only 30 available!
        }
    }
}

// Start all selected jobs
for (job in to_backfill) {
    start_job(job);  // OVER-ALLOCATION!
}
```

**Result**: Both J2 and J3 selected for backfilling, but 30 + 30 = 60 > 30 available!

## Correct Behavior

**Only ONE job should backfill**:

```cpp
available = 30;

for (job in waiting_queue) {
    if (job == fcfs_head) continue;
    
    if (job.nodes <= available) {  // Check current available
        if (job.end_time < reservation_time) {
            start_job(job);
            available -= job.nodes;  // ✓ Update available!
            record_resource_state();
        }
    }
}
```

**Correct schedule**:
- J0: [0, 100]
- J1: [100, 300]  (FCFS head, starts when J0 completes)
- J2: [10, 60]    (backfills at t=10)
- J3: [60, 110]   (waits for J2 to complete, then backfills)

**OR** (depending on queue order):
- J0: [0, 100]
- J1: [100, 300]
- J2: [60, 110]   (waits)
- J3: [10, 60]    (backfills at t=10)

Only ONE of {J2, J3} should backfill at t=10, not both!

## Expected Resource Events at t=10

**Correct** (only one backfills):
```
t=10: J2 starts → 100 used, 0 free
```

**Buggy C++ might do**:
```
t=10: J2 starts → 100 used
t=10: J3 starts → 130 used, -30 free (INVALID!)
```

Or the simulator might catch the over-allocation and prevent J3 from starting, but the backfill selection logic is still wrong.

## What This Tests

1. **Backfill selection without resource updates**: Checks if available_nodes is updated after each selection
2. **Over-allocation detection**: Exposes if multiple jobs are selected when only one fits
3. **Resource constraint enforcement**: System should never exceed 100 nodes

## Expected Schedule

```
J0: [0, 100]
J1: [100, 300]
J2: [10, 60]     (backfills - selected first in queue)
J3: [60, 110]    (waits for J2, then backfills)
```

## Why This Exposes the Bug

With 30 nodes free and two 30-node jobs, the buggy loop checks:
- J2: 30 <= 30 ✓ → selected
- J3: 30 <= 30 ✓ → selected (still checking against original 30!)

Both get selected even though together they need 60 nodes.

The symptom could be:
1. Over-allocation (invalid state)
2. Second job fails to start (caught by start_job check)
3. Wrong resource counts in trace
4. Different job schedule than expected

This is exactly what we see in test 21 at t=368!
