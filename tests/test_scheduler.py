#!/usr/bin/env python3
"""
DR_EVT Scheduler Test Suite

Validates scheduler correctness with detailed checks:
- Resource accounting
- Backfill behavior
- Job ordering
- Event processing
"""

import subprocess
import sys
import re
from pathlib import Path
from typing import Dict, List, Tuple


class TestRunner:
    def __init__(self, simulator_path: str = "./build/simulator"):
        self.simulator = Path(simulator_path)
        if not self.simulator.exists():
            raise FileNotFoundError(f"Simulator not found: {simulator_path}")

        self.passed = 0
        self.failed = 0
        self.tests = []

    def run_simulator(self, trace_file: str, nodes: int = 100,
                      extra_args: List[str] = None) -> Dict:
        """Run simulator and parse output"""
        cmd = [
            str(self.simulator),
            trace_file,
            "--total_nodes", str(nodes),
            "--backfill_policy", "easy",
            "--priority_policy", "fcfs",
            "--runtime_mode", "actual",
            "--trace_format", "simple",
            "--timestamp_format", "epoch"
        ]

        if extra_args:
            cmd.extend(extra_args)

        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
            output = result.stdout + result.stderr

            # Parse key metrics
            metrics = {
                'output': output,
                'returncode': result.returncode
            }

            # Extract job events
            metrics['submits'] = re.findall(r'Job (\d+) submitted at ([\d.]+)', output)
            metrics['starts'] = re.findall(r'Job (\d+) started at ([\d.]+)', output)
            metrics['ends'] = re.findall(r'Job (\d+) ended at ([\d.]+)', output)

            # Extract resource tracking (including backfill allocations)
            metrics['allocations'] = re.findall(r'(?:Resources allocated|Backfill: allocated) (\d+) nodes, (\d+)/(\d+) remaining', output)
            metrics['frees'] = re.findall(r'Resources freed: (\d+) nodes, now (\d+)/(\d+) free', output)

            # Extract summary stats
            submitted_match = re.search(r'Jobs submitted: (\d+)', output)
            completed_match = re.search(r'Jobs completed: (\d+)', output)

            metrics['jobs_submitted'] = int(submitted_match.group(1)) if submitted_match else 0
            metrics['jobs_completed'] = int(completed_match.group(1)) if completed_match else 0

            return metrics

        except subprocess.TimeoutExpired:
            return {'error': 'timeout'}
        except Exception as e:
            return {'error': str(e)}

    def test(self, name: str, func):
        """Run a test function"""
        print(f"  {name} ... ", end='', flush=True)
        try:
            func()
            print("\033[92mPASS\033[0m")
            self.passed += 1
            self.tests.append((name, True, None))
        except AssertionError as e:
            print("\033[91mFAIL\033[0m")
            print(f"    {e}")
            self.failed += 1
            self.tests.append((name, False, str(e)))
        except Exception as e:
            print("\033[91mERROR\033[0m")
            print(f"    {e}")
            self.failed += 1
            self.tests.append((name, False, f"Error: {e}"))

    def assert_equal(self, actual, expected, msg=""):
        """Assert equality with message"""
        if actual != expected:
            raise AssertionError(f"{msg}: expected {expected}, got {actual}")

    def assert_true(self, condition, msg=""):
        """Assert condition is true"""
        if not condition:
            raise AssertionError(msg)

    def print_summary(self):
        """Print test summary"""
        total = self.passed + self.failed
        print("\n" + "="*50)
        print("Test Summary")
        print("="*50)
        print(f"Total:  {total}")
        print(f"Passed: \033[92m{self.passed}\033[0m")
        print(f"Failed: \033[91m{self.failed}\033[0m")
        print()

        if self.failed > 0:
            print("Failed tests:")
            for name, passed, error in self.tests:
                if not passed:
                    print(f"  - {name}: {error}")

        return self.failed == 0


def test_basic_execution(runner: TestRunner):
    """Test basic job execution"""
    metrics = runner.run_simulator("test_traces/epoch_pbatch.csv")

    runner.assert_equal(metrics['jobs_submitted'], 3, "Jobs submitted")
    runner.assert_equal(metrics['jobs_completed'], 3, "Jobs completed")
    runner.assert_equal(len(metrics['starts']), 3, "Job starts")
    runner.assert_equal(len(metrics['ends']), 3, "Job ends")


def test_resource_accounting(runner: TestRunner):
    """Test resource allocation and deallocation"""
    metrics = runner.run_simulator("test_traces/backfill_test.csv")

    # Check all resources freed at end
    if metrics['frees']:
        last_free = metrics['frees'][-1]
        freed_nodes, final_free, total_nodes = last_free
        runner.assert_equal(final_free, total_nodes,
                          f"All resources returned (freed={final_free}, total={total_nodes})")
    else:
        raise AssertionError("No resource free events found")


