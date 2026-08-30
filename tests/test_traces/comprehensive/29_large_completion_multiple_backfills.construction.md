# 29_large_completion_multiple_backfills

## Scenario
One large job completes, freeing space for multiple small jobs to backfill simultaneously.

**System**: 100 nodes total

## Jobs
- Job 0: submit=0, nodes=80 (large), duration=100
- Jobs 1,2,3,4: submit=10, nodes=15 each (small), duration=50

## Timeline

### t=0
- Job 0 starts (80 nodes used, 20 free)

### t=10
- Jobs 1, 2, 3, 4 all arrive
- Each needs 15 nodes, only 20 free
- Job 1 could fit (15 < 20) but would block others
- In EASY backfilling: Job 1 becomes FCFS head, gets reservation
- Jobs 2,3,4 cannot backfill (would delay Job 1)
- All four WAIT

### t=100 (CRITICAL: Large completion triggers cascade of backfills)
- Job 0 completes → 100 nodes free
- Jobs 1,2,3,4 all CAN start now (15×4=60 < 100)
- **All four should start at t=100**

### Expected Schedule
```
Job 0: [0, 100]
Job 1: [100, 150]  (FCFS head, starts immediately)
Job 2: [100, 150]  (backfills)
Job 3: [100, 150]  (backfills)
Job 4: [100, 150]  (backfills)
```

## What This Tests

1. **Single large completion** freeing significant resources
2. **Cascade of multiple backfills** from one completion event
3. **Resource state recording**: Must record after the completion and after EACH of the 4 starts

## Expected Resource Events at t=100

Should have 5 events at t=100:
```
t=100: Job 0 completes → 0 used, 100 free
t=100: Job 1 starts → 15 used, 85 free
t=100: Job 2 starts → 30 used, 70 free
t=100: Job 3 starts → 45 used, 55 free
t=100: Job 4 starts → 60 used, 40 free
```

**THIS IS THE BUG**: If C++ processes all 4 starts in a loop and only records once at the end, it would miss events 2, 3, 4 and jump straight from "0 used" to "60 used".
