#!/usr/bin/env python3
"""
Generate all 20 analytical oracle CSV files from hand-traced results.

Each function below contains the hand-traced execution for one test.
"""

import csv
from pathlib import Path

def write_analytical_oracle(test_name, jobs):
    """Write analytical oracle CSV."""
    outfile = Path("tests/test_traces/correctness") / f"{test_name}_analytical.csv"
    with open(outfile, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['job_idx', 'submit_time', 'start_time', 'end_time', 'nodes', 'duration'])
        for job in jobs:
            writer.writerow([
                job['job_idx'], job['submit_time'], job['start_time'],
                job['end_time'], job['nodes'], job['duration']
            ])
    print(f"✓ {test_name}")

# ============================================================================
# Hand-Traced Analytical Oracles
# ============================================================================

def backfill_3jobs_input():
    """
    Input:
    - Job 0: submit=0, nodes=80, duration=100
    - Job 1: submit=10, nodes=15, duration=50

    Trace:
    t=0: Job 0 arrives, fits (80 ≤ 100) → START Job 0
         Resources: 20 free, 80 allocated
    t=10: Job 1 arrives
          Job 0 running (ends at t=100)
          Job 0 is FCFS head? No, Job 0 already running
          Wait queue: [Job 1]
          Job 1 is FCFS head, fits (15 ≤ 20) → START Job 1
          Resources: 5 free, 95 allocated
    t=60: Job 1 ends
          Resources: 20 free, 80 allocated
    t=100: Job 0 ends
           Resources: 100 free, 0 allocated
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 100, 'nodes': 80, 'duration': 100},
        {'job_idx': 1, 'submit_time': 10, 'start_time': 10, 'end_time': 60, 'nodes': 15, 'duration': 50},
    ]

def backfill_blocked_input():
    """
    Input:
    - Job 0: submit=0, nodes=80, duration=50
    - Job 1: submit=10, nodes=60, duration=30
    - Job 2: submit=20, nodes=15, duration=40

    Trace:
    t=0: Job 0 arrives, fits → START
         Free: 20
    t=10: Job 1 arrives
          Wait queue: [Job 1]
          Job 1 needs 60 > 20, cannot fit
          Reservation: Job 0 ends at t=50, then 100 ≥ 60 → reservation=50
    t=20: Job 2 arrives
          Wait queue: [Job 1, Job 2] (FCFS order by submit time)
          Job 1 is FCFS head, reservation=50
          Backfill check Job 2: fits (15 ≤ 20)? Yes
                                completes before 50? 20+40=60 < 50? No (60 > 50)
          → Job 2 blocked
    t=50: Job 0 ends, free=100
          Job 1 fits → START, free=40
          Job 2 fits → START, free=25
    t=80: Job 1 ends, free=85
    t=90: Job 2 ends, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 50, 'nodes': 80, 'duration': 50},
        {'job_idx': 1, 'submit_time': 10, 'start_time': 50, 'end_time': 80, 'nodes': 60, 'duration': 30},
        {'job_idx': 2, 'submit_time': 20, 'start_time': 50, 'end_time': 90, 'nodes': 15, 'duration': 40},
    ]

def basic_2jobs_input():
    """
    Input:
    - Job 0: submit=0, nodes=10, duration=50
    - Job 1: submit=0, nodes=20, duration=50

    Trace:
    t=0: Jobs 0,1 arrive (same time)
         FCFS order: [Job 0, Job 1] (tie-break by index)
         Job 0 fits → START, free=90
         Job 1 fits → START, free=70
    t=50: Both end, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 50, 'nodes': 10, 'duration': 50},
        {'job_idx': 1, 'submit_time': 0, 'start_time': 0, 'end_time': 50, 'nodes': 20, 'duration': 50},
    ]

def bf01_basic_success_input():
    """
    Input:
    - Job 0: submit=0, nodes=80, duration=100
    - Job 1: submit=10, nodes=15, duration=30

    Trace:
    t=0: Job 0 → START, free=20
    t=10: Job 1 arrives
          Wait queue: [Job 1]
          Job 1 is FCFS head, fits (15 ≤ 20) → START
          (No reservation needed, job fits immediately)
    t=40: Job 1 ends, free=20
    t=100: Job 0 ends, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 100, 'nodes': 80, 'duration': 100},
        {'job_idx': 1, 'submit_time': 10, 'start_time': 10, 'end_time': 40, 'nodes': 15, 'duration': 30},
    ]

