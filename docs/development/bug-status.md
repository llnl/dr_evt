# Bug Status Summary - RESOLVED

## Bugs Found and Fixed

### Original Bug (Aug 27 - Before Refactoring)
**File:** `sim_5jobs_output.csv`
**Issue:** Resource over-subscription
- Job 2 starts at t=30 when only 20 nodes free (needs 60)
- Total: 150 nodes allocated from 100-node system
- Caused over-subscription of 50 nodes
- **STATUS:** ✅ FIXED by refactoring

### Timing Bug (After Initial Refactoring)
**File:** `sim_5jobs_oracle_test.out`
**Issue:** Jobs starting with 10-second delay
- Job 0: submit=0, but starts at t=10 (should start at t=0)
- Job 1: submit=10, starts at t=10 (correct by coincidence)
- Job 2: starts at t=110 (should be t=100)
- **STATUS:** ✅ FIXED

## Root Cause

The event loop didn't handle jobs arriving at t=0 (the initial current_time). 

**Problem:** The main event loop only checked for "current arrivals" when there were no future arrivals (`next_arrival == max`). At t=0:
- Job 0 has submit_time=0 (eligible NOW)
- Job 1 has submit_time=10 (future arrival)
- Because Job 1 exists, `next_arrival=10` (not max)
- Condition failed, Job 0 never detected
- Loop jumped directly to t=10

## Fix Applied

Added a pre-loop check in [sim.cpp:308-333](src/sim/sim.cpp#L308-L333):

```cpp
// Before entering the main loop: check if any jobs arrive at current_time (initially 0)
if (!m_wait_queue.empty()) {
    bool has_arrivals_now = false;
    for (job_no_t job_idx : m_wait_queue) {
        const auto& job = m_trace.data()[job_idx];
        const auto& ts = job.get_submit_time();
        sim_time_t submit = static_cast<sim_time_t>(ts.first) + ts.second;
        if (submit == m_current_time) {
            has_arrivals_now = true;
            break;
        }
    }

    if (has_arrivals_now) {
        // Call scheduler to evaluate newly arriving jobs
        // (scheduler loop code...)
    }
}
```

This handles jobs arriving at the initial current_time (t=0) before entering the main event loop.

## Additional Improvements

Applied efficiency optimization suggested by user:
```cpp
// BEFORE (inefficient - creates temporary pair twice):
sim_time_t submit = static_cast<sim_time_t>(job.get_submit_time().first) +
                    job.get_submit_time().second;

// AFTER (efficient - const reference):
const auto& ts = job.get_submit_time();
sim_time_t submit = static_cast<sim_time_t>(ts.first) + ts.second;
```

## Verification Results

✅ **Oracle verification PASSED**

```
Job | Oracle Start | DR_EVT Start | Oracle End | DR_EVT End
----|--------------|--------------|------------|------------
  0 |          0.0 |          0.0 |      100.0 |      100.0
  1 |         10.0 |         10.0 |       30.0 |       30.0
  2 |        100.0 |        100.0 |      150.0 |      150.0
  3 |         30.0 |         30.0 |       45.0 |       45.0
  4 |         45.0 |         45.0 |       75.0 |       75.0
```

✅ **Performance test passed**
- 2000 jobs: 0.89ms simulation time
- No over-subscription
- All jobs completed correctly

## Summary

Both bugs are now fixed:
1. ✅ Over-subscription bug (fixed by refactoring)
2. ✅ Timing offset bug (fixed by pre-loop check)

The refactored scheduler code is now correct and ready to commit.