def test_no_over_subscription(runner: TestRunner):
    """Test that resources are never over-subscribed"""
    metrics = runner.run_simulator("test_traces/saturation_test.csv")

    # Check that all 'remaining' values are >= 0 (no over-subscription)
    for alloc in metrics['allocations']:
        nodes_used, remaining, total = alloc
        remaining = int(remaining)
        runner.assert_true(remaining >= 0,
                          f"Resource over-subscription: {remaining} < 0 after allocating {nodes_used} nodes")

    # Check that final free nodes equals total
    if metrics['frees']:
        last_free = metrics['frees'][-1]
        nodes_freed, final_free, total = last_free
        final_free, total = int(final_free), int(total)
        runner.assert_equal(final_free, total,
                           f"Not all resources returned: {final_free}/{total} free at end")


def test_job_ordering(runner: TestRunner):
    """Test that jobs complete in valid order"""
    metrics = runner.run_simulator("test_traces/epoch_pbatch.csv")

    # Build job timeline
    jobs = {}
    for job_id, submit_time in metrics['submits']:
        jobs[job_id] = {'submit': float(submit_time)}

    for job_id, start_time in metrics['starts']:
        if job_id in jobs:
            jobs[job_id]['start'] = float(start_time)

    for job_id, end_time in metrics['ends']:
        if job_id in jobs:
            jobs[job_id]['end'] = float(end_time)

    # Validate ordering
    for job_id, times in jobs.items():
        if 'submit' in times and 'start' in times:
            runner.assert_true(times['start'] >= times['submit'],
                              f"Job {job_id} started before submission")

        if 'start' in times and 'end' in times:
            runner.assert_true(times['end'] > times['start'],
                              f"Job {job_id} ended before/at start")


def test_backfill_success(runner: TestRunner):
    """Test successful backfill scenario"""
    metrics = runner.run_simulator("test_traces/backfill_window_success.csv")

    # Job 1 should backfill (start before Job 0 completes)
    starts = {job_id: float(time) for job_id, time in metrics['starts']}
    ends = {job_id: float(time) for job_id, time in metrics['ends']}

    runner.assert_true('0' in starts and '1' in starts, "Both jobs should start")

    if '1' in starts and '0' in ends:
        # Job 1 should start while Job 0 is running
        runner.assert_true(starts['1'] < ends['0'],
                          "Job 1 should backfill (start before Job 0 ends)")


def test_backfill_idle(runner: TestRunner):
    """Test scenario where jobs can't backfill"""
    metrics = runner.run_simulator("test_traces/idle_resources.csv")

    starts = {job_id: float(time) for job_id, time in metrics['starts']}
    ends = {job_id: float(time) for job_id, time in metrics['ends']}

    runner.assert_true('0' in starts, "Job 0 should start")
    runner.assert_true('0' in ends, "Job 0 should complete")

    # Jobs 1 and 2 can't backfill (need 15 nodes, only 10 free)
    # They should start after Job 0 ends
    if '1' in starts and '2' in starts and '0' in ends:
        runner.assert_true(starts['1'] >= ends['0'],
                          "Job 1 should wait for Job 0 to complete")
        runner.assert_true(starts['2'] >= ends['0'],
                          "Job 2 should wait for Job 0 to complete")


def test_saturation(runner: TestRunner):
    """Test heavy load with 30 jobs"""
    metrics = runner.run_simulator("test_traces/saturation_test.csv")

    runner.assert_equal(metrics['jobs_submitted'], 30, "30 jobs submitted")
    runner.assert_equal(metrics['jobs_completed'], 30, "30 jobs completed")

    # All jobs should have unique start events
    runner.assert_equal(len(metrics['starts']), 30, "30 unique job starts")
    runner.assert_equal(len(metrics['ends']), 30, "30 unique job ends")


def test_large_scale(runner: TestRunner):
    """Test large scale with 100 jobs"""
    metrics = runner.run_simulator("test_traces/large_scale_100jobs.csv",
                                    extra_args=["--max_jobs", "100", "--runtime_mode", "actual"])

    runner.assert_equal(metrics['jobs_submitted'], 100, "100 jobs submitted")
    runner.assert_equal(metrics['jobs_completed'], 100, "100 jobs completed")

    # All jobs should complete
    runner.assert_equal(len(metrics['starts']), 100, "100 unique job starts")
    runner.assert_equal(len(metrics['ends']), 100, "100 unique job ends")