def bf02_blocked_time_input():
    """
    Input:
    - Job 0: submit=0, nodes=80, duration=50
    - Job 1: submit=0, nodes=60, duration=30
    - Job 2: submit=10, nodes=15, duration=45

    Trace:
    t=0: Jobs 0,1 arrive
         FCFS: [Job 0, Job 1]
         Job 0 fits → START, free=20
         Job 1 cannot fit (60 > 20)
         Reservation for Job 1: Job 0 ends at 50, then 100 ≥ 60 → res=50
    t=10: Job 2 arrives
          Wait queue: [Job 1, Job 2]
          Job 1 is FCFS head, reservation=50
          Backfill check Job 2: fits (15 ≤ 20)? Yes
                                completes < 50? 10+45=55 < 50? No
          → Blocked
    t=50: Job 0 ends, free=100
          Job 1 fits → START, free=40
          Job 2 fits → START, free=25
    t=80: Job 1 ends, free=85
    t=95: Job 2 ends, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 50, 'nodes': 80, 'duration': 50},
        {'job_idx': 1, 'submit_time': 0, 'start_time': 50, 'end_time': 80, 'nodes': 60, 'duration': 30},
        {'job_idx': 2, 'submit_time': 10, 'start_time': 50, 'end_time': 95, 'nodes': 15, 'duration': 45},
    ]

def bf03_blocked_resources_input():
    """
    Input:
    - Job 0: submit=0, nodes=90, duration=100
    - Job 1: submit=10, nodes=20, duration=30

    Trace:
    t=0: Job 0 fits (90 ≤ 100) → START, free=10
    t=10: Job 1 arrives
          Wait queue: [Job 1]
          Job 1 needs 20 > 10, cannot fit
          Reservation: Job 0 ends at 100 → res=100
          No backfill (Job 1 is FCFS head)
    t=100: Job 0 ends, free=100
           Job 1 fits → START, free=80
    t=130: Job 1 ends, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 100, 'nodes': 90, 'duration': 100},
        {'job_idx': 1, 'submit_time': 10, 'start_time': 100, 'end_time': 130, 'nodes': 20, 'duration': 30},
    ]

def bf04_multiple_backfill_input():
    """
    Input:
    - Job 0: submit=0, nodes=70, duration=100
    - Job 1: submit=10, nodes=10, duration=30
    - Job 2: submit=20, nodes=10, duration=30
    - Job 3: submit=30, nodes=10, duration=30

    Trace:
    t=0: Job 0 → START, free=30
    t=10: Job 1 arrives, fits → START, free=20
    t=20: Job 2 arrives, fits → START, free=10
    t=30: Job 3 arrives, fits → START, free=0
    t=40: Job 1 ends, free=10
    t=50: Job 2 ends, free=20
    t=60: Job 3 ends, free=30
    t=100: Job 0 ends, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 100, 'nodes': 70, 'duration': 100},
        {'job_idx': 1, 'submit_time': 10, 'start_time': 10, 'end_time': 40, 'nodes': 10, 'duration': 30},
        {'job_idx': 2, 'submit_time': 20, 'start_time': 20, 'end_time': 50, 'nodes': 10, 'duration': 30},
        {'job_idx': 3, 'submit_time': 30, 'start_time': 30, 'end_time': 60, 'nodes': 10, 'duration': 30},
    ]

def bf05_sequential_backfill_input():
    """
    Input:
    - Job 0: submit=0, nodes=80, duration=100
    - Job 1: submit=10, nodes=15, duration=20
    - Job 2: submit=20, nodes=15, duration=20

    Trace:
    t=0: Job 0 → START, free=20
    t=10: Job 1 arrives, fits (15 ≤ 20) → START, free=5
    t=20: Job 2 arrives, cannot fit (15 > 5)
          Wait queue: [Job 2]
          Job 2 is FCFS head, reservation at t=100
    t=30: Job 1 ends, free=20
          Job 2 fits → START, free=5
    t=50: Job 2 ends, free=20
    t=100: Job 0 ends, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 100, 'nodes': 80, 'duration': 100},
        {'job_idx': 1, 'submit_time': 10, 'start_time': 10, 'end_time': 30, 'nodes': 15, 'duration': 20},
        {'job_idx': 2, 'submit_time': 20, 'start_time': 30, 'end_time': 50, 'nodes': 15, 'duration': 20},
    ]

