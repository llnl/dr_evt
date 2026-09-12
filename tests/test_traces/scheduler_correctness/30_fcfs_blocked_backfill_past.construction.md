# 30_fcfs_blocked_backfill_past

## Scenario
FCFS head job starts, second job becomes new FCFS head but is blocked (too large), third job backfills past it.

**System**: 100 nodes total

## Jobs
- Job 0: submit=0, nodes=50, duration=50
- Job 1: submit=10, nodes=60 (large, will be blocked), duration=100  
- Job 2: submit=10, nodes=30 (small, can backfill), duration=40

## Timeline

### t=0
- Job 0 starts (50 nodes used, 50 free)
- Queue: []

### t=10
- Job 1 arrives (needs 60 nodes, only 50 free) → WAITS, becomes FCFS head
- Job 2 arrives (needs 30 nodes, only 50 free) → WAITS
- Queue: [Job 1 (FCFS head), Job 2]
- Job 1 gets reservation: when Job 0 completes at t=50, Job 1 will start

### t=50 (CRITICAL: FCFS head starts, blocked job becomes new head, backfill happens)
**Event sequence at t=50**:
1. Job 0 completes → 100 nodes free
2. Job 1 starts (FCFS head) → 40 nodes free
3. Job 2 becomes new FCFS head
4. Job 2 CAN start now (needs 30, have 40)
5. **Job 2 starts at t=50**

### Expected Schedule
```
Job 0: [0, 50]
Job 1: [50, 150]  (was FCFS head at t=10, starts when Job 0 completes)
Job 2: [50, 90]   (starts after Job 1 in the same scheduling pass)
```

## What This Tests

1. **FCFS head transition**: Job 1 was head, starts at t=50, Job 2 becomes new head
2. **Cascading FCFS starts**: Job 2 becomes the head and starts immediately
3. **Simultaneous completion and two FCFS starts**: All at t=50
4. **Resource state recording**: Must record after completion, after Job 1 starts, after Job 2 starts

## Expected Resource Events at t=50

Should have 3 events at t=50:
```
t=50: Job 0 completes → 0 used, 100 free
t=50: Job 1 starts → 60 used, 40 free
t=50: Job 2 starts → 90 used, 10 free
```

The fixture requires the state after job 2 starts to be recorded separately.
