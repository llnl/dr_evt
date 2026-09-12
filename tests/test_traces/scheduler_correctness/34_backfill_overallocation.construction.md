# 34_backfill_overallocation

## Scenario

Two 30-node backfill candidates are considered when exactly 30 nodes are free.
Only one may start; every accepted candidate must reduce the capacity used to
evaluate the next candidate.

## Jobs

| Job | Submit | Nodes | Duration |
|---:|---:|---:|---:|
| 0 | 0 | 70 | 100 |
| 1 | 10 | 40 | 200 |
| 2 | 10 | 30 | 50 |
| 3 | 10 | 30 | 50 |

## Schedule derivation

At `t=10`, job 1 is blocked with a reservation at `t=100`. Job 2 fits the 30
free nodes and completes before the reservation, so it backfills and consumes
all remaining nodes. Job 3 cannot also start.

When job 2 finishes at `t=60`, job 3 fits, but its estimated completion at
`t=110` is not before job 1's `t=100` reservation. It therefore continues
waiting. At `t=100`, job 0 finishes; job 1 starts first, followed by job 3.

| Job | Start | End |
|---:|---:|---:|
| 0 | 0 | 100 |
| 1 | 100 | 300 |
| 2 | 10 | 60 |
| 3 | 100 | 150 |

The fixture checks that the backfill scan updates available capacity after
each selection and never allocates more than 100 nodes.
