#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

"""
Analyze trace characteristics that affect scheduler performance.

Key metrics:
- Job arrival rate vs duration (queue buildup)
- Maximum concurrent jobs in system
- Wait queue size over time
- Backfilling opportunities
"""

import sys
import csv
from collections import defaultdict
import statistics

def analyze_trace(trace_file, total_nodes=100):
    """Analyze performance characteristics of a job trace."""

    # Read jobs
    jobs = []
    with open(trace_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            jobs.append({
                'submit_time': float(row['job_submit_time']),
                'nodes': int(row['num_nodes']),
                'time_limit': float(row['time_limit'])
            })

    # Sort by submit time
    jobs.sort(key=lambda x: x['submit_time'])

    # Calculate statistics
    submit_times = [j['submit_time'] for j in jobs]
    time_limits = [j['time_limit'] for j in jobs]
    nodes = [j['nodes'] for j in jobs]

    print(f"Trace: {trace_file}")
    print(f"Total jobs: {len(jobs)}")
    print(f"Total nodes: {total_nodes}")
    print()

    print("=== Job Characteristics ===")
    print(f"Time limits:")
    print(f"  Min:    {min(time_limits):.1f}")
    print(f"  Max:    {max(time_limits):.1f}")
    print(f"  Mean:   {statistics.mean(time_limits):.1f}")
    print(f"  Median: {statistics.median(time_limits):.1f}")
    print()

    print(f"Nodes requested:")
    print(f"  Min:    {min(nodes)}")
    print(f"  Max:    {max(nodes)}")
    print(f"  Mean:   {statistics.mean(nodes):.1f}")
    print(f"  Median: {statistics.median(nodes):.1f}")
    print()

    print(f"Submit time span: {min(submit_times):.1f} to {max(submit_times):.1f}")
    print(f"Arrival rate: {len(jobs) / (max(submit_times) - min(submit_times) + 1):.2f} jobs/time")
    print()

    # Simulate to find max queue depth and concurrent jobs
    events = []
    for i, job in enumerate(jobs):
        events.append((job['submit_time'], 'submit', i, job['nodes'], job['time_limit']))

    # Simulate EASY backfilling to estimate complexity
    max_wait_queue = 0
    max_concurrent = 0
    max_system_time = 0

    running = {}  # job_id -> end_time
    wait_queue = []
    current_time = 0
    total_schedule_calls = 0
    total_backfill_iterations = 0

    events.sort()

    for event_time, event_type, job_id, job_nodes, job_limit in events:
        current_time = event_time

        # Remove completed jobs
        completed = [jid for jid, end_time in running.items() if end_time <= current_time]
        for jid in completed:
            del running[jid]

        # Submit new job
        wait_queue.append((job_id, job_nodes, job_limit))

        # Try to schedule (simplified EASY logic)
        free_nodes = total_nodes - sum(jobs[jid]['nodes'] for jid in running)

        # Count how many jobs we'd check for backfilling
        total_schedule_calls += 1
        backfill_checks = 0

        while wait_queue and free_nodes >= wait_queue[0][1]:
            jid, jnodes, jlimit = wait_queue.pop(0)
            running[jid] = current_time + jlimit
            free_nodes -= jnodes
            backfill_checks += 1

        # For remaining jobs in wait queue, estimate backfill iteration cost
        # EASY checks each waiting job against shadow reservation
        if wait_queue:
            backfill_checks += len(wait_queue)

        total_backfill_iterations += backfill_checks

        # Track maximums
        max_wait_queue = max(max_wait_queue, len(wait_queue))
        max_concurrent = max(max_concurrent, len(running))
        if running:
            max_system_time = max(max_system_time, max(running.values()) - current_time)

    print("=== Scheduling Complexity ===")
    print(f"Max wait queue depth: {max_wait_queue}")
    print(f"Max concurrent jobs:  {max_concurrent}")
    print(f"Max job lifetime:     {max_system_time:.1f}")
    print(f"Total schedule() calls: {total_schedule_calls}")
    print(f"Total backfill checks:  {total_backfill_iterations}")
    print(f"Avg checks per call:    {total_backfill_iterations / total_schedule_calls:.1f}")
    print()

    # Estimate O(n²) impact
    print("=== Performance Estimate ===")
    # The O(n²) loop happens when checking if window is too short
    # Lines 133-136: for each job, scan all remaining jobs
    worst_case_iterations = max_wait_queue * max_wait_queue / 2
    print(f"Worst-case O(n²) iterations: {worst_case_iterations:.0f}")
    print(f"  (max_queue_depth² / 2 = {max_wait_queue}² / 2)")
    print()

    if max_wait_queue > 50:
        print("⚠️  WARNING: Large wait queue depth!")
        print(f"   Queue depth {max_wait_queue} means up to {worst_case_iterations:.0f} iterations")
        print("   in the O(n²) shortest_remaining loop (scheduler.cpp:133-136)")
        print()

    if statistics.mean(time_limits) > 1000:
        print("⚠️  WARNING: Very long job durations!")
        print(f"   Mean time_limit = {statistics.mean(time_limits):.1f}")
        print("   Long jobs → high concurrency → large wait queues → O(n²) impact")
        print()

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <trace.csv> [total_nodes]")
        print()
        print("Analyze trace characteristics that affect scheduler performance.")
        sys.exit(1)

    trace_file = sys.argv[1]
    total_nodes = int(sys.argv[2]) if len(sys.argv) > 2 else 100

    analyze_trace(trace_file, total_nodes)

if __name__ == '__main__':
    main()
