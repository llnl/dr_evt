# 32_simultaneous_backfill_with_reservation

## Scenario
Tests simultaneous backfilling when FCFS head has a reservation.

**System**: 100 nodes total

## Jobs
- J0 (J1 in description): submit=0, nodes=90, duration=20
- J1 (J2 in description): submit=0, nodes=20, duration=50
- J2 (J3 in description): submit=0, nodes=5, duration=5
- J3 (J4 in description): submit=0, nodes=5, duration=8

## Timeline

### t=0
- All 4 jobs arrive simultaneously
- J0 starts immediately (90 nodes) → 90 used, 10 free
- J1 needs 20 nodes (only 10 free) → WAITS, becomes FCFS head
- J1 gets reservation: will start when J0 completes at t=20
- J2 needs 5 nodes (10 available) → can start now, duration 5 won't delay J1
- J3 needs 5 nodes (10 available) → can start now, duration 8 won't delay J1

**Key decision at t=0**: Both J2 and J3 can backfill!
- After J0 starts: 10 nodes free
- J1 (FCFS head) reserved for t=20
- J2 (duration 5) ends at t=5 < t=20 ✓ doesn't delay J1
- J3 (duration 8) ends at t=8 < t=20 ✓ doesn't delay J1
- Both J2 AND J3 start at t=0

### t=0 (CRITICAL: Multiple simultaneous backfills)
Events at t=0:
1. J0 starts → 90 used, 10 free
2. J2 backfills → 95 used, 5 free
3. J3 backfills → 100 used, 0 free

### t=5
- J2 completes → 95 used, 5 free

### t=8
- J3 completes → 90 used, 10 free

### t=20
- J0 completes → 0 used, 100 free
- J1 starts (reservation fulfilled) → 20 used, 80 free

### t=70
- J1 completes → 0 used, 100 free

## Expected Schedule
```
J0: [0, 20]
J1: [20, 70]   (FCFS head, reserved for t=20)
J2: [0, 5]     (backfills, doesn't delay J1)
J3: [0, 8]     (backfills, doesn't delay J1)
```

## What This Tests

1. **FCFS head with reservation** (J1 reserved for t=20)
2. **Multiple jobs backfilling simultaneously** (J2 and J3 both at t=0)
3. **Reservation checking**: Both J2 and J3 finish before J1's reservation time
4. **Resource state recording**: Must record after J0 starts, after J2 starts, after J3 starts

## Expected Resource Events at t=0

Should have 3 events at t=0:
```
t=0: J0 starts → 90 used, 10 free
t=0: J2 starts → 95 used, 5 free
t=0: J3 starts → 100 used, 0 free
```

**THIS IS THE BUG**: If C++ processes J2 and J3 backfills in a loop and only records once, it would miss the intermediate state and jump from "90 used" to "100 used", losing the "95 used" event.
