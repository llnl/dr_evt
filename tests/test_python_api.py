#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

"""
DR_EVT Python API Test Suite

Tests all Python bindings including:
- Configuration parameters
- Streaming API
- Monitoring API
- Statistics
- Different scheduling policies

Note: none of the SimParams configurations below set duration_mode, so it
stays at its own default, "limit", throughout this file. That matters
because duration_mode="actual" would make the scheduler ignore
run_time_mode entirely and just use the trace's own real run time -
"limit" is what makes run_time_mode=EXACT actually get used.
"""

import sys
import os
import tempfile

# Add build directory to Python path (for CI/testing without install)
build_path = os.path.join(os.path.dirname(__file__), '..', 'build')
if os.path.exists(build_path):
    sys.path.insert(0, build_path)

try:
    import dr_evt
except ImportError as e:
    print(f"✗ Failed to import dr_evt module: {e}", file=sys.stderr)
    print("\nBuild Python bindings with:", file=sys.stderr)
    print("  cd build && cmake .. -DDR_EVT_BUILD_PYTHON=ON && make", file=sys.stderr)
    sys.exit(1)


class TestResult:
    """Track test results"""
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.errors = []

    def record_pass(self, test_name):
        self.passed += 1
        print(f"  ✓ {test_name}")

    def record_fail(self, test_name, error):
        self.failed += 1
        self.errors.append((test_name, error))
        print(f"  ✗ {test_name}: {error}")

    def summary(self):
        total = self.passed + self.failed
        print(f"\n{'='*60}")
        print(f"Test Results: {self.passed}/{total} passed")
        print(f"{'='*60}")

        if self.errors:
            print("\nFailed tests:")
            for name, error in self.errors:
                print(f"  - {name}: {error}")
            return 1
        else:
            print("\n✅ ALL PYTHON API TESTS PASSED!")
            return 0


def create_test_trace(filename, jobs):
    """Create a test trace file"""
    with open(filename, 'w') as f:
        f.write("job_submit_time,num_nodes,exit_status,queue,time_limit\n")
        for job in jobs:
            f.write(f"{job[0]},{job[1]},0,pbatch,{job[2]}\n")


def test_module_import(result):
    """Test 1: Module import and version"""
    print("\n1. Module Import")
    try:
        assert hasattr(dr_evt, '__version__')
        result.record_pass(f"Version: {dr_evt.__version__}")
    except Exception as e:
        result.record_fail("Module version", str(e))


def test_enumerations(result):
    """Test 2: Enumerations"""
    print("\n2. Enumerations")

    # BackfillPolicy
    try:
        assert hasattr(dr_evt, 'BackfillPolicy')
        assert hasattr(dr_evt.BackfillPolicy, 'NONE')
        assert hasattr(dr_evt.BackfillPolicy, 'EASY')
        assert hasattr(dr_evt.BackfillPolicy, 'CONSERVATIVE')
        result.record_pass("BackfillPolicy")
    except Exception as e:
        result.record_fail("BackfillPolicy", str(e))

    # PriorityPolicy
    try:
        assert hasattr(dr_evt, 'PriorityPolicy')
        assert hasattr(dr_evt.PriorityPolicy, 'FCFS')
        assert hasattr(dr_evt.PriorityPolicy, 'SJF')
        assert hasattr(dr_evt.PriorityPolicy, 'LJF')
        result.record_pass("PriorityPolicy")
    except Exception as e:
        result.record_fail("PriorityPolicy", str(e))

    # RunTimeMode
    try:
        assert hasattr(dr_evt, 'RunTimeMode')
        assert hasattr(dr_evt.RunTimeMode, 'FROM_COLUMN')
        assert hasattr(dr_evt.RunTimeMode, 'EXACT')
        assert hasattr(dr_evt.RunTimeMode, 'DISTRIBUTION')
        result.record_pass("RunTimeMode")
    except Exception as e:
        result.record_fail("RunTimeMode", str(e))


def test_sim_params(result):
    """Test 3: SimParams configuration"""
    print("\n3. SimParams Configuration")

    try:
        params = dr_evt.SimParams()

        # Test all exposed parameters
        params.infile = "test.csv"
        params.total_nodes = 100
        params.trace_format = "simple"
        params.timestamp_format = "epoch"
        params.run_time_mode = dr_evt.RunTimeMode.EXACT
        params.backfill_policy = dr_evt.BackfillPolicy.EASY
        params.priority_policy = dr_evt.PriorityPolicy.FCFS
        params.verbose = False

        result.record_pass("SimParams creation and configuration")
    except Exception as e:
        result.record_fail("SimParams", str(e))


