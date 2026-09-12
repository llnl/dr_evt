# 31_completion_arrival_with_queue

## Scenario

A completion and two new submissions occur at the same timestamp while an
older job is waiting. The older FCFS head must be considered first.

## Jobs

| Job | Submit | Nodes | Duration |
|---:|---:|---:|---:|
| 0 | 0 | 50 | 100 |
| 1 | 50 | 60 | 100 |
| 2 | 100 | 28 | 50 |
| 3 | 100 | 32 | 50 |

## Schedule derivation

Job 0 starts at `t=0`. Job 1 arrives at `t=50` but cannot fit in the 50 free
nodes.

At `t=100`, job 0 completes and jobs 2 and 3 arrive. Job 1 starts first,
leaving 40 nodes. Job 2 then starts, leaving 12 nodes, so job 3 waits until job
2 finishes at `t=150`.

| Job | Start | End |
|---:|---:|---:|
| 0 | 0 | 100 |
| 1 | 100 | 200 |
| 2 | 100 | 150 |
| 3 | 150 | 200 |

This fixture checks ordering among a completion, an existing wait-queue head,
and new same-time arrivals, including separate resource-history records for
each state change.
