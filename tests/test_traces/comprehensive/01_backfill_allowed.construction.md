# 01_backfill_allowed

## Scenario
Classic backfill success case.

**Formula**:
- Job 0: R0=70 (70% of system), D0=200
- Free: F = TOTAL - R0 = 30
- Job 1 (FCFS head): R1=45 > F (needs 15 more), D1=300
- Job 1 reservation: t=200 (when Job 0 finishes)
- Job 2: R2=20 <= F (fits!), D2=50 << D0
- Job 2 completes at: 20 + 50 = 70 < 200 ✓ Backfills!

## Analytical Construction

**System**: 100 nodes total

**Jobs**:
- Job 0: submit=0, nodes=70 (70% of system), duration=200
- Job 1: submit=10, nodes=45 (45% of system), duration=300
- Job 2: submit=20, nodes=20 (20% of system), duration=50
