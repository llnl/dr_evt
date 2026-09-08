# 02_backfill_blocked_time

## Scenario
Backfill blocked by time window.

**Formula**:
- Job 0: R0=70, D0=100
- Job 1 reservation: t=100
- Job 2: R2=20 <= 30 (fits), D2=150
- Job 2 would complete at: 20 + 150 = 170 > 100 ✗ Blocked!

## Analytical Construction

**System**: 100 nodes total

**Jobs**:
- Job 0: submit=0, nodes=70 (70% of system), duration=100
- Job 1: submit=10, nodes=45 (45% of system), duration=200
- Job 2: submit=20, nodes=20 (20% of system), duration=150
