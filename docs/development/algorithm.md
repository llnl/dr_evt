# Simulation Algorithm - Event Loop

## Overview

The simulator uses an event-driven approach where time advances by processing discrete events in chronological order.

## Key Components

### 1. Wait Queue (`m_wait_queue`)
- Contains ALL jobs that have been submitted but not yet started
- Includes both:
  - **Eligible jobs**: `submit_time <= current_time` (can be scheduled NOW)
  - **Future jobs**: `submit_time > current_time` (not yet submitted)
- Jobs are removed from wait_queue when scheduler decides to start them

### 2. Running Jobs (`m_running_jobs`)
- Map of `job_idx → start_time` for currently executing jobs
- Updated when jobs start and removed when they complete

### 3. Replay Context (`m_replay_ctx.m_evtq`)
- Event queue managed by replay engine
- Contains START and END events for scheduled jobs
- Events are sorted by time

## Division of Responsibilities

### Scheduler (`scheduler.cpp`)
**Role:** Decides WHICH jobs to start

**Behavior:**
- Only considers **eligible jobs** where `submit_time <= current_time`
- Jobs with `submit_time > current_time` are IGNORED (don't exist yet)
- Returns `std::vector<job_no_t>` - list of jobs to start
- Does NOT create events

**Implementation:**
```cpp
// scheduler.cpp lines 43-50
for (job_no_t job_idx : wait_queue) {
    const auto& job = (*m_job_data_ptr)[job_idx];
    const auto& ts = job.get_submit_time();
    sim_time_t submit_time = static_cast<sim_time_t>(ts.first) + ts.second;
    
    if (submit_time <= current_time) {
        // ONLY eligible jobs
        sorted_jobs.push_back(job_idx);
    }
    // Jobs with submit_time > current_time are SKIPPED
}
```

### Replay Engine (`trace.cpp`)
**Role:** Creates events when jobs are scheduled

**Behavior:**
- Receives decisions from scheduler via `insert_job(job_idx, time, ctx)`
- Creates **BOTH** START and END events:
  - START event at `time`
  - END event at `time + duration`
- Adds events to `replay_ctx.m_evtq`

### Event Loop (`sim.cpp`)
**Role:** Advances time and coordinates scheduling

## Time Advancement Algorithm

**Time advances by processing whichever comes first:**

1. **Next replay event** (START or END from replay_ctx.m_evtq), OR
2. **Next job arrival** (from wait_queue where `submit_time > current_time`)

### Event Types

**START Event:**
- A scheduled job begins execution
- **Action:** Housekeeping only (update bookkeeping)
- **Does NOT trigger rescheduling** (scheduler already decided this job should run)

**END Event:**
- A running job completes
- **Action:** Free resources, remove from running_jobs
- **DOES trigger rescheduling** (newly freed resources may allow waiting jobs to start)

**JOB ARRIVAL Event:**
- A job's submit_time is reached
- **Action:** None (job already in wait_queue)
- **DOES trigger rescheduling** (new job may be able to start immediately)

## Main Event Loop Logic

```
while (current_time < target_time || !wait_queue.empty()):
    
    1. Find next_arrival = earliest submit_time > current_time in wait_queue
    2. Find next_replay = earliest event time in replay_ctx.m_evtq
    
    3. Decide which event to process:
       
       IF next_replay exists AND next_replay <= next_arrival AND next_replay <= target_time:
           IF next_replay is START:
               → Process START event
               → Advance current_time to next_replay
               → NO rescheduling
           ELSE (END event):
               → Process END event  
               → Advance current_time to next_replay
               → Trigger rescheduling
       
       ELSE IF next_arrival <= target_time:
           → Process arrival
           → Advance current_time to next_arrival
           → Trigger rescheduling
       
       ELSE:
           → No more events before target_time
           → Break loop
    
    4. IF rescheduling triggered:
           LOOP:
               jobs_to_run = scheduler.schedule(wait_queue, free_nodes, running_jobs, current_time)
               
               IF jobs_to_run is empty:
                   BREAK  // Scheduler can't start anything
               
               FOR each job in jobs_to_run:
                   trace.insert_job(job, current_time, replay_ctx)  // Creates START+END events
                   running_jobs[job] = current_time
                   wait_queue.erase(job)
               
               // Process START events at current_time
               trace.run_until_inclusive(replay_ctx, current_time)
```

## Critical Rules

### Rule 1: Scheduler Only Sees Eligible Jobs
**Jobs with `submit_time > current_time` don't exist yet.**

The scheduler MUST filter:
```cpp
if (submit_time <= current_time) {
    // Consider this job
} else {
    // SKIP - not yet submitted
}
```

### Rule 2: Time Advances Only by Processing Events
Time NEVER advances without processing an event:
- Process replay event → time = event_time
- Process arrival → time = arrival_time
- No events → break loop

**NEVER:** Call scheduler repeatedly without advancing time (causes infinite loop)

### Rule 3: START Events Don't Reschedule
When processing a START event:
- Just update bookkeeping
- Don't call scheduler (we already decided this job runs)

### Rule 4: END Events Always Reschedule
When processing an END event:
- Free resources
- Call scheduler (waiting jobs may now fit)

### Rule 5: Arrivals Always Reschedule  
When current_time reaches a job's submit_time:
- Call scheduler (new job may start immediately)

## Common Bugs

### Bug 1: Infinite Loop - Calling Scheduler Without Advancing Time
**Symptom:** Simulator hangs, current_time stuck

**Cause:** Loop condition has `have_eligible_jobs_now` → call scheduler → returns empty → loop again

**Fix:** Remove the problematic condition. Only call scheduler after:
- Processing a replay event, OR
- Processing an arrival

**Wrong:**
```cpp
if (have_eligible_jobs) {
    should_reschedule = true;  // BUG: No time advancement!
}
```

**Correct:**
```cpp
// Only reschedule after processing an event
if (processed_replay_event || processed_arrival) {
    should_reschedule = true;
}
```

### Bug 2: Scheduler Considers Future Jobs
**Symptom:** Jobs start before their submit_time

**Cause:** Scheduler doesn't filter by `submit_time <= current_time`

**Fix:** Filter eligible jobs in scheduler

### Bug 3: O(n²) Performance
**Symptom:** Simulator slow on large traces (50+ jobs)

**Cause:** Scanning full wait_queue on every loop iteration

**Fix:** 
- Combine loops that scan wait_queue
- Exit early when finding next arrival
- Don't create unnecessary copies

## Example: Timeline of Events

```
Input trace:
Job 0: submit=0, nodes=80, duration=50
Job 1: submit=0, nodes=60, duration=30  
Job 2: submit=10, nodes=10, duration=20

Event timeline:

t=0: (Arrival)
  - Job 0, Job 1 arrive (both in wait_queue now)
  - Call scheduler
  - Scheduler returns [Job 0] (FCFS head, fits)
  - insert_job(0, 0) → creates START@0, END@50
  - Job 0 removed from wait_queue
  - Job 1 gets reservation@50

t=0: (START - same time, processed next)
  - Job 0 START event
  - No rescheduling

t=10: (Arrival)
  - Job 2 arrives
  - Call scheduler
  - Backfill check: 10+20=30 < 50 ✓
  - Scheduler returns [Job 2]
  - insert_job(2, 10) → creates START@10, END@30
  - Job 2 removed from wait_queue

t=10: (START)
  - Job 2 START event
  - No rescheduling

t=30: (END)
  - Job 2 END event
  - Free 10 nodes
  - Call scheduler
  - Scheduler returns [] (Job 1 still doesn't fit)

t=50: (END)
  - Job 0 END event  
  - Free 80 nodes
  - Call scheduler
  - Scheduler returns [Job 1]
  - insert_job(1, 50) → creates START@50, END@80

t=50: (START)
  - Job 1 START event
  - No rescheduling

t=80: (END)
  - Job 1 END event
  - All jobs complete
  - wait_queue empty
  - Loop exits
```

## Performance Considerations

1. **Minimize wait_queue scans:** Combine multiple loops into one
2. **Early termination:** Stop scanning when next_arrival found
3. **Avoid copies:** Work with wait_queue directly, don't copy to intermediate sets
4. **Cache event times:** Don't recompute submit_time for same job repeatedly

## Verification

After simulation:
- All jobs should have `begin_time > 0` and `end_time > 0` (except if never started)
- Resource constraint: At any time, `allocated_nodes <= total_nodes`
- Causality: All jobs have `begin_time >= submit_time`
- Completeness: All jobs in input should be processed
