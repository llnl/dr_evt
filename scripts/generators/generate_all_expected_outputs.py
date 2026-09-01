#!/usr/bin/env python3
"""
Generate expected output CSV files for all 27 comprehensive tests.
Uses the verified Python reference implementation (27/27 passing).
"""

import sys
import csv
from pathlib import Path

sys.path.append('../scripts')
from python_reference_scheduler import EasyBackfillingScheduler, Job

TRACE_DIR = Path("../tests/test_traces/comprehensive")
TOTAL_NODES = 100

# All 27 tests in order
TESTS = [
    # Tier 1: FCFS
    "07_simultaneous_submit",
    "10_queue_drain_idle",
    "13_consecutive_fcfs",
    "15_fcfs_partial_overlap",

    # Tier 2: Basic Backfilling
    "01_backfill_allowed",
    "02_backfill_blocked_time",
    "03_backfill_blocked_resources",
    "06_backfill_out_of_order",

    # Tier 3: Competition
    "04_backfill_resource_competition",
    "05_multiple_backfills",
    "14_fcfs_with_backfill",

    # Tier 4: Events
    "08_simultaneous_completion",
    "09_simultaneous_submit_complete",
    "11_multiple_drains",
    "12_drain_with_backlog",

    # Tier 5: Complex
    "24_multiple_running_jobs",
    "19_resource_fragmentation",
    "20_fragmentation_recovery",

    # Tier 6: Properties
    "16_starvation_prevention",
    "17_late_large_priority",
    "18_backfill_no_starvation",
    "21_sustained_high_load",
    "22_bursty_load",
    "23_mixed_load",

    # Tier 7: Early completion
    "25_early_completion_basic",
    "26_early_completion_cascading",
    "27_early_vs_late_completion",
]

def load_trace(filename):
    """Load trace file into Job objects."""
    jobs = []
    with open(filename, 'r') as f:
        reader = csv.DictReader(f)
        for idx, row in enumerate(reader):
            actual_runtime = float(row.get('actual_duration', row['time_limit']))
            jobs.append(Job(
                idx=idx,
                submit_time=float(row['job_submit_time']),
                nodes=int(row['num_nodes']),
                duration=float(row['time_limit']),
                actual_runtime=actual_runtime
            ))
    return jobs

def generate_expected_output(test_id):
    """Generate expected output for a test."""
    input_file = TRACE_DIR / f"{test_id}.csv"
    output_file = TRACE_DIR / f"{test_id}.expected_output.csv"
    resource_file = TRACE_DIR / f"{test_id}.expected_resources.csv"

    if not input_file.exists():
        print(f"✗ {test_id} - Input file not found")
        return False

    try:
        # Load trace
        jobs = load_trace(input_file)

        # Simulate with Python reference
        scheduler = EasyBackfillingScheduler(TOTAL_NODES, verbose=False)
        result = scheduler.simulate(jobs)

        # Write expected job output
        with open(output_file, 'w') as f:
            f.write("job_id,start_time,end_time\n")
            for job in result:
                f.write(f"{job.idx},{int(job.start_time)},{int(job.end_time)}\n")

        # Write expected resource trace
        with open(resource_file, 'w') as f:
            f.write("time,nodes_used,nodes_free,running_jobs\n")
            for time, nodes_used, nodes_free, running_jobs in scheduler.resource_timeline:
                f.write(f"{int(time)},{nodes_used},{nodes_free},{running_jobs}\n")

        print(f"✓ {test_id} - Generated ({len(result)} jobs, {len(scheduler.resource_timeline)} resource events)")
        return True

    except Exception as e:
        print(f"✗ {test_id} - Error: {e}")
        return False

def main():
    """Generate all expected outputs."""
    print("=" * 60)
    print("Generating Expected Outputs from Python Reference")
    print("=" * 60)
    print()

    success = 0
    failed = 0

    for test_id in TESTS:
        if generate_expected_output(test_id):
            success += 1
        else:
            failed += 1

    print()
    print("=" * 60)
    print(f"Generated: {success}/{len(TESTS)}")
    if failed > 0:
        print(f"Failed: {failed}")
    print("=" * 60)

    return 0 if failed == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
