# 24_multiple_running_jobs

## Scenario
FCFS head waits for multiple running jobs.

**Formula**:
- Job 0: R0=30, ends at T0=100
- Job 1: R1=40, ends at T1≈200
- Free: F = TOTAL - R0 - R1 = 30
- Job 2: R2=70 > (TOTAL - R0) and > (TOTAL - R1)
- Job 2 needs BOTH jobs to finish → reservation ≈ T1
- Job 3: R3=20 <= F, completes before T1

## Analytical Construction

**System**: 100 nodes total

**Jobs**:
- Job 0: submit=0, nodes=30 (30% of system), duration=100
- Job 1: submit=5, nodes=40 (40% of system), duration=195
- Job 2: submit=10, nodes=70 (70% of system), duration=300
- Job 3: submit=20, nodes=20 (20% of system), duration=120
