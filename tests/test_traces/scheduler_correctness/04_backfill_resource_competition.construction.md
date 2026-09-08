# 04_backfill_resource_competition

## Scenario
Multiple backfillers competing for limited space.

**Formula**:
- Free space: F=30
- Each backfiller needs: B=15
- Concurrent capacity: K = floor(F/B) = 2
- 3 backfillers arrive, only 2 fit at once

## Analytical Construction

**System**: 100 nodes total

**Jobs**:
- Job 0: submit=0, nodes=70 (70% of system), duration=300
- Job 1: submit=10, nodes=15 (15% of system), duration=50
- Job 2: submit=11, nodes=15 (15% of system), duration=50
- Job 3: submit=12, nodes=15 (15% of system), duration=50