def test_streaming_api(result):
    """Test 4: Streaming API"""
    print("\n4. Streaming API")

    trace_file = tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False)
    trace_file.close()

    try:
        # Create test trace
        create_test_trace(trace_file.name, [
            (0, 10, 100),    # Job 0: t=0, 10 nodes, 100s
            (50, 20, 100),   # Job 1: t=50, 20 nodes, 100s
        ])

        # Configure
        params = dr_evt.SimParams()
        params.infile = trace_file.name
        params.total_nodes = 100
        params.trace_format = "simple"
        params.timestamp_format = "epoch"
        params.run_time_mode = dr_evt.RunTimeMode.EXACT
        params.backfill_policy = dr_evt.BackfillPolicy.EASY
        params.priority_policy = dr_evt.PriorityPolicy.FCFS

        # Create simulation
        sim = dr_evt.Simulation(params)
        num_jobs = sim.initialize_trace()
        assert num_jobs == 2, f"Expected 2 jobs, got {num_jobs}"

        # Test submit_job
        sim.submit_job(0, 0.0)
        sim.advance_to(0.0)
        assert sim.get_nodes_in_use() == 10
        result.record_pass("submit_job and advance_to")

        # Test run_until_exclusive
        sim.submit_job(1, 50.0)
        sim.run_until_exclusive(50.0)
        # Job 1 must NOT have started yet - the event at exactly the
        # target time is excluded by run_until_exclusive.
        assert sim.get_nodes_in_use() == 10, \
            f"run_until_exclusive(50.0) should not process the t=50 event yet, but nodes_in_use={sim.get_nodes_in_use()}"
        sim.advance_to(50.0)
        assert sim.get_nodes_in_use() == 30
        result.record_pass("run_until_exclusive")

    except Exception as e:
        result.record_fail("Streaming API", str(e))
    finally:
        os.unlink(trace_file.name)


def test_monitoring_api(result):
    """Test 5: Monitoring API"""
    print("\n5. Monitoring API")

    trace_file = tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False)
    trace_file.close()

    try:
        create_test_trace(trace_file.name, [(0, 30, 100)])

        params = dr_evt.SimParams()
        params.infile = trace_file.name
        params.total_nodes = 100
        params.trace_format = "simple"
        params.timestamp_format = "epoch"
        params.run_time_mode = dr_evt.RunTimeMode.EXACT

        sim = dr_evt.Simulation(params)
        sim.initialize_trace()

        # Initial state
        assert sim.get_current_time() == 0.0
        assert sim.get_nodes_in_use() == 0
        assert sim.get_available_nodes() == 100
        result.record_pass("Initial state monitoring")

        # After job starts
        sim.submit_job(0, 0.0)
        sim.advance_to(0.0)
        assert sim.get_nodes_in_use() == 30
        assert sim.get_available_nodes() == 70
        result.record_pass("Active state monitoring")

        # Queue status
        queue_size = sim.get_active_job_count()
        shadow_time = sim.get_fcfs_head_shadow_time()
        result.record_pass("Queue status API")

    except Exception as e:
        result.record_fail("Monitoring API", str(e))
    finally:
        os.unlink(trace_file.name)


def test_statistics(result):
    """Test 6: Statistics"""
    print("\n6. Statistics API")

    trace_file = tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False)
    trace_file.close()

    try:
        create_test_trace(trace_file.name, [
            (0, 10, 50),
            (10, 20, 50),
            (20, 30, 50),
        ])

        params = dr_evt.SimParams()
        params.infile = trace_file.name
        params.total_nodes = 100
        params.trace_format = "simple"
        params.timestamp_format = "epoch"
        params.run_time_mode = dr_evt.RunTimeMode.EXACT

        sim = dr_evt.Simulation(params)
        sim.initialize_trace()

        # Run complete simulation
        sim.advance_to(0.0)
        sim.submit_job(0, 0.0)
        sim.advance_to(0.0)

        sim.submit_job(1, 10.0)
        sim.advance_to(10.0)

        sim.submit_job(2, 20.0)
        sim.advance_to(20.0)

        sim.advance_to(100.0)

        # Get statistics
        stats = sim.get_statistics()

        # Check all fields exist
        assert hasattr(stats, 'jobs_submitted')
        assert hasattr(stats, 'jobs_completed')
        assert hasattr(stats, 'jobs_running')
        assert hasattr(stats, 'jobs_waiting')
        assert hasattr(stats, 'current_time')
        assert hasattr(stats, 'total_nodes')
        assert hasattr(stats, 'nodes_in_use')
        assert hasattr(stats, 'nodes_available')
        assert hasattr(stats, 'utilization')
        assert hasattr(stats, 'avg_wait_time')
        assert hasattr(stats, 'avg_turnaround_time')
        assert hasattr(stats, 'makespan')

        # Check values make sense
        assert stats.jobs_completed == 3
        assert stats.total_nodes == 100
        assert 0.0 <= stats.utilization <= 1.0

        result.record_pass("Statistics fields and values")

    except Exception as e:
        result.record_fail("Statistics", str(e))
    finally:
        os.unlink(trace_file.name)


