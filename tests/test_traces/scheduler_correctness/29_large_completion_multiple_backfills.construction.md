# 29_large_completion_multiple_backfills

## Scenario

A 100-node system starts an 80-node job. Four smaller jobs arrive together;
the first two begin at different completion events, while the final two start
in one scheduling pass when the large job completes.

## Jobs

| Job | Submit | Nodes | Duration |
|---:|---:|---:|---:|
| 0 | 0 | 80 | 100 |
| 1 | 10 | 12 | 50 |
| 2 | 10 | 14 | 50 |
| 3 | 10 | 16 | 50 |
| 4 | 10 | 18 | 50 |

## Schedule derivation

At `t=10`, job 1 fits in the 20 free nodes and starts. Job 2 becomes the
blocked head; jobs 3 and 4 do not fit in the remaining 8 nodes.

At `t=60`, job 1 releases 12 nodes, so job 2 starts. Job 3 is then blocked
until job 0 finishes.

At `t=100`, job 0 releases 80 nodes. Jobs 3 and 4 both fit and start in the
same scheduling pass.

| Job | Start | End |
|---:|---:|---:|
| 0 | 0 | 100 |
| 1 | 10 | 60 |
| 2 | 60 | 110 |
| 3 | 100 | 150 |
| 4 | 100 | 150 |

This fixture checks reservation recalculation when the queue head changes,
multiple starts after one completion, and a distinct resource-history record
for every completion and start.
