# EASY Backfilling Algorithm - Reference Implementation

## Overview

FCFS (First-Come-First-Served) with EASY (Extensible Argonne Scheduling sYstem) backfilling.

Jobs are scheduled in arrival order (FCFS), but smaller jobs can "backfill" - start early while larger jobs wait - as long as they don't delay the first waiting job.

---

## Data Structures

### Wait Queue
- **Ordered list** of jobs waiting to start
- **FCFS order**: sorted by arrival time (submit_time)
- **FCFS head**: First job in wait queue (index 0)

### Running Jobs
- Jobs currently executing
- Track: (job_id, start_time, end_time, nodes)
- **end_time based on time_limit** for reservation calculations (pessimistic)
- **actual end_time** for actual completion (may be earlier)

### System State
- `TOTAL_NODES`: Total system capacity (e.g., 100)
- `free_nodes = TOTAL_NODES - sum(running_job.nodes)`

---

## Algorithm

### Scheduling Events

Scheduler runs at these events:
1. Job arrival (submit_time)
2. Job completion (actual end_time)

### Scheduling Logic (per event)

```
SCHEDULE():
  1. Process FCFS Head
  2. If blocked, calculate Reservation
  3. Try Backfilling
  4. Repeat until no more jobs can start
```

---

## Step 1: Process FCFS Head

```
IF wait_queue is empty:
  RETURN (nothing to schedule)

fcfs_head = wait_queue[0]

IF free_nodes >= fcfs_head.nodes:
  // FCFS head can fit - start it immediately
  START(fcfs_head)
  remove fcfs_head from wait_queue
  GOTO Step 1 (check next head)
ELSE:
  // FCFS head blocked - go to backfilling
  GOTO Step 2
```

**Key Rule**: FCFS head starts **immediately** if resources available. No additional constraints.

---

## Step 2: Calculate Reservation

When FCFS head is blocked (needs more nodes than available):

```
needed = fcfs_head.nodes
available = free_nodes

// Find when enough nodes will be available
events = []
FOR EACH running_job:
  pessimistic_end = running_job.start + running_job.time_limit
  events.append((pessimistic_end, running_job.nodes))

SORT events by time

cumulative_freed = 0
FOR EACH (end_time, nodes) IN events:
  cumulative_freed += nodes
  IF available + cumulative_freed >= needed:
    reservation_time = end_time
    BREAK

// FCFS head is GUARANTEED to start no later than reservation_time
```

**Key Points**:
- Use **time_limit** (not actual_runtime) for pessimistic planning
- May need **multiple jobs** to finish to free enough resources
- Reservation = when **enough** resources available, not just first job end

---

## Step 3: Try Backfilling

```
backfill_window = reservation_time

FOR EACH job IN wait_queue[1:]:  // Skip FCFS head (index 0)
  
  // Check 1: Does it fit in current free space?
  IF job.nodes > free_nodes:
    CONTINUE  // Too big, skip
  
  // Check 2: Will it complete before reservation?
  estimated_completion = current_time + job.time_limit
  IF estimated_completion >= reservation_time:
    CONTINUE  // Would delay FCFS head, skip (resources aren't freed instantly)
  
  // Both checks passed - backfill!
  START(job)
  remove job from wait_queue
  update free_nodes

// After backfilling, GOTO Step 1
// (free_nodes changed, FCFS head might fit now)
```

**Key Points**:
- Check uses **time_limit** for estimated completion (pessimistic)
- Must complete **strictly before** reservation (`<` not `<=`)
- Multiple jobs can backfill if all fit
- Backfilling processes jobs in **FCFS order** among backfillers

---

## Early Completion Handling

Jobs may finish before their time_limit (actual_runtime < time_limit).

### Planning (Pessimistic)
- Reservation calculated using **time_limit**
- Assume job runs full time_limit

### Execution (Optimistic)
- Job actually ends at: `start_time + actual_runtime`
- When job ends early, immediately reschedule:
  - More free nodes available
  - FCFS head might fit now
  - New jobs can backfill

### Example

```
Job 0: time_limit=200, actual_runtime=50
  - Reservation calculated assuming ends at t=200
  - Actually ends at t=50
  - At t=50: reschedule immediately (opportunistic)
```

---

## Edge Cases & Clarifications

### 1. FCFS Head Can Fit
**Q**: When FCFS head can fit, does it start immediately?  
**A**: YES. **It starts immediately.** No additional time window constraints.

The FCFS head is THE highest priority job. If resources are available, it starts. Period.

**Terminology - "Remaining Resources"**:
**Remaining resources** = currently available resources = `free_nodes`
= `TOTAL_NODES - sum(running job nodes)`

In the 3-job backfill pattern:
> "Job 0 running using resource. Job 1 has arrived and is waiting in the queue because the system does not have enough resource to accommodate its demand. Job 2 is waiting and it can actually run using the **remaining resources**."

This means:
- Job 0: running, using 70 nodes
- Free/remaining: 30 nodes (100 - 70)
- Job 1: needs 50 nodes > 30 remaining → CANNOT fit, blocked
- Job 2: needs 20 nodes ≤ 30 remaining → CAN fit in the currently available space

"Remaining" is simply the free space right now. No special constraint implied.

### 2. Multiple Running Jobs
**Q**: FCFS head needs resources from 2+ running jobs. When is reservation?  
**A**: **Correct.** When ENOUGH resources freed (may require multiple jobs to finish).

