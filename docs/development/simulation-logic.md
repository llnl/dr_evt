# DR_EVT Simulation Logic

## Overview

DR_EVT is a discrete-event simulator for job scheduling with EASY backfilling. The simulator processes jobs in time order, making scheduling decisions at key event times.

## Architecture

```
┌─────────────┐
│   Trace     │  Load jobs from CSV
│   Loader    │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ Simulation  │  Main event loop
│   Engine    │  (sim.cpp)
└──────┬──────┘
       │
       ├──────────────┐
       │              │
       ▼              ▼
┌─────────────┐  ┌─────────────┐
│  Scheduler  │  │   Replay    │
│   (EASY)    │  │   Engine    │
└─────────────┘  └─────────────┘
```

### Components

1. **Trace Loader** ([trace.cpp](../src/trace/trace.cpp))
   - Loads job data from CSV files
   - Stores job metadata (submit_time, nodes, duration, queue)

2. **Simulation Engine** ([sim.cpp](../src/sim/sim.cpp))
   - Orchestrates the event loop
   - Manages wait_queue and running_jobs
   - Calls scheduler at appropriate times

3. **Scheduler** ([scheduler.cpp](../src/sim/scheduler.cpp))
   - Implements EASY backfilling algorithm
   - Evaluates which jobs can run given current resources
   - Makes decisions based on FCFS head and backfilling rules

4. **Replay Engine** ([trace.cpp](../src/trace/trace.cpp))
   - Maintains ordered event queue (START and END events)
   - Tracks resource usage over time
   - Processes events in chronological order

## Simulation Modes

### Batch Mode (Current Implementation)

All jobs are loaded into the wait_queue before simulation starts:

```cpp
// Load all jobs into wait_queue
for (job_idx = 0; job_idx < num_jobs; job_idx++) {
    submit_job(job_idx, job.get_submit_time());
}

// Run simulation to completion
advance_to(max_time);
```

**Characteristics:**
- All jobs known upfront
- Jobs "don't exist" until current_time reaches their submit_time
- Scheduler only considers jobs where `submit_time <= current_time`

### Streaming Mode (Also Supported)

Jobs arrive dynamically during simulation:

```cpp
// Submit jobs incrementally
submit_job(job_idx, submit_time);
advance_to(next_decision_point);
```

## Event Loop Logic

The main simulation loop processes events in chronological order.

### Initial State (t=0)

Before entering the main loop, handle jobs arriving at t=0:

```cpp
// Check if any jobs arrive at current_time (initially 0)
if (!m_wait_queue.empty()) {
    for (job_idx in m_wait_queue) {
        if (job.submit_time == m_current_time) {
            has_arrivals_now = true;
            break;
        }
    }
    
    if (has_arrivals_now) {
        // Call scheduler to evaluate newly arriving jobs
        schedule_and_start_jobs();
    }
}
```

**Why this is needed:**
- Can't use negative initial time (principle: don't use signed types for non-negative quantities)
- Jobs at t=0 must be detected before the main loop
- Main loop only advances time forward, so t=0 is a special case

### Main Event Loop

```cpp
while (current_time < target_time || !wait_queue.empty()) {
    // 1. Find next event time
    next_arrival = find_next_job_arrival();
    next_replay_time = find_next_replay_event();
    
    // 2. Decide which event to process
    if (next_replay_time <= next_arrival) {
        process_replay_event();
    } else if (next_arrival < infinity) {
        process_job_arrival();
    } else if (have_jobs_at_current_time()) {
        schedule_jobs_at_current_time();
    } else {
        break;  // No more events
    }
}
```

### Event Types

#### 1. Job Arrival Event

**When:** `current_time` reaches a job's `submit_time`

**Actions:**
1. Advance time to arrival time
2. Job becomes "eligible" (can be considered by scheduler)
3. Call scheduler to evaluate newly arrived job(s)
4. Scheduler decides: can it run now?
   - If yes → insert into replay engine
   - If no → stays in wait_queue

**Code:**
```cpp
if (next_arrival <= target_time) {
    m_trace.run_until_exclusive(m_replay_ctx, next_arrival);
    m_current_time = next_arrival;
    should_reschedule = true;
}
```

**Important:** Scheduler evaluates ALL jobs where `submit_time <= current_time`, not just the newly arrived job. This is because resource availability may have changed.

#### 2. Job Start Event (Replay Engine)

**When:** Replay engine processes a START event

**Actions:**
1. Housekeeping only (replay engine tracks resources)
2. No rescheduling needed

