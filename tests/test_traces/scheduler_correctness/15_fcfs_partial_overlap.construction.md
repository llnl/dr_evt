# 15_fcfs_partial_overlap

## Scenario
Test partial overlap - some jobs run together, others wait.
Formula: Jobs 0,1 each use 50 = TOTAL_NODES/2.
Job 2 uses all 100 nodes, must wait for job 0.

## Analytical Construction

**System**: 100 nodes total

**Jobs**:
- Job 0: submit=0, nodes=50 (50% of system), duration=100
- Job 1: submit=10, nodes=50 (50% of system), duration=100
- Job 2: submit=50, nodes=100 (100% of system), duration=100
