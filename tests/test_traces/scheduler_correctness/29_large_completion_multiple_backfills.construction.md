# 29_large_completion_multiple_backfills

## Scenario
One large job completes, eventually freeing space for a cascade of
smaller jobs - but the actual, validated schedule does not have all four
small jobs waiting until then. This file previously described a
different, incorrect timeline (and used stale node counts from before
the jobs were given distinguishing sizes - see tests/README.md's note on
fixture identity fixes); corrected below to match the actual EASY
backfilling behavior, verified against the C++ simulator and the Python
reference, which agree exactly, and cross-checked line-by-line against
`29_large_completion_multiple_backfills.expected_resources.csv`.

**System**: 100 nodes total

## Jobs
- Job 0: submit=0, nodes=80 (large), duration=100
- Job 1: submit=10, nodes=12, duration=50
- Job 2: submit=10, nodes=14, duration=50
- Job 3: submit=10, nodes=16, duration=50
- Job 4: submit=10, nodes=18, duration=50

(Jobs 1-4 have distinguishing node counts rather than all being 15 - this
was changed from an earlier version of this fixture where all four were
identical, which meant a job-identity mixup bug wouldn't have been
visible in the test's output. The schedule shape described below is
otherwise unaffected.)

## Timeline

### t=0
- Job 0 starts (80 nodes used, 20 free)

### t=10
- Jobs 1, 2, 3, 4 all arrive
- Job 1 is the FCFS head. It fits (12 <= 20 free) - **the FCFS head
  always starts immediately if it fits, regardless of whether doing so
  would leave less room for jobs behind it in the queue.** Job 1 starts.
  (8 free)
- Job 2 is now the new head. It does NOT fit (14 > 8) - blocked.
  Reservation is computed from currently-running jobs' completion times:
  Job 0 ends at 100 (frees 80), Job 1 ends at 60 (frees 12). Job 2 needs
  14, has 8, deficit is 6 - the earliest completion that frees enough
  (12 >= 6) is Job 1's, at t=60. Reservation = 60.
- Jobs 3 and 4 are backfill candidates, but neither even fits in the 8
  free nodes (16 > 8, 18 > 8) - the reservation window is irrelevant here
  since the node-count check fails first. Both wait.

### t=60 (Job 1 completes)
- Job 1 ends, freeing 12 nodes (8 + 12 = 20 free)
- Job 2 (head) now fits (14 <= 20) - starts. (6 free)
- Job 3 is now the head. Does NOT fit (16 > 6) - blocked. Reservation
  recomputed from currently-running jobs: Job 0 ends at 100 (frees 80),
  Job 2 ends at 110 (frees 14). Deficit is 10, earliest sufficient
  completion is Job 0's at t=100. Reservation = 100.
- Job 4 is a backfill candidate but doesn't fit (18 > 6) either - waits.

### t=100 (Job 0 completes - the large completion this test is named for)
- Job 0 ends, freeing 80 nodes (6 + 80 = 86 free)
- Job 3 (head) fits (16 <= 86) - starts. (70 free)
- Job 4 is now the only remaining waiting job, and the new head. It fits
  (18 <= 70) - starts too, cascading in the same scheduling pass.

### Actual, Validated Schedule
```
Job 0: [0, 100]
Job 1: [10, 60]    (FCFS head, starts immediately since it fits)
Job 2: [60, 110]   (starts once Job 1's completion frees enough room)
Job 3: [100, 150]  (starts once Job 0's completion frees enough room)
Job 4: [100, 150]  (cascades immediately after Job 3 in the same pass)
```

## What This Tests

1. **The FCFS head always starts if it fits** - it is never held back to
   avoid blocking jobs behind it in the queue, even though doing so
   delays them.
2. **Reservation is recomputed each time the head changes**, based on
   whichever currently-running job's completion first frees enough
   capacity for the *new* head - not necessarily the single largest job.
3. **A completion can trigger a cascade of multiple starts** within one
   scheduling pass (Jobs 3 and 4 both starting at t=100).
4. **Resource state recording**: must record after each individual
   completion and each individual start separately, not once per batch.

## Expected Resource Events

(Cross-checked exactly against `29_large_completion_multiple_backfills.expected_resources.csv`)

At t=10:
```
t=10: Job 1 starts -> 92 used, 8 free
```

At t=60:
```
t=60: Job 1 completes -> 80 used, 20 free
t=60: Job 2 starts -> 94 used, 6 free
```

At t=100 (the cascade this test is specifically named for):
```
t=100: Job 0 completes -> 14 used, 86 free
t=100: Job 3 starts -> 30 used, 70 free
t=100: Job 4 starts -> 48 used, 52 free
```

This is what the "Resource State Recording" bug (see tests/README.md)
would have gotten wrong: recording only the state after all of a batch's
starts, rather than after each individual start - which would miss the
intermediate 30-used state between Job 3 and Job 4 starting.
