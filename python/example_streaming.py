#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

"""
Example: DR_EVT Streaming API usage

Demonstrates how to:
1. Read job requests from a CSV file
2. Append jobs incrementally
3. Monitor resource usage
4. Get queue and scheduling statistics
"""

import csv
import os
import dr_evt

def main():
    # Configure simulation
    params = dr_evt.SimParams()
    # Resolve relative to this script's own location, not the caller's
    # cwd - a bare "examples/sample_trace.csv" only resolves correctly
    # if invoked as `python example_streaming.py` from within python/
    # itself; `python3 python/example_streaming.py` from the repo root
    # (an equally natural way to run it - and how CI's own test now
    # invokes it) would otherwise fail with "Failed to initialize data
    # columns" since that path doesn't exist relative to the repo root.
    params.infile = os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "examples", "sample_trace.csv")
    params.total_nodes = 100
    params.trace_format = "simple"
    params.timestamp_format = "epoch"
    params.run_time_mode = dr_evt.RunTimeMode.LIMIT
    params.backfill_policy = dr_evt.BackfillPolicy.EASY
    params.priority_policy = dr_evt.PriorityPolicy.FCFS
    params.verbose = False

    # Read external job requests. append_job() is the public streaming API;
    # initialize_trace() loads a batch trace and is not a source of jobs to
    # re-submit individually.
    with open(params.infile, newline='', encoding='utf-8') as trace:
        jobs = list(csv.DictReader(trace))[:10]

    # Create simulator.
    sim = dr_evt.Simulation(params)
    print(f"Read {len(jobs)} jobs from trace")

    print("\n" + "="*60)
    print("Streaming Simulation with Monitoring")
    print("="*60)

    # Submit and run jobs incrementally
    queue_field = "queue" if dr_evt.legacy_queue_input else "q_id"
    default_queue = "pbatch" if dr_evt.legacy_queue_input else "1"
    for job_idx, job in enumerate(jobs):
        submit_time = float(job["job_submit_time"])
        queue_input = (job.get(queue_field) or default_queue).strip()
        sim.append_job(submit_time, int(job["num_nodes"]), queue_input,
                       float(job["time_limit"]))

        # Advance to submit time
        sim.advance_to(submit_time)

        # Monitor state after each job
        print(f"\nTime {sim.get_current_time():.1f}:")
        print(f"  Job {job_idx} appended")
        print(f"  Nodes in use: {sim.get_nodes_in_use()}/{params.total_nodes}")
        print(f"  Available: {sim.get_available_nodes()}")
        print(f"  Wait queue: {sim.get_active_job_count()} jobs")

        # Get FCFS head shadow time (when head of queue can start)
        shadow_time = sim.get_fcfs_head_shadow_time()
        if shadow_time >= 0:
            print(f"  FCFS head can start at: {shadow_time:.1f}")

        # Get comprehensive statistics
        stats = sim.get_statistics()
        print(f"  Utilization: {stats.utilization*100:.1f}%")
        print(f"  Jobs: {stats.jobs_running} running, {stats.jobs_waiting} waiting")

    # Advance to end of simulation
    print(f"\n{'='*60}")
    print("Advancing to end of simulation...")
    sim.advance_to(10000.0)

    # Final statistics
    print(f"\n{'='*60}")
    print("Final Statistics")
    print("="*60)

    final_stats = sim.get_statistics()
    print(f"Jobs submitted: {final_stats.jobs_submitted}")
    print(f"Jobs completed: {final_stats.jobs_completed}")
    print(f"Average wait time: {final_stats.avg_wait_time:.2f} seconds")
    print(f"Average turnaround: {final_stats.avg_turnaround_time:.2f} seconds")
    print(f"Makespan: {final_stats.makespan:.2f} seconds")
    print(f"Overall utilization: {final_stats.utilization*100:.1f}%")

    print(f"\n{'='*60}")
    print(final_stats)


if __name__ == '__main__':
    main()
