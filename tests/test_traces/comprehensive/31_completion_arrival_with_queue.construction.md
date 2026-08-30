# 31_completion_arrival_with_queue

## Scenario
Job completes and new job arrives at exact same time, with other jobs already waiting in queue. Tests priority between new arrival and waiting jobs.

**System**: 100 nodes total

## Jobs
- Job 0: submit=0, nodes=50, duration=100
- Job 1: submit=50, nodes=40, duration=50 (arrives while Job 0 running, must wait)
- Job 2: submit=100, nodes=30, duration=50 (arrives exactly when Job 0 completes)
- Job 3: submit=100, nodes=30, duration=50 (also arrives at t=100)

## Timeline

### t=0
- Job 0 starts (50 nodes used, 50 free)

### t=50
- Job 1 arrives (needs 40, only 50 free)
- Job 1 WAITS (could start but let's assume it's blocked by some constraint)
- Actually: Job 1 CAN start (40 < 50)
- Job 1 starts at t=50 → 90 nodes used, 10 free

### t=100 (CRITICAL: Completion + new arrivals + waiting queue)
**Event sequence**:
1. Job 0 completes → 40 nodes used, 60 free
2. Jobs 2, 3 arrive (new submissions at t=100)
3. Job 1 completes → 0 nodes used, 100 free
4. Now Job 2 and 3 both can start (30 + 30 = 60 < 100)
5. Both start at t=100

Wait, let me reconsider this scenario...

Actually, let me redesign for better test:

- Job 0: 50 nodes, 100 duration
- Job 1: arrives at 50, needs 60 nodes (too large), WAITS
- Job 2: arrives at 100, needs 30 nodes
- Job 3: arrives at 100, needs 30 nodes

### Corrected Timeline

### t=0
- Job 0 starts (50 used, 50 free)

### t=50
- Job 1 arrives (needs 60, only 50 free) → WAITS, becomes FCFS head
- Queue: [Job 1]

### t=100 (CRITICAL)
1. Job 0 completes → 0 used, 100 free
2. Jobs 2, 3 arrive (new submissions)
3. Queue processing order:
   - Job 1 (FCFS head) starts → 60 used, 40 free
   - Job 2 can backfill (30 < 40) → 90 used, 10 free
   - Job 3 cannot fit (30 > 10) → WAITS

### Expected Schedule
```
Job 0: [0, 100]
Job 1: [100, 200]  (was waiting, FCFS head, starts at t=100)
Job 2: [100, 150]  (new arrival, backfills at t=100)
Job 3: [150, 200]  (waits for Job 2 to complete)
```

## What This Tests

1. **Completion + simultaneous new arrivals**
2. **Priority between waiting jobs (Job 1) and new arrivals (Jobs 2, 3)**
3. **FCFS head from waiting queue starts first**
4. **New arrivals can backfill if they don't delay FCFS head**
5. **Resource state recording**: Must record after completion, after Job 1 starts, after Job 2 starts

## Expected Resource Events at t=100

Should have 3 events at t=100:
```
t=100: Job 0 completes → 0 used, 100 free
t=100: Job 1 starts → 60 used, 40 free
t=100: Job 2 starts → 90 used, 10 free
```

Then at t=150:
```
t=150: Job 2 completes → 60 used, 40 free
t=150: Job 3 starts → 90 used, 10 free
```

**THIS TESTS**: Mixed event types (completion, waiting job starts, new arrival backfills) all at same timestamp.