**Code:**
```cpp
if (next_is_start) {
    m_trace.run_until_inclusive(m_replay_ctx, next_replay_time);
    m_current_time = next_replay_time;
    // No reschedule - just bookkeeping
}
```

**Note:** START events are created by scheduler when it decides a job can run.

#### 3. Job End Event (Replay Engine)

**When:** Replay engine processes an END event

**Actions:**
1. Free resources
2. Remove job from running_jobs
3. Trigger rescheduling (resources now available)

**Code:**
```cpp
if (!next_is_start) {
    m_trace.run_until_inclusive(m_replay_ctx, next_replay_time);
    m_current_time = next_replay_time;
    
    // Remove completed jobs
    for (auto it = m_running_jobs.begin(); it != m_running_jobs.end(); ) {
        if (job.end_time <= current_time) {
            it = m_running_jobs.erase(it);
            m_jobs_completed++;
        } else {
            ++it;
        }
    }
    
    should_reschedule = true;
}
```

## Scheduler Integration

### When Scheduler is Called

The scheduler is called when `should_reschedule == true`:

1. **Job arrival** - new job(s) become eligible
2. **Job completion** - resources freed up
3. **Initial t=0** - handle jobs arriving at start

### Scheduler Loop

```cpp
if (should_reschedule) {
    while (true) {
        free_nodes = total_nodes - nodes_in_use;
        jobs_to_run = scheduler.schedule(wait_queue, free_nodes, 
                                         running_jobs, current_time);
        
        if (jobs_to_run.empty()) {
            break;  // Nothing else can run
        }
        
        // Start selected jobs
        for (job in jobs_to_run) {
            m_trace.insert_job(job, current_time, m_replay_ctx);
            m_running_jobs[job] = current_time;
            m_jobs_submitted++;
        }
        
        // Process START events
        m_trace.run_until_inclusive(m_replay_ctx, current_time);
    }
}
```

**Why loop?**
- First iteration: scheduler selects jobs to run
- After starting jobs, resources decrease
- Loop again: maybe more jobs can backfill with remaining resources
- Continue until scheduler returns empty list

### Replay Engine Integration

When scheduler decides a job can run:

```cpp
m_trace.insert_job(job_idx, start_time, m_replay_ctx);
```

**This creates TWO events:**
1. **START event** at `start_time`
2. **END event** at `start_time + duration`

Both events are inserted into `m_replay_ctx.m_evtq` (ordered by time).

## EASY Backfilling Algorithm

### Core Principles

1. **FCFS Head Gets Reservation**
   - First job in queue (by submit_time) gets guaranteed start time
   - If can't run now, calculate when it CAN run
   - This is the "reservation time"

2. **Backfilling Rules**
   - Other jobs can run early IF:
     - They fit in currently available resources
     - They will complete BEFORE FCFS head's reservation

3. **Resource Tracking**
   - Free nodes = total_nodes - nodes_in_use
   - Updated after each job starts
   - Recalculated when jobs complete

### Scheduler Decision Flow

```cpp
1. Filter wait_queue for eligible jobs (submit_time <= current_time)

2. Sort by priority policy (default: FCFS = submission time)

3. Evaluate FCFS head (first in sorted list):
   if (head_nodes <= available_nodes) {
       // Can run now
       schedule_head()
       available_nodes -= head_nodes
       reservation_time = current_time
   } else {
       // Calculate when it CAN run
       reservation_time = calculate_fcfs_reservation()
   }

4. Try backfilling remaining jobs:
   for (job in remaining_jobs) {
       if (job.nodes <= available_nodes) {
           // Check: will it complete before reservation?
           if (current_time + job.duration <= reservation_time) {
               schedule_job()
               available_nodes -= job.nodes
           }
       }
   }
```

### Reservation Calculation

When FCFS head can't fit:

```cpp
calculate_fcfs_reservation(job, free_nodes, running_jobs, current_time):
    // Find when enough nodes will be free
    end_events = []
    for (running_job in running_jobs) {
        end_time = running_job.start + running_job.duration
        end_events.append((end_time, running_job.nodes))
    }
    
    sort(end_events by time)
    
    available = free_nodes
    for (event in end_events) {
        available += event.nodes
        if (available >= job.nodes) {
            return event.time  // Can start when this job completes
        }
    }
    
    return current_time  // Shouldn't happen if job fits in system
```

## Time Representation

Jobs are submitted with `epoch_t = std::pair<time_t, float>`:
- `.first` = integer seconds since epoch
- `.second` = fractional seconds

**Efficient conversion:**
```cpp
const auto& ts = job.get_submit_time();
sim_time_t submit = static_cast<sim_time_t>(ts.first) + ts.second;
```

