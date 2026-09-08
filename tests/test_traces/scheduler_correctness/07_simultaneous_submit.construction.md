# 07_simultaneous_submit

## Scenario
Test simultaneous submission and FCFS ordering.
Formula: 4 jobs, each using 25 = TOTAL_NODES/4 nodes.
All fit simultaneously, all have same duration.

## Analytical Construction

**System**: 100 nodes total

**Jobs**:
- Job 0: submit=0, nodes=25 (25% of system), duration=100
- Job 1: submit=0, nodes=25 (25% of system), duration=100
- Job 2: submit=0, nodes=25 (25% of system), duration=100
- Job 3: submit=0, nodes=25 (25% of system), duration=100
