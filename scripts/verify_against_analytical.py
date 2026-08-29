#!/usr/bin/env python3
"""
Verify both DR_EVT and Python reference implementations against analytical oracles.

For 19 small tests: verify against hand-traced analytical oracles
For 1 medium test + 3 large tests: cross-validate implementations match each other
"""

import csv
import subprocess
import sys
from pathlib import Path

def load_csv(filename):
    """Load job trace CSV."""
    jobs = []
    with open(filename) as f:
        reader = csv.DictReader(f)
        for row in reader:
            jobs.append({
                'job_idx': int(row.get('job_idx', row.get('job_submit_time', 0))),
                'start_time': float(row.get('start_time', row.get('begin_time', 0))),
                'end_time': float(row.get('end_time', 0)),
            })
    return jobs

def compare_traces(analytical, implementation, name):
    """Compare two traces, return mismatches."""
    mismatches = []
    for a, i in zip(analytical, implementation):
        if abs(a['start_time'] - i['start_time']) > 0.1 or abs(a['end_time'] - i['end_time']) > 0.1:
            mismatches.append({
                'job_idx': a['job_idx'],
                'analytical_start': a['start_time'],
                'impl_start': i['start_time'],
                'analytical_end': a['end_time'],
                'impl_end': i['end_time'],
            })
    return mismatches

def run_drevt(input_file, output_file):
    """Run DR_EVT simulator."""
    cmd = [
        './build/simulator', input_file,
        '--total_nodes', '100',
        '--trace_format', 'simple',
        '--timestamp_format', 'epoch',
        '--duration_mode', 'exact',
        '--max_jobs', '10000',  # Process all jobs, not just first 10
        '--outfile', output_file
    ]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

def run_python(input_file):
    """Run Python reference implementation."""
    cmd = ['python3', 'scripts/python_reference_scheduler.py', input_file, '--nodes', '100']
    subprocess.run(cmd, stdout=subprocess.DEVNULL, check=True)

def verify_analytical(test_name):
    """Verify test against analytical oracle."""
    base = f"tests/test_traces/correctness/{test_name}"
    input_file = f"{base}.csv"
    analytical_file = f"{base}_analytical.csv"
    drevt_out = f"/tmp/{test_name}_drevt.csv"
    python_out = f"{base}_reference.csv"

    # Check if analytical oracle exists
    if not Path(analytical_file).exists():
        return None  # No analytical oracle, skip

    # Run implementations
    run_drevt(input_file, drevt_out)
    run_python(input_file)

    # Load traces
    analytical = load_csv(analytical_file)
    drevt = load_csv(drevt_out)
    python = load_csv(python_out)

    # Compare
    drevt_mismatches = compare_traces(analytical, drevt, 'DR_EVT')
    python_mismatches = compare_traces(analytical, python, 'Python')

    return {
        'drevt_ok': len(drevt_mismatches) == 0,
        'python_ok': len(python_mismatches) == 0,
        'drevt_mismatches': drevt_mismatches,
        'python_mismatches': python_mismatches,
    }

def cross_validate(test_name):
    """Cross-validate: check DR_EVT and Python produce identical results."""
    base = f"tests/test_traces/correctness/{test_name}"
    input_file = f"{base}.csv"
    drevt_out = f"/tmp/{test_name}_drevt.csv"
    python_out = f"{base}_reference.csv"

    # Run implementations
    run_drevt(input_file, drevt_out)
    run_python(input_file)

    # Load and compare
    drevt = load_csv(drevt_out)
    python = load_csv(python_out)

    mismatches = compare_traces(drevt, python, 'cross')

    return {
        'match': len(mismatches) == 0,
        'mismatches': mismatches,
    }

def main():
    print("="*70)
    print("VERIFICATION AGAINST ANALYTICAL ORACLES")
    print("="*70)
    print()

    # 19 small tests with analytical oracles
    analytical_tests = [
        'backfill_3jobs_input',
        'backfill_blocked_input',
        'basic_2jobs_input',
        'bf01_basic_success_input',
        'bf02_blocked_time_input',
        'bf03_blocked_resources_input',
        'bf04_multiple_backfill_input',
        'bf05_sequential_backfill_input',
        'bf06_exact_timing_input',
        'bf07_fcfs_not_backfill_input',
        'bf08_backfill_fcfs_delayed_input',
        'bf09_multiple_fcfs_input',
        'bf10_long_duration_input',
        'easy_5jobs_input',
        'hand_backfill_blocked_input',
        'hand_simple_backfill_input',
        'idle_gap_input',
        'inv01_idle_system_input',
        'sequential_wait_input',
    ]

    drevt_pass = 0
    python_pass = 0
    drevt_fail = 0
    python_fail = 0

    print("Phase 1: Verify against analytical oracles (19 tests)")
    print("-" * 70)
    for test in analytical_tests:
        result = verify_analytical(test)
        if result is None:
            continue

        drevt_status = "✓" if result['drevt_ok'] else "✗"
        python_status = "✓" if result['python_ok'] else "✗"

        print(f"{test:40s}  DR_EVT:{drevt_status}  Python:{python_status}")

        if result['drevt_ok']:
            drevt_pass += 1
        else:
            drevt_fail += 1
            print(f"  DR_EVT mismatches: {result['drevt_mismatches'][:2]}")

        if result['python_ok']:
            python_pass += 1
        else:
            python_fail += 1
            print(f"  Python mismatches: {result['python_mismatches'][:2]}")

    print()
    print(f"DR_EVT:  {drevt_pass} passed, {drevt_fail} failed")
    print(f"Python:  {python_pass} passed, {python_fail} failed")

    # Cross-validation tests
    print()
    print("="*70)
    print("Phase 2: Cross-validation (4 tests)")
    print("-" * 70)

    cross_val_tests = [
        'medium_50jobs_input',
        'cross_validation_100jobs_input',
        'large_500jobs_input',
        'large_2000jobs_input',
    ]

    cross_pass = 0
    cross_fail = 0

    for test in cross_val_tests:
        result = cross_validate(test)
        status = "✓" if result['match'] else "✗"
        print(f"{test:40s}  {status}")

        if result['match']:
            cross_pass += 1
        else:
            cross_fail += 1
            print(f"  Mismatches: {result['mismatches'][:2]}")

    print()
    print(f"Cross-validation: {cross_pass} passed, {cross_fail} failed")

    # Summary
    print()
    print("="*70)
    print("SUMMARY")
    print("="*70)
    total_tests = drevt_pass + drevt_fail + cross_pass + cross_fail
    total_pass = min(drevt_pass, python_pass) + cross_pass
    print(f"Total: {total_pass}/{total_tests} tests passed")

    if drevt_fail == 0 and python_fail == 0 and cross_fail == 0:
        print("\n✓ ALL TESTS PASSED")
        print("Both implementations verified against analytical oracles!")
        return 0
    else:
        print("\n✗ SOME TESTS FAILED")
        print("Review mismatches above")
        return 1

if __name__ == '__main__':
    sys.exit(main())
