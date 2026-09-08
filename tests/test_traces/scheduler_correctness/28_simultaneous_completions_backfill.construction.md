# 28_simultaneous_completions_backfill

## Scenario
Test multiple jobs completing simultaneously and multiple jobs backfilling at the same time.

**System**: 100 nodes total

## Jobs
- Jobs 0,1,2: submit=0, nodes=30 each (90 nodes total), duration=100
- Jobs 3,4,5: submit=10, nodes=20 each, duration=50

## Timeline

### t=0
- Jobs 0, 1, 2 all start (90 nodes used, 10 free)

### t=10  
- Jobs 3, 4, 5 arrive
- Only 10 nodes free, each needs 20
- All three jobs WAIT (cannot start)

### t=100 (CRITICAL: Simultaneous completions + multiple backfills)
- Jobs 0, 1, 2 ALL complete at same time
- Now 100 nodes free
- Jobs 3, 4, 5 all CAN backfill (20+20+20=60 < 100)
- **ALL THREE should start at t=100**

### Expected Schedule
```
Job 0: [0, 100]
Job 1: [0, 100]
Job 2: [0, 100]
Job 3: [100, 150]  (backfills)
Job 4: [100, 150]  (backfills)
Job 5: [100, 150]  (backfills)
```

## What This Tests

1. **Multiple simultaneous completions** (jobs 0,1,2 at t=100)
2. **Multiple simultaneous backfills** (jobs 3,4,5 all start at t=100)
3. **Resource state recording**: Must record after EACH of the 3 completions and after EACH of the 3 backfills

## Expected Resource Events at t=100

Should have 6 events at t=100:
```
t=100: Job 0 completes → 60 used, 40 free
t=100: Job 1 completes → 30 used, 70 free
t=100: Job 2 completes → 0 used, 100 free
t=100: Job 3 starts → 20 used, 80 free
t=100: Job 4 starts → 40 used, 60 free
t=100: Job 5 starts → 60 used, 40 free
```

**THIS IS THE BUG**: C++ might only record 1-2 of these states instead of all 6.