def bf06_exact_timing_input():
    """
    Input:
    - Job 0: submit=0, nodes=80, duration=50
    - Job 1: submit=0, nodes=60, duration=30
    - Job 2: submit=10, nodes=15, duration=40

    Trace:
    t=0: Jobs 0,1 arrive
         FCFS: [Job 0, Job 1]
         Job 0 fits → START, free=20
         Job 1 cannot fit
         Reservation: Job 0 ends at 50 → res=50
    t=10: Job 2 arrives
          Job 1 is FCFS head, res=50
          Backfill Job 2: fits (15 ≤ 20)? Yes
                          completes < 50? 10+40=50 < 50? No (strict <)
          → Blocked
    t=50: Job 0 ends, free=100
          Job 1 fits → START, free=40
          Job 2 fits → START, free=25
    t=80: Job 1 ends, free=85
    t=90: Job 2 ends, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 50, 'nodes': 80, 'duration': 50},
        {'job_idx': 1, 'submit_time': 0, 'start_time': 50, 'end_time': 80, 'nodes': 60, 'duration': 30},
        {'job_idx': 2, 'submit_time': 10, 'start_time': 50, 'end_time': 90, 'nodes': 15, 'duration': 40},
    ]

def bf07_fcfs_not_backfill_input():
    """
    Input:
    - Job 0: submit=0, nodes=40, duration=50
    - Job 1: submit=10, nodes=30, duration=40

    Trace:
    t=0: Job 0 → START, free=60
    t=10: Job 1 arrives, fits (30 ≤ 60) → START, free=30
    t=50: Job 0 ends, free=70
    t=50: Job 1 ends, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 50, 'nodes': 40, 'duration': 50},
        {'job_idx': 1, 'submit_time': 10, 'start_time': 10, 'end_time': 50, 'nodes': 30, 'duration': 40},
    ]

