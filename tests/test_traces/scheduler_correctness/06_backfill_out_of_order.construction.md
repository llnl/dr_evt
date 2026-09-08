# 06_backfill_out_of_order

## Scenario
Out-of-order backfilling.

**Formula**:
- Job 1 arrives at t=10: R1=50 > F, becomes FCFS head
- Job 2 arrives at t=15 (later!): R2=20 <= F, D2=50
- Job 2 finishes at: 15 + 50 = 65 < 100 ✓
- Job 2 starts before Job 1 (out of arrival order)

## Analytical Construction

**System**: 100 nodes total

**Jobs**:
- Job 0: submit=0, nodes=70 (70% of system), duration=100
- Job 1: submit=10, nodes=50 (50% of system), duration=150
- Job 2: submit=15, nodes=20 (20% of system), duration=50
