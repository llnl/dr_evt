# 05_multiple_backfills

## Scenario
Multiple simultaneous backfills.

**Formula**:
- Free space: F=30
- N=3 backfillers, each B=10
- Total needed: N × B = 30 < F ✓
- All 3 can backfill concurrently

## Analytical Construction

**System**: 100 nodes total

**Jobs**:
- Job 0: submit=0, nodes=70 (70% of system), duration=300
- Job 1: submit=10, nodes=10 (10% of system), duration=50
- Job 2: submit=15, nodes=10 (10% of system), duration=50
- Job 3: submit=20, nodes=10 (10% of system), duration=50