def bf08_backfill_fcfs_delayed_input():
    """
    Input:
    - Job 0: submit=0, nodes=80, duration=50
    - Job 1: submit=0, nodes=60, duration=80
    - Job 2: submit=10, nodes=15, duration=20

    Trace:
    t=0: Jobs 0,1 arrive
         FCFS: [Job 0, Job 1] (tie-break by index)
         Job 0 fits → START, free=20
         Job 1 cannot fit (60 > 20)
         Reservation for Job 1: Job 0 ends at 50 → res=50
    t=10: Job 2 arrives
          Wait queue: [Job 1, Job 2]
          Job 1 is FCFS head, res=50
          Backfill Job 2: fits (15 ≤ 20)? Yes
                          completes < 50? 10+20=30 < 50? Yes → BACKFILL
    t=30: Job 2 ends, free=20
    t=50: Job 0 ends, free=100
          Job 1 fits → START, free=40
    t=130: Job 1 ends, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 50, 'nodes': 80, 'duration': 50},
        {'job_idx': 1, 'submit_time': 0, 'start_time': 50, 'end_time': 130, 'nodes': 60, 'duration': 80},
        {'job_idx': 2, 'submit_time': 10, 'start_time': 10, 'end_time': 30, 'nodes': 15, 'duration': 20},
    ]

def bf09_multiple_fcfs_input():
    """
    Input:
    - Job 0: submit=0, nodes=80, duration=50
    - Job 1: submit=0, nodes=80, duration=50
    - Job 2: submit=10, nodes=10, duration=20

    Trace:
    t=0: Jobs 0,1 arrive
         FCFS: [Job 0, Job 1]
         Job 0 fits → START, free=20
         Job 1 cannot fit (80 > 20)
         Reservation: Job 0 ends at 50 → res=50
    t=10: Job 2 arrives
          Wait queue: [Job 1, Job 2]
          Job 1 is FCFS head, res=50
          Backfill Job 2: fits (10 ≤ 20)? Yes
                          completes < 50? 10+20=30 < 50? Yes → BACKFILL
    t=30: Job 2 ends, free=20
    t=50: Job 0 ends, free=100
          Job 1 fits → START, free=20
    t=100: Job 1 ends, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 50, 'nodes': 80, 'duration': 50},
        {'job_idx': 1, 'submit_time': 0, 'start_time': 50, 'end_time': 100, 'nodes': 80, 'duration': 50},
        {'job_idx': 2, 'submit_time': 10, 'start_time': 10, 'end_time': 30, 'nodes': 10, 'duration': 20},
    ]

def bf10_long_duration_input():
    """
    Input:
    - Job 0: submit=0, nodes=80, duration=50
    - Job 1: submit=0, nodes=60, duration=30
    - Job 2: submit=10, nodes=5, duration=100

    Trace:
    t=0: Jobs 0,1 arrive
         FCFS: [Job 0, Job 1]
         Job 0 fits → START, free=20
         Job 1 cannot fit
         Reservation: Job 0 ends at 50 → res=50
    t=10: Job 2 arrives
          Wait queue: [Job 1, Job 2]
          Job 1 is FCFS head, res=50
          Backfill Job 2: fits (5 ≤ 20)? Yes
                          completes < 50? 10+100=110 < 50? No → BLOCKED
    t=50: Job 0 ends, free=100
          Job 1 fits → START, free=40
          Job 2 fits → START, free=35
    t=80: Job 1 ends, free=95
    t=110: Job 2 ends, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 50, 'nodes': 80, 'duration': 50},
        {'job_idx': 1, 'submit_time': 0, 'start_time': 50, 'end_time': 80, 'nodes': 60, 'duration': 30},
        {'job_idx': 2, 'submit_time': 10, 'start_time': 50, 'end_time': 150, 'nodes': 5, 'duration': 100},
    ]

def easy_5jobs_input():
    """
    Input:
    - Job 0: submit=0, nodes=80, duration=100
    - Job 1: submit=10, nodes=15, duration=20
    - Job 2: submit=20, nodes=60, duration=50
    - Job 3: submit=30, nodes=10, duration=15
    - Job 4: submit=40, nodes=20, duration=30

    Trace:
    t=0: Job 0 → START, free=20
    t=10: Job 1 arrives, fits → START, free=5
    t=20: Job 2 arrives, cannot fit (60 > 5)
          Wait queue: [Job 2]
          Job 2 is FCFS head, reservation at t=100
    t=30: Job 1 ends, free=20
          Job 3 arrives
          Wait queue: [Job 2, Job 3]
          Job 2 is FCFS head, res=100
          Backfill Job 3: fits (10 ≤ 20)? Yes
                          completes < 100? 30+15=45 < 100? Yes → BACKFILL Job 3
          Free after Job 3 starts: 10
    t=40: Job 4 arrives
          Wait queue: [Job 2, Job 4]
          Job 2 is FCFS head, res=100
          Backfill Job 4: fits (20 > 10)? No - waits
    t=45: Job 3 ends, free=20
          Job 2 is FCFS head, res=100
          Backfill Job 4: fits (20 ≤ 20)? Yes
                          completes < 100? 45+30=75 < 100? Yes → BACKFILL Job 4
    t=75: Job 4 ends, free=20
    t=100: Job 0 ends, free=100
           Job 2 fits → START, free=40
    t=150: Job 2 ends, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 100, 'nodes': 80, 'duration': 100},
        {'job_idx': 1, 'submit_time': 10, 'start_time': 10, 'end_time': 30, 'nodes': 15, 'duration': 20},
        {'job_idx': 2, 'submit_time': 20, 'start_time': 100, 'end_time': 150, 'nodes': 60, 'duration': 50},
        {'job_idx': 3, 'submit_time': 30, 'start_time': 30, 'end_time': 45, 'nodes': 10, 'duration': 15},
        {'job_idx': 4, 'submit_time': 40, 'start_time': 45, 'end_time': 75, 'nodes': 20, 'duration': 30},
    ]

