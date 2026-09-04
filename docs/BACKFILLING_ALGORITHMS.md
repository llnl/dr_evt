# Backfilling Algorithms - Reference Implementation

This document describes the backfilling algorithms implemented in DR_EVT: EASY and CONSERVATIVE backfilling.

## Table of Contents

1. [EASY Backfilling](#easy-backfilling)
2. [Conservative Backfilling](#conservative-backfilling)
3. [Comparison](#comparison)

---

# EASY Backfilling

## Overview

FCFS (First-Come-First-Served) with EASY (Extensible Argonne Scheduling sYstem) backfilling.

Jobs are scheduled in arrival order (FCFS), but smaller jobs can "backfill" - start early while larger jobs wait - as long as they don't delay the **first waiting job**.

## Visual Overview

```{mermaid}
graph TD
    A[Job Arrives] --> B{Wait Queue Empty?}
    B -->|Yes| C[Start Immediately]
    B -->|No| D[Add to Wait Queue]
    D --> E[Scheduling Event]
    E --> F{FCFS Head<br/>Can Start?}
    F -->|Yes| G[Start FCFS Head]
    F -->|No| H[Calculate Reservation<br/>for FCFS Head]
    H --> I[Try Backfilling]
    I --> J{Found Backfill<br/>Candidate?}
    J -->|Yes| K[Start Backfill Job]
    J -->|No| L[Wait for Resources]
    G --> F
    K --> J
    L --> M[Next Event]
    M --> E
    C --> N[Job Runs]
    N --> O[Job Completes]
    O --> E
```

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

### Reservation Timeline Example

```{mermaid}
gantt
    title Reservation Calculation (FCFS Head needs 50 nodes, 30 available)
    dateFormat X
    axisFormat %s
    
    section Running Jobs
    Job A 20 nodes :done, j1, 0, 100
    Job B 25 nodes :done, j2, 0, 150
    Job C 15 nodes :done, j3, 0, 80
    
    section Wait Queue
    FCFS Head 50 nodes :crit, fcfs, 150, 200
    
    section Resource Timeline
    Free 30 nodes :active, r1, 0, 80
    Job C ends 45 nodes :active, r2, 80, 100
    Job A ends 65 nodes :milestone, r3, 100, 100
```

**Reservation Decision:** Job A completes at t=100, freeing 20 nodes (30+20=50). FCFS head can start at t=100.

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

### Backfilling Example

```{mermaid}
gantt
    title EASY Backfilling Example (100 total nodes)
    dateFormat X
    axisFormat %s
    
    section Running
    Job A 60 nodes :done, ja, 0, 200
    Backfill Job D 20 nodes :active, jd, 50, 150
    Backfill Job E 15 nodes :active, je, 50, 120
    
    section Wait Queue
    FCFS Head Job B 50 nodes :crit, jb, 200, 300
    Job C 30 nodes :jc, 300, 400
    Job D BACKFILLED :done, jd2, 0, 0
    Job E BACKFILLED :done, je2, 0, 0
    
    section Timeline
    t0 40 nodes free :milestone, t0, 0, 0
    t50 Job B blocked :crit, t1, 50, 50
    Reservation at t200 :milestone, res, 200, 200
    Backfill window :active, bw, 50, 200
```

**Scenario:**
- t=0: Job A (60 nodes) running, 40 nodes free
- t=50: Job B arrives (needs 50 nodes) → BLOCKED (only 40 free)
  - Reservation: Job A ends at t=200 → 100 nodes available
  - Backfill window: [50, 200)
- Jobs D and E can backfill:
  - Job D: 20 nodes, time_limit=100 → ends at 150 < 200 ✓
  - Job E: 15 nodes, time_limit=70 → ends at 120 < 200 ✓
  - Job C: 30 nodes → Won't fit (40-20-15=5 < 30) ✗
- t=200: Job A ends, Job B starts immediately

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

---

# Conservative Backfilling

## Overview

FCFS with **CONSERVATIVE backfilling** provides stronger fairness guarantees than EASY.

Jobs are scheduled in arrival order (FCFS), but backfilling can only occur if it **does not delay ANY waiting job** (not just the first).

### Key Difference from EASY

| Aspect | EASY | CONSERVATIVE |
|--------|------|--------------|
| **Reservations** | Only first waiting job | ALL waiting jobs |
| **Backfill Constraint** | Complete before first job's reservation | Complete before ANY job's reservation |
| **Fairness** | Lower (deep queue jobs can be delayed) | Higher (no job is delayed by backfilling) |
| **Utilization** | Higher (~95% typical) | Lower (~87% typical) |
| **Complexity** | O(n) per scheduling event | O(n²) per scheduling event |

---

## Visual Overview

```{mermaid}
graph TD
    A[Job Arrives] --> B{Wait Queue Empty?}
    B -->|Yes| C[Start Immediately]
    B -->|No| D[Add to Wait Queue]
    D --> E[Scheduling Event]
    E --> F{FCFS Head<br/>Can Start?}
    F -->|Yes| G[Start FCFS Head]
    F -->|No| H[Calculate Reservations<br/>for ALL Waiting Jobs]
    H --> I[Try Backfilling]
    I --> J{Found Backfill<br/>Candidate?}
    J -->|Yes| K{Will Complete Before<br/>ANY Reservation?}
    K -->|Yes| L[Start Backfill Job]
    K -->|No| M[Skip Job]
    J -->|No| N[Wait for Resources]
    G --> F
    L --> J
    M --> J
    N --> O[Next Event]
    O --> E
    C --> P[Job Runs]
    P --> Q[Job Completes]
    Q --> E
```

---

## Algorithm

### Step 1: Process FCFS Head (Same as EASY)

```
IF wait_queue is empty:
  RETURN (nothing to schedule)

fcfs_head = wait_queue[0]

IF free_nodes >= fcfs_head.nodes:
  START(fcfs_head)
  remove fcfs_head from wait_queue
  GOTO Step 1 (check next head)
ELSE:
  GOTO Step 2
```

---

## Step 2: Calculate Reservations (Shadow Times) for ALL Waiting Jobs

Unlike EASY (which only calculates reservation for the first job), CONSERVATIVE calculates **independent reservations** (shadow times) for **every waiting job**.

```
FOR EACH job IN wait_queue:
  
  needed = job.nodes
  available = free_nodes
  
  // Find when enough nodes will be available
  // IMPORTANT: Based ONLY on currently running jobs
  events = []
  FOR EACH running_job:
    pessimistic_end = running_job.start + running_job.time_limit
    events.append((pessimistic_end, running_job.nodes))
  
  SORT events by time
  
  cumulative_freed = 0
  FOR EACH (end_time, nodes) IN events:
    cumulative_freed += nodes
    IF available + cumulative_freed >= needed:
      job.reservation_time = end_time  // Shadow time
      BREAK
```

**Critical Properties of Shadow Times (Reservations)**:

1. **Independent Calculation**: Each job's reservation is calculated independently, assuming only currently running jobs exist (not other waiting jobs)

2. **May Be Unrealistic**: Multiple jobs may have overlapping or identical reservations because they don't account for jobs ahead of them in the queue

3. **Example of Unrealistic Reservations**:
   ```
   Currently running: Job A (60 nodes, ends t=50)
   Free: 20 nodes
   
   Waiting queue:
   - Job 1: needs 50 nodes → reservation t=50 (when 20+60=80 available)
   - Job 2: needs 30 nodes → reservation t=50 (when 20+60=80 available)  
   - Job 3: needs 40 nodes → reservation t=50 (when 20+60=80 available)
   
   All three have reservation at t=50, but realistically only one can start then!
   ```

4. **Stricter Than Necessary**: Because reservations ignore queue order, a job deep in the queue may have an early reservation, blocking backfill opportunities that wouldn't actually delay it

5. **Formal Guarantee**: Despite being unrealistic, each reservation is a **formal guarantee** - no waiting job will be delayed past its predicted start time

**Why This Works**: A backfill job can only start if it completes before **ALL** shadow times. This ensures no waiting job is delayed, even if the shadow times themselves are optimistic.

---

## Step 3: Conservative Backfilling

```
FOR EACH job IN wait_queue[1:]:  // Skip FCFS head (index 0)
  
  // Check 1: Does it fit in current free space?
  IF job.nodes > free_nodes:
    CONTINUE  // Too big, skip
  
  // Check 2: Calculate conservative window
  earliest_reservation = infinity
  FOR EACH waiting_job IN wait_queue[0:current_position]:
    earliest_reservation = min(earliest_reservation, waiting_job.reservation_time)
  
  // Check 3: Will it complete before ANY waiting job's reservation?
  estimated_completion = current_time + job.time_limit
  IF estimated_completion >= earliest_reservation:
    CONTINUE  // Would delay at least one job, skip
  
  // All checks passed - backfill!
  START(job)
  remove job from wait_queue
  update free_nodes
  
  // IMPORTANT: Update reservations for remaining jobs
  // (backfilled job is now "effectively running")
```

**Critical Detail**: When a job backfills, it becomes "effectively running" for calculating reservations of subsequent backfill candidates in the **same scheduling cycle**.

---

## Conservative Backfilling Example

### Scenario
- 100 total nodes
- Job 0: 60 nodes, ends at t=50
- Job 1: 20 nodes, ends at t=100
- Free: 20 nodes

**Wait Queue at t=0:**
1. Job 2: needs 50 nodes, duration 200 → reservation at t=50
2. Job 3: needs 30 nodes, duration 50 → reservation at t=50
3. Job 4: needs 15 nodes, duration 100 → reservation at t=0 (could start now!)
4. Job 5: needs 10 nodes, duration 40 → backfill candidate

### EASY Decision

```
FCFS head = Job 2 (first in queue)
Job 2 reservation = t=50

Backfill candidate = Job 5
  - Fits: 10 ≤ 20 ✓
  - Complete before Job 2 reservation: 0+40=40 < 50 ✓
  
EASY: ✅ BACKFILL Job 5
```

Result: Job 5 starts at t=0, ends at t=40

### CONSERVATIVE Decision

```
Calculate ALL reservations:
  Job 2: reservation = t=50 (needs 50, will have 80 at t=50)
  Job 3: reservation = t=50 (needs 30, will have 80 at t=50)
  Job 4: reservation = t=0 (needs 15, have 20 now!)
  Job 5: checking...

Backfill candidate = Job 5
  - Fits: 10 ≤ 20 ✓
  - Conservative window = min(t=50, t=50, t=0) = t=0
  - Complete before window: 0+40=40 < 0 ✗
  
CONSERVATIVE: ❌ REJECT Job 5
```

Result: Job 5 must wait (protects Job 4's reservation)

**Key Difference**: CONSERVATIVE protects Job 4 (which could start immediately), while EASY only protects Job 2 (first in queue).

---

## Implementation Considerations

### Effective Running Jobs

When multiple jobs backfill in the same scheduling cycle, subsequent backfill checks must treat already-backfilled jobs as "effectively running":

```
effective_running = actual_running_jobs + backfilled_jobs_this_cycle

FOR EACH backfill_candidate:
  Calculate reservations using effective_running (not just actual_running)
  Check conservative window
  IF backfill succeeds:
    Add to backfilled_jobs_this_cycle
```

This prevents multiple backfilled jobs from incorrectly overlapping with waiting jobs' reservations.

### Complexity

- **EASY**: O(n) per scheduling event
  - Calculate one reservation (FCFS head)
  - Check each backfill candidate once
  
- **CONSERVATIVE**: O(n²) per scheduling event
  - Calculate n reservations (all waiting jobs)
  - For each backfill candidate, calculate conservative window (scan all jobs ahead)

---

## Performance Comparison

Based on DR_EVT test results (2000 jobs, 400 nodes):

| Metric | EASY | CONSERVATIVE | Difference |
|--------|------|--------------|------------|
| **Runtime** | 0.053s | 143.9s | **2715x slower** |
| **Makespan** | 909,621s | 993,969s | **+9.3% longer** |
| **Utilization** | 95.20% | 87.13% | **-8.08 points** |
| **Fairness** | Lower | Higher | Guarantees no delays |

**Trade-offs**:

**CONSERVATIVE Advantages**:
- ✅ **Fairness**: No waiting job is ever delayed past its predicted start time
- ✅ **Predictability**: Every queued job gets a formal reservation (shadow time)
- ✅ **User Trust**: Queue position guarantees are honored

**CONSERVATIVE Disadvantages**:
- ❌ **Lower Utilization**: Rigid reservations create strict limits that block smaller jobs from running (87% vs 95%)
- ❌ **Higher Overhead**: Must calculate and track reservations for ALL waiting jobs (O(n²) vs O(n))
- ❌ **Overly Conservative**: Independent shadow time calculations ignore queue order, making reservations unrealistic but stricter than necessary
- ❌ **Longer Makespan**: Jobs complete later overall (+9.3%)

---

## When to Use Conservative Backfilling

**Use CONSERVATIVE when:**
- Fairness is critical (SLAs, user expectations)
- Job priority/order must be strictly respected
- Preventing job delays is more important than maximizing utilization

**Use EASY when:**
- Maximizing system utilization is priority
- Faster scheduling performance needed
- Small delays to waiting jobs are acceptable

---

## Reference

Conservative backfilling concepts from:
- Feitelson, D. G., & Weil, A. M. (1998). "Utilization and predictability in scheduling the IBM SP2 with backfilling"
- Srinivasan, S., Kettimuthu, R., Subramani, V., & Sadayappan, P. (2002). "Selective reservation strategies for backfill job scheduling"

Key insight: **All jobs get reservations** (shadow times), ensuring no job is delayed by backfilling.

---

# Comparison

## Summary Table

| Feature | EASY | CONSERVATIVE |
|---------|------|--------------|
| **Reservations** | 1 (first job only) | N (all waiting jobs) |
| **Backfill Check** | Before first job's reservation | Before ANY job's reservation |
| **Fairness Guarantee** | First job not delayed | NO job delayed |
| **Time Complexity** | O(n) | O(n²) |
| **Space Complexity** | O(n) | O(n) |
| **Typical Utilization** | 93-96% | 85-90% |
| **Use Case** | High throughput systems | Fair-share clusters |
| **Implementation** | Simpler | More complex |

## Algorithm Choice Decision Tree

```
Is fairness guarantee critical?
├─ YES → Use CONSERVATIVE
│   └─ Accept: Lower utilization, slower scheduling, longer makespan
└─ NO → Use EASY
    └─ Accept: Deep queue jobs may be delayed by backfilling
```

---

## DR_EVT Implementation

Both algorithms are implemented in DR_EVT:

**EASY**:
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator trace.csv --priority_policy fcfs --backfill_policy easy
```

**CONSERVATIVE**:
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator trace.csv --priority_policy fcfs_conservative --backfill_policy conservative
```

**Python References**:
- EASY: [scripts/python_reference_scheduler.py](https://github.com/llnl/dr_evt/blob/main/scripts/python_reference_scheduler.py)
- CONSERVATIVE: [scripts/python_conservative_scheduler.py](https://github.com/llnl/dr_evt/blob/main/scripts/python_conservative_scheduler.py)

**Tests**: See [tests/test_easy_vs_conservative_correctness.sh](https://github.com/llnl/dr_evt/blob/main/tests/test_easy_vs_conservative_correctness.sh) for behavioral demonstration.