**Why?**
- Avoids creating temporary pairs multiple times
- Uses const reference (no copy)

## Resource Tracking

The replay engine maintains accurate resource usage:

```cpp
num_nodes_t nodes_in_use = m_trace.get_nodes_in_use(m_replay_ctx);
num_nodes_t free_nodes = total_nodes - nodes_in_use;
```

**Updated by:**
- START events (increase nodes_in_use)
- END events (decrease nodes_in_use)

## Edge Cases

### Multiple Jobs at t=0

If multiple jobs arrive at t=0:

```cpp
// Check finds ANY job at t=0
has_arrivals_now = true

// Scheduler evaluates ALL jobs with submit_time <= 0
// Loop continues until no more jobs can fit
```

All t=0 jobs are scheduled in the pre-loop phase.

### Jobs with Same Submit Time

Sorted by job index (stable_sort preserves order):

```cpp
std::stable_sort(m_trace.data().begin(), m_trace.data().end());
```

Jobs with same submit_time maintain their original order.

### No Future Arrivals

When `next_arrival == infinity`:

```cpp
if (next_arrival == std::numeric_limits<sim_time_t>::max() && !m_wait_queue.empty()) {
    // Check for jobs at current_time
    // (Main loop case - pre-loop handles t=0)
}
```

This handles the case where all remaining jobs have already arrived.

## Performance

**Time Complexity:**
- Per job arrival: O(W log W) where W = wait_queue size
  - Filtering: O(W)
  - Sorting: O(W log W)
  - Scheduling: O(W)
- Per job completion: O(R + W log W) where R = running_jobs size
  - Update running: O(R)
  - Reschedule: O(W log W)

**Measured Performance:**
- 5 jobs: ~1ms
- 2000 jobs: ~1ms
- Linear scaling with job count

## Example Trace

**Input:** sim_5jobs_simple.csv
```
submit_time, nodes, duration
0,           80,    100
10,          15,    20
20,          60,    50
30,          10,    15
40,          20,    30
```

**Execution Timeline:**

```
t=0: [Pre-loop]
  - Job 0 arrives (80 nodes, 100 duration)
  - 100 free nodes
  - Schedule Job 0 → START at t=0, END at t=100
  
t=10: [Job arrival]
  - Job 1 arrives (15 nodes, 20 duration)
  - 20 free nodes (100 - 80)
  - Schedule Job 1 → START at t=10, END at t=30
  - Backfills before Job 0 ends
  
t=20: [Job arrival]
  - Job 2 arrives (60 nodes, 50 duration)
  - 5 free nodes (100 - 80 - 15)
  - Can't fit, stays in wait_queue
  
t=30: [Job end + arrival]
  - Job 1 ends, free 15 nodes
  - Job 3 arrives (10 nodes, 15 duration)
  - 20 free nodes (100 - 80)
  - Schedule Job 3 → START at t=30, END at t=45
  - Backfills before Job 0 ends
  - Job 2 still can't fit (needs 60, only 10 free)
  
t=40: [Job arrival]
  - Job 4 arrives (20 nodes, 30 duration)
  - 10 free nodes (100 - 80 - 10)
  - Can't fit, stays in wait_queue
  
t=45: [Job end]
  - Job 3 ends, free 10 nodes
  - 20 free nodes (100 - 80)
  - Schedule Job 4 → START at t=45, END at t=75
  - Backfills before Job 0 ends
  - Job 2 still can't fit
  
t=75: [Job end]
  - Job 4 ends, free 20 nodes
  - 40 free nodes (100 - 80)
  - Job 2 still can't fit (needs 60)
  
t=100: [Job end]
  - Job 0 ends, free 80 nodes
  - 100 free nodes
  - Schedule Job 2 → START at t=100, END at t=150
  
t=150: [Job end]
  - Job 2 ends
  - All jobs complete
```

**Result:**
- No over-subscription (max usage: 100 nodes)
- EASY backfilling used (Jobs 1, 3, 4 ran while Job 0 executing)
- Job 2 waited for FCFS head (Job 0) to complete

## Verification

Oracle verification compares DR_EVT output against independent Python implementation:

```bash
python3 compare_with_oracle.py oracle.csv dr_evt_output.csv
```

Checks:
- Start times match
- End times match
- No over-subscription
- EASY backfilling rules followed

## References

- [sim.cpp](../src/sim/sim.cpp) - Main simulation engine
- [scheduler.cpp](../src/sim/scheduler.cpp) - EASY backfilling implementation
- [trace.cpp](../src/trace/trace.cpp) - Replay engine
- [minimal_easy_oracle.py](../minimal_easy_oracle.py) - Reference implementation
