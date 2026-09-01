# 13_consecutive_fcfs

## Scenario
Test pure FCFS with no parallelism.
Formula: Each job uses all 100 nodes.
Jobs must run sequentially: [0,100], [100,200], [200,300].

## Analytical Construction

**System**: 100 nodes total

**Jobs**:
- Job 0: submit=0, nodes=100 (100% of system), duration=100
- Job 1: submit=10, nodes=100 (100% of system), duration=100
- Job 2: submit=20, nodes=100 (100% of system), duration=100