def test_backfill_policies(result):
    """Test 7: Different backfill policies"""
    print("\n7. Backfill Policies")

    trace_file = tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False)
    trace_file.close()

    try:
        create_test_trace(trace_file.name, [(0, 50, 100), (10, 30, 50)])

        for policy in [dr_evt.BackfillPolicy.NONE,
                       dr_evt.BackfillPolicy.EASY,
                       dr_evt.BackfillPolicy.CONSERVATIVE]:
            params = dr_evt.SimParams()
            params.infile = trace_file.name
            params.total_nodes = 100
            params.trace_format = "simple"
            params.timestamp_format = "epoch"
            params.run_time_mode = dr_evt.RunTimeMode.EXACT
            params.backfill_policy = policy

            sim = dr_evt.Simulation(params)
            sim.initialize_trace()
            sim.submit_job(0, 0.0)
            sim.advance_to(0.0)
            sim.submit_job(1, 10.0)
            sim.advance_to(200.0)

            stats = sim.get_statistics()
            assert stats.jobs_completed == 2

        result.record_pass("NONE, EASY, CONSERVATIVE policies")

    except Exception as e:
        result.record_fail("Backfill policies", str(e))
    finally:
        os.unlink(trace_file.name)


def test_priority_policies(result):
    """Test 8: Different priority policies"""
    print("\n8. Priority Policies")

    trace_file = tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False)
    trace_file.close()

    try:
        create_test_trace(trace_file.name, [
            (0, 10, 100),   # Long job
            (5, 10, 20),    # Short job
            (10, 10, 50),   # Medium job
        ])

        for policy in [dr_evt.PriorityPolicy.FCFS,
                       dr_evt.PriorityPolicy.SJF,
                       dr_evt.PriorityPolicy.LJF]:
            params = dr_evt.SimParams()
            params.infile = trace_file.name
            params.total_nodes = 100
            params.trace_format = "simple"
            params.timestamp_format = "epoch"
            params.run_time_mode = dr_evt.RunTimeMode.EXACT
            params.priority_policy = policy

            sim = dr_evt.Simulation(params)
            sim.initialize_trace()

            sim.submit_job(0, 0.0)
            sim.advance_to(0.0)
            sim.submit_job(1, 5.0)
            sim.advance_to(5.0)
            sim.submit_job(2, 10.0)
            sim.advance_to(200.0)

            stats = sim.get_statistics()
            assert stats.jobs_completed == 3

        result.record_pass("FCFS, SJF, LJF policies")

    except Exception as e:
        result.record_fail("Priority policies", str(e))
    finally:
        os.unlink(trace_file.name)


def test_batch_mode(result):
    """Test 9: Batch mode API"""
    print("\n9. Batch Mode")

    trace_file = tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False)
    trace_file.close()

    try:
        create_test_trace(trace_file.name, [(0, 10, 50), (10, 20, 50)])

        params = dr_evt.SimParams()
        params.infile = trace_file.name
        params.total_nodes = 100
        params.trace_format = "simple"
        params.timestamp_format = "epoch"
        params.run_time_mode = dr_evt.RunTimeMode.EXACT

        sim = dr_evt.Simulation(params)
        sim.initialize_trace()

        # Run entire simulation at once
        sim.run()

        stats = sim.get_statistics()
        assert stats.jobs_completed == 2
        assert stats.nodes_in_use == 0, \
            f"All jobs completed but nodes_in_use={stats.nodes_in_use}, expected 0"

        result.record_pass("Batch mode run()")

    except Exception as e:
        result.record_fail("Batch mode", str(e))
    finally:
        os.unlink(trace_file.name)


def main():
    print("="*60)
    print("DR_EVT Python API Test Suite")
    print("="*60)

    result = TestResult()

    # Run all tests
    test_module_import(result)
    test_enumerations(result)
    test_sim_params(result)
    test_streaming_api(result)
    test_monitoring_api(result)
    test_statistics(result)
    test_backfill_policies(result)
    test_priority_policies(result)
    test_batch_mode(result)

    # Print summary and exit
    return result.summary()


if __name__ == '__main__':
    sys.exit(main())
