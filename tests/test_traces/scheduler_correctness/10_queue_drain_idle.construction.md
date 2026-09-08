# 10_queue_drain_idle

## Scenario
Test queue drainage and idle periods.
Formula: Each job = 30 = TOTAL_NODES × 0.3.
Jobs 0 and 1 can run together (2 × 0.3 = 0.6 < 1.0).
Gap = 500 time units before next job.

## Analytical Construction

**System**: 100 nodes total

**Jobs**:
- Job 0: submit=0, nodes=30 (30% of system), duration=50
- Job 1: submit=10, nodes=30 (30% of system), duration=50
- Job 2: submit=500, nodes=30 (30% of system), duration=50
