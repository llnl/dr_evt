# Backfill Timing Decision

## Question
When checking if a job can backfill before the FCFS head's reservation, should we allow:
- `completion <= reservation` (job completes AT reservation time)
- `completion < reservation` (job completes BEFORE reservation time)

## Decision
**Use strict inequality: `completion < reservation`**

A backfill job must complete **strictly before** the FCFS head's reservation time.

## Rationale

While theoretically a job ending at t=10 and another starting at t=10 seems acceptable (since we process END events before START events), in practice:

1. **Resource overhead**: Resources are not instantaneously available after a job ends. There's overhead for cleanup, accounting, etc.

2. **Scheduling guarantee**: EASY backfilling guarantees that the FCFS head will start **at** its reservation time. If a backfill job completes exactly at the reservation time, it may delay the FCFS head due to processing overhead.

3. **Conservative approach**: Being conservative ensures the FCFS head is never delayed, which is a core guarantee of EASY backfilling.

## Implementation

**Both implementations must use the same rule:**

### DR_EVT (C++):
```cpp
// scheduler.cpp
if (current_time + runtime_est < m_fcfs_reservation_time) {
    // Can backfill
}
```

### Reference Implementation (Python):
```python
# minimal_easy_oracle.py
estimated_completion = self.current_time + job.duration
if estimated_completion >= reservation_time:
    return False  # Cannot backfill
```

## Example

At t=62:
- FCFS head = Job 9, reservation at t=122
- Job 12: duration 60, would complete at t=122
- **Decision:** Job 12 cannot backfill (122 is not < 122)
- Jobs with duration < 60 can backfill

This ensures Job 9 can start promptly at t=122 without waiting for Job 12's cleanup.