def hand_backfill_blocked_input():
    """
    Input:
    - Job 0: submit=0, nodes=80, duration=50
    - Job 1: submit=0, nodes=60, duration=30
    - Job 2: submit=10, nodes=10, duration=30

    Trace:
    t=0: Jobs 0,1 arrive
         FCFS: [Job 0, Job 1]
         Job 0 fits → START, free=20
         Job 1 cannot fit
         Reservation: Job 0 ends at 50 → res=50
    t=10: Job 2 arrives
          Wait queue: [Job 1, Job 2]
          Job 1 is FCFS head, res=50
          Backfill Job 2: fits (10 ≤ 20)? Yes
                          completes < 50? 10+30=40 < 50? Yes → BACKFILL
    t=40: Job 2 ends, free=20
    t=50: Job 0 ends, free=100
          Job 1 fits → START, free=40
    t=80: Job 1 ends, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 50, 'nodes': 80, 'duration': 50},
        {'job_idx': 1, 'submit_time': 0, 'start_time': 50, 'end_time': 80, 'nodes': 60, 'duration': 30},
        {'job_idx': 2, 'submit_time': 10, 'start_time': 10, 'end_time': 40, 'nodes': 10, 'duration': 30},
    ]

def hand_simple_backfill_input():
    """
    Input:
    - Job 0: submit=0, nodes=80, duration=100
    - Job 1: submit=10, nodes=10, duration=20

    Trace:
    t=0: Job 0 → START, free=20
    t=10: Job 1 arrives, fits → START, free=10
    t=30: Job 1 ends, free=20
    t=100: Job 0 ends, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 100, 'nodes': 80, 'duration': 100},
        {'job_idx': 1, 'submit_time': 10, 'start_time': 10, 'end_time': 30, 'nodes': 10, 'duration': 20},
    ]

def idle_gap_input():
    """
    Input:
    - Job 0: submit=0, nodes=20, duration=20
    - Job 1: submit=10, nodes=30, duration=20
    - Job 2: submit=100, nodes=40, duration=30
    - Job 3: submit=110, nodes=50, duration=40

    Trace:
    t=0: Job 0 → START, free=80
    t=10: Job 1 arrives, fits → START, free=50
    t=20: Job 0 ends, free=70
    t=30: Job 1 ends, free=100
    t=100: Job 2 arrives → START, free=60
    t=110: Job 3 arrives, fits → START, free=10
    t=130: Job 2 ends, free=50
    t=150: Job 3 ends, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 20, 'nodes': 20, 'duration': 20},
        {'job_idx': 1, 'submit_time': 10, 'start_time': 10, 'end_time': 30, 'nodes': 30, 'duration': 20},
        {'job_idx': 2, 'submit_time': 100, 'start_time': 100, 'end_time': 130, 'nodes': 40, 'duration': 30},
        {'job_idx': 3, 'submit_time': 110, 'start_time': 110, 'end_time': 150, 'nodes': 50, 'duration': 40},
    ]

def inv01_idle_system_input():
    """
    Input:
    - Job 0: submit=0, nodes=50, duration=30
    - Job 1: submit=100, nodes=40, duration=20
    - Job 2: submit=250, nodes=60, duration=40

    Trace:
    t=0: Job 0 → START, free=50
    t=30: Job 0 ends, free=100
    t=100: Job 1 arrives → START, free=60
    t=120: Job 1 ends, free=100
    t=250: Job 2 arrives → START, free=40
    t=290: Job 2 ends, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 30, 'nodes': 50, 'duration': 30},
        {'job_idx': 1, 'submit_time': 100, 'start_time': 100, 'end_time': 120, 'nodes': 40, 'duration': 20},
        {'job_idx': 2, 'submit_time': 250, 'start_time': 250, 'end_time': 290, 'nodes': 60, 'duration': 40},
    ]

