# Scheduler Optimization - Event Type Matters

## Key Insight

The scheduler should be called differently depending on what triggered it:

### Job Arrival Event

**Situation:**
- New job(s) just became eligible (submit_time reached)
- Resources have NOT changed
- All other jobs in wait_queue were already evaluated at previous scheduling point
- They failed to start because of insufficient resources or backfill constraints
- Those constraints STILL HOLD (resources unchanged)

**Therefore:**
- Only evaluate the **newly arriving jobs**
- Do NOT re-scan entire wait_queue
- Other jobs will be evaluated when resources free up (END event)

**Implementation:**
```cpp
// Only check jobs arriving at current_time
std::vector<job_no_t> new_arrivals;
for (job_no_t job_idx : m_wait_queue) {
    if (get_submit_time(job_idx) == m_current_time) {
        new_arrivals.push_back(job_idx);
    }
}
// Evaluate only new arrivals
scheduler.evaluate_arrivals(new_arrivals, ...);
```

### END Event (Resource Return)

**Situation:**
- Job completed, resources freed
- Resources HAVE changed (more available)
- Jobs that couldn't fit before might fit now
- FCFS head might now meet resource requirement
- Backfill candidates might now fit

**Therefore:**
- Must scan **entire wait_queue**
- Resource availability changed, so previous failures may now succeed

**Implementation:**
```cpp
// Resources changed - evaluate entire wait_queue
scheduler.schedule(m_wait_queue, free_nodes, ...);
```

## Performance Impact

**Before:** O(n) scan of wait_queue on every arrival
**After:** O(k) where k = number of new arrivals (usually 1)

For traces with sequential arrivals, this is significant savings.

## Correctness Impact

This is not just an optimization - it's conceptually correct:
- Arrival changes eligibility, not feasibility
- END changes feasibility (resources available)
- Only re-evaluate when feasibility changes
