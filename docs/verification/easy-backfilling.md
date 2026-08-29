# EASY Backfilling - Complete Properties

## Overview

EASY (Extensible Argonne Scheduling sYstem) backfilling is a scheduling algorithm that improves cluster utilization by allowing smaller jobs to "backfill" - run earlier than their FCFS position - while guaranteeing the first job in the queue will not be delayed.

## Core Properties

### 1. FCFS Head Priority

**Definition:** The first job in the waiting queue (ordered by submission time) gets highest priority.

**Guarantee:** The FCFS head receives a **reservation** - a guaranteed start time - and no other job can delay this reservation.

**Implementation:**
```
if (first_job fits now) {
    start first_job immediately
    reservation_time = current_time
} else {
    reservation_time = earliest_time_first_job_can_fit
}
```

**Example:**
```
t=0: Job 0 arrives (80 nodes, 100s)
     → FCFS head, starts immediately at t=0
     → Reservation time = 0

t=10: Job 1 arrives (60 nodes, 50s)
      → Becomes FCFS head (Job 0 is running)
      → Cannot fit now (only 20 nodes free)
      → Reservation time = 100 (when Job 0 completes)
```

**Why it matters:** Guarantees forward progress - the oldest waiting job will eventually run.

---

### 2. Backfilling Constraint

**Definition:** A job can backfill (run out of FCFS order) ONLY if it will complete BEFORE the FCFS head's reservation time.

**Mathematical constraint:**
```
For job J to backfill at time t:
    t + duration(J) ≤ reservation_time
```

**Why this constraint exists:** Prevents backfilled jobs from delaying the FCFS head's guaranteed start time.

**Example - Backfill Allowed:**
```
t=0:  Job 0 arrives (80 nodes, 100s) → starts t=0
t=10: Job 1 arrives (60 nodes, 50s) → FCFS head, reservation at t=100
t=20: Job 2 arrives (15 nodes, 20s)
      → Can backfill? Check: 20 + 20 = 40 ≤ 100 ✓
      → YES, starts at t=20
```

**Example - Backfill Blocked:**
```
t=0:  Job 0 arrives (80 nodes, 50s) → starts t=0
t=10: Job 1 arrives (60 nodes, 30s) → FCFS head, reservation at t=50
t=20: Job 2 arrives (15 nodes, 40s)
      → Can backfill? Check: 20 + 40 = 60 > 50 ✗
      → NO, would delay Job 1's reservation
      → Must wait until t=50
```

**Critical insight:** This is what makes it "EASY" - only the first job gets a reservation, making the check simple.

---

### 3. Resource Constraint

**Definition:** Total allocated resources never exceed system capacity.

**Invariant:**
```
At all times: Σ(nodes_allocated) ≤ total_nodes
```

**Implementation check:**
```cpp
num_nodes_t free_nodes = total_nodes - nodes_in_use;
if (job.nodes <= free_nodes) {
    // Can schedule
} else {
    // Must wait
}
```

**Example:**
```
System: 100 nodes total

t=0: Job 0 running (80 nodes) → 20 free
t=10: Job 1 arrives (60 nodes)
      → 60 > 20 → Cannot fit, must wait
      
t=10: Job 2 arrives (15 nodes)
      → 15 ≤ 20 → Can fit, may backfill (check constraint #2)
```

**Why it matters:** Prevents over-subscription - physical impossibility.

---

### 4. Time Ordering (Causality)

**Definition:** Jobs cannot start before their submission time.

**Constraint:**
```
For all jobs: start_time ≥ submit_time
```

**Why it matters:** Respects causality - jobs don't exist before they're submitted.

**Example:**
```
Job arrives at t=10
Job cannot start at t=5 (even if resources available)
Job earliest start = t=10
```

**Special case:** Jobs submitted at t=0 must be handled correctly (this was our bug!).

---

### 5. No Starvation

**Definition:** Every job eventually completes (assuming finite durations).

**Guarantee mechanism:** FCFS head always gets a reservation, preventing indefinite starvation.

**Proof sketch:**
1. Job J becomes FCFS head eventually (finite queue)
2. J gets reservation R
3. No job can delay R (backfill constraint)
4. J starts at time ≤ R
5. J completes at time ≤ R + duration(J)

