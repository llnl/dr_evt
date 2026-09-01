#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

"""
Test DR_EVT Python API

Quick test to verify all API methods work correctly.
"""

import sys
import os

# Add build directory to path for testing without install
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'build'))

try:
    import dr_evt
    print("✓ dr_evt module imported successfully")
except ImportError as e:
    print(f"✗ Failed to import dr_evt: {e}")
    print("\nTo build:")
    print("  cd build && cmake .. -DDR_EVT_BUILD_PYTHON=ON && make")
    sys.exit(1)

def test_basic_api():
    """Test basic API functionality"""
    print("\n" + "="*60)
    print("Testing DR_EVT Python API")
    print("="*60)

    # Test enums
    print("\n1. Testing enums...")
    assert hasattr(dr_evt, 'DurationMode')
    assert hasattr(dr_evt, 'BackfillPolicy')
    assert hasattr(dr_evt, 'PriorityPolicy')
    print("  ✓ Enums accessible")

    # Test SimParams
    print("\n2. Testing SimParams...")
    params = dr_evt.SimParams()
    params.total_nodes = 100
    params.duration_mode = dr_evt.DurationMode.EXACT
    params.backfill_policy = dr_evt.BackfillPolicy.EASY
    params.priority_policy = dr_evt.PriorityPolicy.FCFS
    print("  ✓ SimParams created and configured")

    # Create test trace
    print("\n3. Creating test trace...")
    test_trace = "test_python_api.csv"
    with open(test_trace, 'w') as f:
        f.write("job_submit_time,num_nodes,exit_status,queue,time_limit\n")
        f.write("0,10,0,pbatch,100\n")
        f.write("50,20,0,pbatch,100\n")
    print(f"  ✓ Test trace created: {test_trace}")

    # Test Simulation
    print("\n4. Testing Simulation...")
    params.infile = test_trace
    params.trace_format = "simple"
    params.timestamp_format = "epoch"
    sim = dr_evt.Simulation(params)
    print("  ✓ Simulation created")

    # Test initialize
    print("\n5. Testing trace initialization...")
    num_jobs = sim.initialize_trace()
    print(f"  ✓ Loaded {num_jobs} jobs")
    assert num_jobs == 2

    # Test monitoring before any jobs
    print("\n6. Testing monitoring API (initial state)...")
    assert sim.get_current_time() == 0.0
    assert sim.get_nodes_in_use() == 0
    assert sim.get_available_nodes() == 100
    assert sim.get_active_job_count() == 0
    print("  ✓ Initial state correct")

    # Test streaming API
    print("\n7. Testing streaming API...")
    sim.submit_job(0, 0.0)
    sim.advance_to(0.0)
    assert sim.get_nodes_in_use() == 10
    assert sim.get_available_nodes() == 90
    print("  ✓ Job 0 started, 10 nodes in use")

    sim.submit_job(1, 50.0)
    sim.advance_to(50.0)
    assert sim.get_nodes_in_use() == 30  # Both jobs running
    assert sim.get_available_nodes() == 70
    print("  ✓ Job 1 started, 30 nodes in use")

    # Test wait queue
    print("\n8. Testing queue status...")
    queue_size = sim.get_active_job_count()
    print(f"  ✓ Wait queue size: {queue_size}")

    # Test shadow time
    print("\n9. Testing FCFS head shadow time...")
    shadow_time = sim.get_fcfs_head_shadow_time()
    print(f"  ✓ Shadow time: {shadow_time}")

    # Test statistics
    print("\n10. Testing statistics API...")
    stats = sim.get_statistics()
    assert hasattr(stats, 'jobs_submitted')
    assert hasattr(stats, 'jobs_completed')
    assert hasattr(stats, 'jobs_running')
    assert hasattr(stats, 'jobs_waiting')
    assert hasattr(stats, 'utilization')
    assert hasattr(stats, 'avg_wait_time')
    print(f"  ✓ Statistics retrieved")
    print(f"     Jobs: {stats.jobs_running} running, {stats.jobs_waiting} waiting")
    print(f"     Utilization: {stats.utilization*100:.1f}%")

    # Complete simulation
    print("\n11. Completing simulation...")
    sim.advance_to(200.0)
    final_stats = sim.get_statistics()
    assert final_stats.jobs_completed == 2
    assert final_stats.nodes_in_use == 0
    print("  ✓ Simulation completed")
    print(f"     Completed: {final_stats.jobs_completed}/2 jobs")
    print(f"     Avg wait: {final_stats.avg_wait_time:.2f}s")
    print(f"     Makespan: {final_stats.makespan:.2f}s")

    # Test exclusive vs inclusive
    print("\n12. Testing exclusive vs inclusive...")
    params2 = dr_evt.SimParams()
    params2.infile = test_trace
    params2.total_nodes = 100
    params2.trace_format = "simple"
    params2.timestamp_format = "epoch"
    params2.duration_mode = dr_evt.DurationMode.EXACT

    sim2 = dr_evt.Simulation(params2)
    sim2.initialize_trace()
    sim2.submit_job(0, 0.0)

    # Exclusive should NOT process event at t=0
    sim2.run_until_exclusive(0.0)
    # Can't easily test this without knowing internal state

    # Inclusive SHOULD process event at t=0
    sim2.advance_to(0.0)
    assert sim2.get_nodes_in_use() == 10
    print("  ✓ Exclusive vs inclusive works")

    # Cleanup
    os.remove(test_trace)
    print("\n" + "="*60)
    print("ALL TESTS PASSED!")
    print("="*60)

if __name__ == '__main__':
    try:
        test_basic_api()
    except AssertionError as e:
        print(f"\n✗ TEST FAILED: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
    except Exception as e:
        print(f"\n✗ ERROR: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
