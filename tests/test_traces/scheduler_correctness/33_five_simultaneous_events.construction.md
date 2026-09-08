# 33_five_simultaneous_events

## Scenario
At t=50, four events occur simultaneously: J1 completes, J2 (FCFS) starts, J3 is blocked, J4 backfills past J3.

**System**: 100 nodes total

## Jobs
- J0 (J1 in your description): submit=0, nodes=60, duration=50
- J1 (J2 in your description): submit=10, nodes=30, duration=100
- J2 (J3 in your description): submit=10, nodes=40, duration=50
- J3 (blocked job): submit=10, nodes=35, duration=40
- J4 (backfills): submit=10, nodes=10, duration=30

## Timeline

### t=0
- J0 starts (60 nodes) → 60 used, 40 free

### t=10
- J1, J2, J3, J4 all arrive
- J1 needs 30 (40 free) → **starts** → 90 used, 10 free
- J2 needs 40 (10 free) → WAITS, becomes **FCFS head**
- J3 needs 35 (10 free) → WAITS (behind J2)
- J4 needs 10 (10 free) → Could backfill?
  - J2 (FCFS head) reserved for when 40 nodes available
  - J0 completes at t=50 → 70 free total
  - J2 will start at t=50
  - J4 duration 30, ends at t=40 < t=50 ✓
  - **J4 backfills at t=10** → 100 used, 0 free

### t=40
- J4 completes → 90 used, 10 free

### t=50 (CRITICAL: Four simultaneous events)

**Event sequence at t=50:**
1. **J0 (J1) completes** → 30 used (just J1 running), 70 free
2. **J2 (J3, FCFS head) starts** (needs 40) → 70 used, 30 free
3. **J3 is blocked** - next in queue, needs 35, only 30 free → WAITS
4. **J4 already completed** at t=40, but if there was another small job, it could backfill past J3

Wait, I need a 5th job for J4 to backfill at t=50. Let me reconsider...

Actually, the scenario is complete as-is. At t=50:
- J0 completes
- J2 (FCFS head) starts  
- J3 is next but blocked
- There's no more jobs to backfill

Your description mentioned J4 backfills at this moment, so I need another job that arrives before t=50 but hasn't been scheduled yet.

Let me add J5:
- J5: submit=45, nodes=10, duration=20

## Final Design

Jobs:
- J0: submit=0, nodes=60, duration=50
- J1: submit=10, nodes=30, duration=100
- J2: submit=10, nodes=40, duration=50
- J3: submit=10, nodes=35, duration=40
- J4: submit=10, nodes=10, duration=30 (backfills at t=10)
- J5: submit=45, nodes=15, duration=20 (backfills at t=50)

### t=10
- J1 starts → 90 used, 10 free
- J2,J3,J4 arrive
- J4 backfills → 100 used

### t=40
- J4 completes → 90 used, 10 free

### t=45
- J5 arrives → WAITS (10 free, needs 15)

### t=50 (FOUR EVENTS)
1. **J0 completes** → 30 used, 70 free
2. **J2 (FCFS head) starts** → 70 used, 30 free
3. **J3 is blocked** (needs 35, only 30 free)
4. **J5 backfills past J3** (needs 15, 30 available, duration 20 won't delay J3) → 85 used, 15 free

## Expected Schedule
```
J0: [0, 50]
J1: [10, 110]
J2: [50, 100]  (FCFS head from t=10, starts at t=50)
J3: [110, 150] (blocked at t=50, starts when J1 completes)
J4: [10, 40]   (backfills at t=10)
J5: [50, 70]   (backfills at t=50, jumps past blocked J3)
```

## What This Tests

1. **Job completes** (J0)
2. **FCFS head starts** (J2, was waiting since t=10)
3. **Next job is blocked** (J3, not enough resources)
4. **Later job backfills** (J5, jumps past blocked J3)
5. **Resource state recording**: Must record after EACH event at t=50

## Expected Resource Events at t=50

Should have 3 resource state changes at t=50:
```
t=50: J0 completes → 30 used, 70 free
t=50: J2 starts → 70 used, 30 free
t=50: J5 starts → 85 used, 15 free
```

**THIS IS THE BUG**: If C++ processes "check queue and start waiting jobs" in a batch and only records once, it might jump from "30 used" directly to "85 used", missing the "70 used" intermediate state.
