#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

"""
Calculate resource trace analytically from job trace.

Given a job schedule (job_id, submit_time, start_time, end_time, nodes, duration),
calculate the exact resource usage at each time point.

This provides a hand-crafted oracle for validating simulator resource traces.
"""

import sys
import csv
from collections import defaultdict

def calculate_resource_trace(job_trace_file, total_nodes=100):
    """
    Calculate resource usage from job schedule.

    Returns list of (time, free_nodes, allocated_nodes) tuples.
    """
    # Read jobs
    jobs = []
    with open(job_trace_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            # Handle different column name conventions
            start_time = row.get('start_time') or row.get('begin_time')
            nodes = row.get('nodes') or row.get('num_nodes') or row.get('nodes_requested')

            jobs.append({
                'job_id': row.get('job_id') or row.get('job_idx') or row.get('job_submit_time'),
                'start_time': float(start_time),
                'end_time': float(row['end_time']),
                'nodes': int(nodes)
            })

    # Create events: (time, delta_nodes)
    # Positive delta = allocation (job start), negative = deallocation (job end)
    events = []
    for job in jobs:
        events.append((job['start_time'], job['nodes'], 'start', job['job_id']))
        events.append((job['end_time'], -job['nodes'], 'end', job['job_id']))

    # Sort by time, then by event type (end before start at same time)
    events.sort(key=lambda x: (x[0], 0 if x[2] == 'end' else 1))

    # Calculate resource usage at each event
    resource_trace = []
    allocated = 0

    # Initial state
    resource_trace.append((0, total_nodes, 0))

    for time, delta, event_type, job_id in events:
        allocated += delta
        free = total_nodes - allocated

        if free < 0:
            print(f"ERROR: Over-allocation at time {time}!", file=sys.stderr)
            print(f"  Event: {event_type} job {job_id}, delta={delta}", file=sys.stderr)
            print(f"  Allocated: {allocated}, Free: {free}", file=sys.stderr)
            sys.exit(1)

        resource_trace.append((time, free, allocated))

    return resource_trace

def write_resource_trace(resource_trace, output_file):
    """Write resource trace to CSV."""
    with open(output_file, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['time', 'free_nodes', 'allocated_nodes'])
        for time, free, allocated in resource_trace:
            writer.writerow([time, free, allocated])

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <job_trace.csv> <resource_trace.csv> [total_nodes]")
        print()
        print("Calculate resource trace analytically from job schedule.")
        sys.exit(1)

    job_trace_file = sys.argv[1]
    resource_trace_file = sys.argv[2]
    total_nodes = int(sys.argv[3]) if len(sys.argv) > 3 else 100

    resource_trace = calculate_resource_trace(job_trace_file, total_nodes)
    write_resource_trace(resource_trace, resource_trace_file)

    print(f"Calculated resource trace from {job_trace_file}")
    print(f"Total events: {len(resource_trace)}")
    print(f"Wrote to: {resource_trace_file}")

if __name__ == '__main__':
    main()