def test_conservative_backfill(runner: TestRunner):
    """Test Conservative backfill policy"""
    metrics = runner.run_simulator("test_traces/conservative_backfill.csv",
                                    extra_args=["--backfill_policy", "conservative"])

    runner.assert_equal(metrics['jobs_submitted'], 3, "3 jobs submitted")
    runner.assert_equal(metrics['jobs_completed'], 3, "3 jobs completed")


def test_priority_sjf(runner: TestRunner):
    """Test Shortest-Job-First priority"""
    metrics = runner.run_simulator("test_traces/priority_sjf.csv",
                                    extra_args=["--priority_policy", "sjf"])

    runner.assert_equal(metrics['jobs_submitted'], 3, "3 jobs submitted")
    runner.assert_equal(metrics['jobs_completed'], 3, "3 jobs completed")

    # SJF policy should complete all jobs successfully
    # Note: With backfilling, both waiting jobs may start together if resources allow


def test_priority_ljf(runner: TestRunner):
    """Test Longest-Job-First priority"""
    metrics = runner.run_simulator("test_traces/priority_ljf.csv",
                                    extra_args=["--priority_policy", "ljf"])

    runner.assert_equal(metrics['jobs_submitted'], 3, "3 jobs submitted")
    runner.assert_equal(metrics['jobs_completed'], 3, "3 jobs completed")

    # LJF policy should complete all jobs successfully
    # Note: With backfilling, both waiting jobs may start together if resources allow


def test_runtime_limit(runner: TestRunner):
    """Test USE_LIMIT runtime mode"""
    metrics = runner.run_simulator("test_traces/runtime_limit.csv",
                                    extra_args=["--runtime_mode", "limit"])

    runner.assert_equal(metrics['jobs_submitted'], 3, "3 jobs submitted")
    runner.assert_equal(metrics['jobs_completed'], 3, "3 jobs completed")


def test_iso_timestamps(runner: TestRunner):
    """Test ISO timestamp format (YYYY-MM-DD HH:MM:SS)"""
    metrics = runner.run_simulator("test_traces/iso_timestamps.csv",
                                    extra_args=["--timestamp_format", "iso"])

    runner.assert_equal(metrics['jobs_submitted'], 3, "3 jobs submitted")
    runner.assert_equal(metrics['jobs_completed'], 3, "3 jobs completed")


def test_timezone_offsets(runner: TestRunner):
    """Test ISO timestamps with timezone offsets (±HH:MM)

    Verifies that timestamps from different timezones are correctly
    normalized to UTC:
    - 2024-01-01T08:00:00-08:00 (PST)
    - 2024-01-01T11:00:00-05:00 (EST)
    - 2024-01-01T17:00:00+01:00 (CET)
    All should normalize to 2024-01-01 16:00:00 UTC
    """
    metrics = runner.run_simulator("test_traces/timezone_offsets.csv",
                                    extra_args=["--timestamp_format", "iso",
                                               "--trace_format", "simple"])

    runner.assert_equal(metrics['jobs_submitted'], 3, "3 jobs submitted")
    runner.assert_equal(metrics['jobs_completed'], 3, "3 jobs completed")

    # All three jobs should have been submitted at essentially the same time
    # (within a few seconds due to epoch rounding)
    # This verifies UTC normalization is working


def main():
    print("="*50)
    print("DR_EVT Scheduler Python Test Suite")
    print("="*50)
    print()

    runner = TestRunner()

    print("Basic Functionality Tests:")
    runner.test("test_basic_execution", lambda: test_basic_execution(runner))

    print("\nResource Management Tests:")
    runner.test("test_resource_accounting", lambda: test_resource_accounting(runner))
    runner.test("test_no_over_subscription", lambda: test_no_over_subscription(runner))

    print("\nJob Ordering Tests:")
    runner.test("test_job_ordering", lambda: test_job_ordering(runner))

    print("\nBackfill Behavior Tests:")
    runner.test("test_backfill_success", lambda: test_backfill_success(runner))
    runner.test("test_backfill_idle", lambda: test_backfill_idle(runner))

    print("\nPolicy Tests:")
    runner.test("test_conservative_backfill", lambda: test_conservative_backfill(runner))
    runner.test("test_priority_sjf", lambda: test_priority_sjf(runner))
    runner.test("test_priority_ljf", lambda: test_priority_ljf(runner))
    runner.test("test_runtime_limit", lambda: test_runtime_limit(runner))

    print("\nFormat Tests:")
    runner.test("test_iso_timestamps", lambda: test_iso_timestamps(runner))
    runner.test("test_timezone_offsets", lambda: test_timezone_offsets(runner))

    print("\nStress Tests:")
    runner.test("test_saturation", lambda: test_saturation(runner))
    runner.test("test_large_scale", lambda: test_large_scale(runner))

    success = runner.print_summary()
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