**Otherwise, there are no sufficient resources.**

Example:
- Running: Job A (30 nodes, ends t=100), Job B (40 nodes, ends t=200)
- Free: 30 nodes
- FCFS head needs: 70 nodes
- Reservation: t=100 (when Job A frees 30 → total 60, still not enough)
- Wait... no, t=200 (when Job B also frees 40 → total 100, enough!)
- Actually: At t=100, Job A frees 30 → 60 total free, still < 70
- At t=200, Job B frees 40 → 100 total free, >= 70 ✓
- Reservation = t=200

### 3. Backfill Time Window
**Q**: Strict `<` or `<=`?  
**A**: **Strict `<`** (complete BEFORE reservation, not AT).

**Reasoning**: 
- We discussed this in DR_EVT logic
- **In practice, resources do not get returned instantly after job finishes**
- There is overhead (cleanup, deallocation, etc.)
- A job completing exactly at reservation time would delay FCFS head's start
- Therefore: backfiller must complete **strictly before** reservation

### 4. What if No Jobs Running?
When wait queue has jobs but nothing running:
- FCFS head is first job
- If can fit → starts immediately
- If cannot fit → ERROR (job needs more than TOTAL_NODES)
- No backfilling (no reservation without blocked FCFS head)

---

## Complete Example Trace

**System**: 100 nodes

**Jobs**:
- Job 0: submit=0, nodes=70, time_limit=200, actual=200
- Job 1: submit=10, nodes=50, time_limit=300, actual=300
- Job 2: submit=20, nodes=20, time_limit=50, actual=50

**Trace**:

```
t=0: Job 0 arrives
  wait_queue = [0]
  free = 100
  FCFS head = Job 0, needs 70 <= 100 ✓
  START Job 0 → end_time = 0 + 200 = 200
  wait_queue = []

t=10: Job 1 arrives
  wait_queue = [1]
  free = 100 - 70 = 30
  FCFS head = Job 1, needs 50 > 30 ✗
  Calculate reservation:
    Job 0 ends at 200 → free 70 → total 30+70=100 >= 50 ✓
    reservation_time = 200

t=20: Job 2 arrives
  wait_queue = [1, 2]
  free = 30
  FCFS head = Job 1, needs 50 > 30 ✗
  reservation_time = 200 (same)
  Try backfill:
    Job 2: nodes=20 <= 30 ✓
    Job 2: 20 + 50 = 70 < 200 ✓
    BACKFILL Job 2 → end_time = 70
  wait_queue = [1]

t=70: Job 2 completes
  free = 30 + 20 = 50
  wait_queue = [1]
  FCFS head = Job 1, needs 50 <= 50 ✓
  // Would start here IF FCFS head could start!
  // But Job 0 still running, so free = 30, not 50
  // My mistake above - recalculate...
  
  Actually at t=70:
  Running: Job 0 (still running, ends at 200)
  free = 100 - 70 = 30 (Job 2 freed 20, but Job 0 still uses 70)
  FCFS head = Job 1, needs 50 > 30 ✗
  Still blocked, wait...

t=200: Job 0 completes
  free = 100
  wait_queue = [1]
  FCFS head = Job 1, needs 50 <= 100 ✓
  START Job 1 → end_time = 200 + 300 = 500
  wait_queue = []

t=500: Job 1 completes
  All done
```

**Expected Output**:
```
Job 0: start=0, end=200
Job 1: start=200, end=500
Job 2: start=20, end=70
```

---

## Common Mistakes I Made

### ❌ Mistake 1: "FCFS head must check time window"
**Wrong**: Job that can fit must check if it outlasts running jobs  
**Right**: FCFS head starts immediately if resources available

### ❌ Mistake 2: "Use earliest running job end"
**Wrong**: Backfill window = min(reservation, earliest_running_end)  
**Right**: Backfill window = reservation (which already accounts for ALL running jobs)

### ❌ Mistake 3: "Only 2-job tests are invalid"
**Wrong**: Need 3 jobs for backfill test  
**Right**: 3-job minimum: 1 running, 1 FCFS head (blocked), 1 backfiller

### ❌ Mistake 4: "Use actual_runtime for reservation"
**Wrong**: Calculate reservation using actual_runtime  
**Right**: Use time_limit (pessimistic), actual for execution only

---

## Questions to Verify Understanding

### Q1: Job can fit but would outlast running job. Start it?
**A**: Depends. Is it FCFS head or backfiller?
- FCFS head → START immediately (highest priority)
- Backfiller → DON'T START (would delay FCFS head's reservation)

### Q2: Reservation uses time_limit or actual_runtime?
**A**: time_limit (pessimistic planning, conservative guarantee)

### Q3: Backfill check: `completion < reservation` or `<=`?
**A**: Strict `<` (must complete BEFORE, not AT)

### Q4: Multiple running jobs - which end time matters?
**A**: Whichever combination frees ENOUGH resources (may need multiple)

---

## Reference

This algorithm is EASY backfilling from:
- Lifka, D. A. (1995). "The ANL/IBM SP scheduling system"
- Mu'alem, A. W., & Feitelson, D. G. (2001). "Utilization, predictability, workloads, and user runtime estimates in scheduling the IBM SP2 with backfilling"

Key insight: **One reservation** (FCFS head) instead of shadow times for all jobs (conservative backfilling).