**Why EASY prevents starvation:**
- Small jobs can backfill, but cannot delay FCFS head
- Large jobs waiting as FCFS head will eventually run
- No job can be perpetually delayed by backfilling

**Contrast with pure backfilling:**
- Without FCFS head reservation, large jobs could starve
- Small jobs keep backfilling indefinitely
- Large job never gets resources

---

### 6. Reservation Calculation

**Definition:** Calculate when the FCFS head can start, given current and future resource availability.

**Algorithm:**
```
Calculate reservation for job J:
1. Get currently free nodes: F
2. If J.nodes ≤ F:
   reservation = current_time (can start now)
3. Else:
   Find earliest time T when:
     free_nodes(T) ≥ J.nodes
   
   Where free_nodes(T) = F + Σ(nodes freed by jobs ending at or before T)
```

**Implementation:**
```cpp
sim_time_t calculate_reservation(job, free_nodes, running_jobs) {
    if (job.nodes <= free_nodes) {
        return current_time;
    }
    
    // Sort running jobs by end time
    vector<end_event> = running_jobs.end_times
    sort(end_events by time)
    
    // Simulate jobs completing
    available = free_nodes
    for (event : end_events) {
        available += event.nodes
        if (available >= job.nodes) {
            return event.time  // Can start when this job ends
        }
    }
    
    return current_time;  // Should not happen if job fits in system
}
```

**Example:**
```
System: 100 nodes
Currently running:
  - Job A: 60 nodes, ends t=50
  - Job B: 30 nodes, ends t=80

Free nodes: 10
New job: 70 nodes

Calculation:
  t=now: 10 nodes < 70 → cannot start
  t=50:  10 + 60 = 70 nodes ≥ 70 → CAN START!
  Reservation = 50
```

---

### 7. Backfill Window

**Definition:** The time window during which a job can backfill.

**Calculation:**
```
backfill_window = [current_time, reservation_time)
```

For job J to backfill:
- Must fit in available resources NOW
- Must complete within window: current_time + duration(J) ≤ reservation_time

**Visual representation:**
```
Time:     0         50        100       150       200
          |---------|---------|---------|---------|
Job 0:    [=============================]  80 nodes
          ^                             ^
          starts                        ends

Job 1 (FCFS head):                     [===========]  60 nodes
                                       ^ reservation

Backfill window for Job 2: [t=20, t=100)
                           ^^^^^^^^^^^^
                           80 time units

Job 2 (15 nodes, 40s):     [====]  ✓ fits in window
Job 3 (15 nodes, 85s):               ✗ exceeds window
```

---

### 8. Duration Correctness

**Definition:** Jobs run for their specified duration (or less if they fail).

**Two modes in simulation:**

**A. Exact Duration (USE_ACTUAL)**
- Scheduler uses actual runtime for planning
- "Oracle" mode - perfect knowledge
- Use for correctness testing

**B. Realistic Duration (USE_LIMIT)**
- Scheduler uses user-provided time_limit for planning
- Jobs complete based on actual runtime
- Realistic mode - users often overestimate

**Example:**
```
Job submitted: time_limit = 100s, actual_runtime = 60s

Exact mode:
  Scheduler plans for 60s
  Job runs for 60s
  Perfect efficiency

Realistic mode:
  Scheduler plans for 100s (conservative)
  Job runs for 60s (completes early)
  Wasted 40s of reservation
```

**Why it matters:** Realistic mode tests real-world behavior where users overestimate.

---

### 9. Completeness

**Definition:** All submitted jobs eventually complete (assuming no failures).

**Invariant:**
```
jobs_completed = jobs_submitted
```

**Tracked during simulation:**
```cpp
// When job J is submitted
m_jobs_submitted++

// When job J completes
m_jobs_completed++

// Verification at end
assert(m_jobs_completed == m_jobs_submitted)
```

**Why it matters:** Ensures no jobs are lost or forgotten by scheduler.

---

### 10. Work Conservation

**Definition:** The scheduler never leaves resources idle when waiting jobs could use them.

**Guarantee:** If resources are available and jobs are waiting, schedule something.

**Algorithm ensures this:**
```cpp
while (true) {
    jobs_to_run = scheduler.schedule(...)
    if (jobs_to_run.empty()) {
        break;  // Nothing fits
    }
    start(jobs_to_run)
    // Loop continues - try to schedule more
}
```

