# 33_five_simultaneous_events

## Scenario

A large completion lets the FCFS head start, leaves the next job blocked, and
allows a later job to backfill in the same scheduling pass.

## Jobs

| Job | Submit | Nodes | Duration |
|---:|---:|---:|---:|
| 0 | 0 | 60 | 50 |
| 1 | 10 | 30 | 100 |
| 2 | 10 | 40 | 50 |
| 3 | 10 | 35 | 40 |
| 4 | 10 | 10 | 30 |
| 5 | 45 | 15 | 20 |

## Schedule derivation

At `t=10`, jobs 1 and 4 start, filling all 100 nodes. Job 4 completes at
`t=40`; job 5 arrives at `t=45` but does not fit.

At `t=50`, job 0 completes. Job 2, the queue head, starts with 40 nodes. Job 3
then needs 35 nodes but only 30 are free. Job 5 fits those 30 nodes and its
20-second estimate ends before job 3's reservation, so it backfills.

| Job | Start | End |
|---:|---:|---:|
| 0 | 0 | 50 |
| 1 | 10 | 110 |
| 2 | 50 | 100 |
| 3 | 100 | 140 |
| 4 | 10 | 40 |
| 5 | 50 | 70 |

This fixture checks the completion, head start, blocked successor, and
backfill decision at `t=50`, with each resource-state transition recorded.
