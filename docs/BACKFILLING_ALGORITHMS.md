# Scheduling Policies

DR_EVT separates queue ordering from the rule that permits a lower-priority
job to start ahead of the queue head. Command-line names and defaults are in
[Command-Line Options](user-guide/command-line.md#scheduling-policies).

## Priority policies

- **FCFS** orders jobs by submission time and preserves input order for ties.
- **SJF** orders jobs by ascending estimated run time.
- **LJF** orders jobs by descending estimated run time.

The `fcfs_alt` policy is an alternative FCFS implementation used for
differential testing. `fcfs_conservative` selects the FCFS implementation that
maintains the reservations needed for conservative backfilling.

## Backfilling policies

At each arrival or completion time, the scheduler first starts as many jobs
from the front of the eligible queue as currently fit. If the new head is
blocked, the configured backfilling policy determines whether a later job may
start.

### None

No job may bypass the blocked queue head.

### EASY

EASY protects the blocked head's reservation:

1. Starting with the currently free nodes, inspect the estimated completion
   times of running jobs until enough nodes would be available for the head.
   That time is the head's reservation.
2. Scan later eligible jobs in priority order.
3. Start a candidate only if it fits the currently free nodes and its
   estimated completion is strictly before the reservation.

Only the queue head receives a reservation, so jobs deeper in the queue can be
delayed by backfilling.

### Conservative

Conservative backfilling protects every earlier waiting job. For each
candidate, the scheduler calculates the reservations of the jobs ahead of it
and uses their earliest reservation as the candidate's backfill window. The
candidate starts only if it fits and completes strictly before that window.
Jobs accepted earlier in the same scheduling pass are included when later
candidates are evaluated.

Use `--priority_policy fcfs_conservative --backfill_policy conservative` for
the full FCFS conservative-reservation implementation. It currently uses the
`deque` wait queue.

## Runtime estimates and completion

Reservations and backfill checks use each job's requested `time_limit`, which
is the information available to the scheduler. A simulation job's observed
run time may be shorter, depending on `--run_time_mode`. An early completion
releases its nodes and triggers another scheduling decision; it does not
retroactively change an earlier reservation calculation.

The completion test is strict: a candidate whose estimated completion equals
the protected reservation is not accepted as a backfill job.

## Comparison

| Policy | Reservations protected | Main trade-off |
|---|---:|---|
| None | Queue order only | No backfill utilization benefit |
| EASY | Blocked queue head | More backfill opportunities; no guarantee for deeper jobs |
| Conservative | All earlier waiting jobs | Stronger start-time protection; more scheduling work and fewer opportunities |

The priority policy determines which job is considered "earlier" in this
table. Queue-storage implementations do not change the intended scheduling
result; see [Wait Queues](dev/WAIT_QUEUES.md).

## References

- Lifka, D. A. (1995), *The ANL/IBM SP Scheduling System*.
- Mu'alem, A. W., and Feitelson, D. G. (2001), *Utilization,
  Predictability, Workloads, and User Runtime Estimates in Scheduling the IBM
  SP2 with Backfilling*.
- Feitelson, D. G., and Weil, A. M. (1998), *Utilization and Predictability in
  Scheduling the IBM SP2 with Backfilling*.