**Example:**
```
20 nodes free
Waiting jobs:
  - Job A: 30 nodes (cannot fit)
  - Job B: 15 nodes (can fit, but would delay FCFS head)
  - Job C: 10 nodes (can fit, can backfill)

Result: Schedule Job C (work conserving - don't leave 20 nodes idle)
```

---

## Property Dependencies

Some properties depend on others:

```
Resource Constraint (physical)
    ↓
Time Ordering (causality)
    ↓
FCFS Head Priority (fairness)
    ↓
Backfilling Constraint (prevents delays)
    ↓
No Starvation (liveness)
```

---

## Comparison: EASY vs Conservative Backfilling

| Property | EASY | Conservative |
|----------|------|--------------|
| **Reservations** | Only FCFS head | ALL waiting jobs |
| **Backfill constraint** | Don't delay FCFS head | Don't delay ANY reservation |
| **Complexity** | O(n) - check one reservation | O(n²) - check all reservations |
| **Utilization** | Higher - more aggressive | Lower - more conservative |
| **Implementation** | Simpler | More complex |

**EASY advantage:** Better utilization, simpler to implement
**Conservative advantage:** Stronger guarantees, more predictable wait times

---

## Verification Requirements

To verify EASY backfilling correctness, test all properties:

1. ✅ **FCFS Head Priority** - First job gets reservation
   - Test: sequential_wait, easy_5jobs

2. ✅ **Backfilling** - Small jobs run early
   - Test: backfill_3jobs, easy_5jobs

3. ✅ **Backfill Constraint** - Don't delay FCFS head
   - Test: backfill_blocked

4. ✅ **Resource Constraint** - No over-subscription
   - Test: All tests (implicit)

5. ✅ **Time Ordering** - Jobs start ≥ submit_time
   - Test: All tests (implicit)

6. ✅ **Duration** - Jobs run correctly
   - Test: All tests (implicit)

7. ✅ **Completeness** - All jobs finish
   - Test: All tests (implicit)

8. ✅ **No Starvation** - Jobs don't wait forever
   - Test: Long-running tests (scale tests)

9. ✅ **Work Conservation** - Don't waste resources
   - Test: idle_gap

10. ✅ **Reservation Calculation** - Correct timing
    - Test: All tests (verified by oracle comparison)

---

## Common Misconceptions

### ❌ "Backfilling means shortest job first"
**Wrong.** Backfilling respects FCFS for the head. Small jobs backfill only if they don't delay the FCFS head.

### ❌ "Any job that fits can backfill"
**Wrong.** Must also complete before FCFS head's reservation.

### ❌ "EASY gives smaller jobs priority"
**Wrong.** EASY is still FCFS-based. Backfilling is an optimization, not a priority change.

### ❌ "Jobs can start before their submit_time"
**Wrong.** Violates causality. Jobs must wait until at least their submission time.

### ❌ "The scheduler can over-allocate temporarily"
**Wrong.** Physical constraint - never exceed total nodes.

---

## Mathematical Formulation

For a job J to be scheduled at time t:

```
CAN_SCHEDULE(J, t) ⟺
    J.submit_time ≤ t                           (Time ordering)
    ∧ J.nodes ≤ free_nodes(t)                   (Resource constraint)
    ∧ (J = FCFS_head                            (FCFS priority OR
       ∨ (t + J.duration ≤ R))                  backfill constraint)

Where:
    R = reservation_time of FCFS_head
    free_nodes(t) = total_nodes - Σ(running at t)
```

---

## Implementation Checklist

When implementing EASY backfilling, ensure:

- [ ] FCFS head identified correctly (sorted by submit_time)
- [ ] Reservation calculated for FCFS head
- [ ] Backfill constraint checked (end_time ≤ reservation)
- [ ] Resource constraint enforced (never over-subscribe)
- [ ] Time ordering maintained (start ≥ submit)
- [ ] Jobs at t=0 handled correctly
- [ ] Work conservation loop (schedule until nothing fits)
- [ ] All jobs eventually complete
- [ ] Reservation updated when FCFS head changes

---

## References

- Original EASY paper: Lifka, D.A. (1995). "The ANL/IBM SP Scheduling System"
- Feitelson, D.G., et al. "Backfilling with Lookahead to Optimize the Performance of Parallel Job Scheduling"
- DR_EVT Implementation: [src/sim/scheduler.cpp](../src/sim/scheduler.cpp)