def medium_50jobs_input():
    """
    50 jobs - too complex to trace by hand in this format.
    Will use cross-validation approach.
    """
    return []

def sequential_wait_input():
    """
    Input:
    - Job 0: submit=0, nodes=80, duration=50
    - Job 1: submit=0, nodes=60, duration=40

    Trace:
    t=0: Jobs 0,1 arrive
         FCFS: [Job 0, Job 1]
         Job 0 fits → START, free=20
         Job 1 cannot fit (60 > 20)
         Reservation: Job 0 ends at 50 → res=50
    t=50: Job 0 ends, free=100
          Job 1 fits → START, free=40
    t=90: Job 1 ends, free=100
    """
    return [
        {'job_idx': 0, 'submit_time': 0, 'start_time': 0, 'end_time': 50, 'nodes': 80, 'duration': 50},
        {'job_idx': 1, 'submit_time': 0, 'start_time': 50, 'end_time': 90, 'nodes': 60, 'duration': 40},
    ]

def main():
    print("Generating analytical oracles from hand-traced results...\n")

    # All 20 small tests
    tests = [
        ('backfill_3jobs_input', backfill_3jobs_input),
        ('backfill_blocked_input', backfill_blocked_input),
        ('basic_2jobs_input', basic_2jobs_input),
        ('bf01_basic_success_input', bf01_basic_success_input),
        ('bf02_blocked_time_input', bf02_blocked_time_input),
        ('bf03_blocked_resources_input', bf03_blocked_resources_input),
        ('bf04_multiple_backfill_input', bf04_multiple_backfill_input),
        ('bf05_sequential_backfill_input', bf05_sequential_backfill_input),
        ('bf06_exact_timing_input', bf06_exact_timing_input),
        ('bf07_fcfs_not_backfill_input', bf07_fcfs_not_backfill_input),
        ('bf08_backfill_fcfs_delayed_input', bf08_backfill_fcfs_delayed_input),
        ('bf09_multiple_fcfs_input', bf09_multiple_fcfs_input),
        ('bf10_long_duration_input', bf10_long_duration_input),
        ('easy_5jobs_input', easy_5jobs_input),
        ('hand_backfill_blocked_input', hand_backfill_blocked_input),
        ('hand_simple_backfill_input', hand_simple_backfill_input),
        ('idle_gap_input', idle_gap_input),
        ('inv01_idle_system_input', inv01_idle_system_input),
        ('medium_50jobs_input', medium_50jobs_input),
        ('sequential_wait_input', sequential_wait_input),
    ]

    completed = 0
    skipped = 0
    for name, func in tests:
        jobs = func()
        if jobs:
            write_analytical_oracle(name, jobs)
            completed += 1
        else:
            skipped += 1
            print(f"⊗ {name} - skipped (too large for manual tracing)")

    print(f"\n✓ Generated {completed} analytical oracles")
    if skipped:
        print(f"⊗ Skipped {skipped} (will use cross-validation)")
    print("\nNote: medium_50jobs uses cross-validation (50 jobs too many to hand-trace)")

if __name__ == '__main__':
    main()
