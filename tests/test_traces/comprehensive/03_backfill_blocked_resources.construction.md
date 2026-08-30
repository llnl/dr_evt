# 03_backfill_blocked_resources

## Scenario
Backfill blocked by insufficient resources.

**Formula**:
- Job 0: R0=90 (90%), F=10 (10%) free
- Job 1: R1=50 > F
- Job 2: R2=20 > F ✗ Doesn't fit!

## Analytical Construction

**System**: 100 nodes total

**Jobs**:
- Job 0: submit=0, nodes=90 (90% of system), duration=200
- Job 1: submit=10, nodes=50 (50% of system), duration=300
- Job 2: submit=20, nodes=20 (20% of system), duration=50
