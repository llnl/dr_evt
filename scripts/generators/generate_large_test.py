#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

"""Generate a realistic 500-job test trace for stress testing"""

import csv
import random
import numpy as np

def generate_job_trace(num_jobs=500, total_nodes=1000, output_file="test_traces/sim_500jobs.csv"):
    """
    Generate realistic job trace with:
    - Varied arrival patterns (Poisson arrivals)
    - Mixed job sizes (small, medium, large)
    - Realistic durations
    """

    random.seed(42)  # Reproducible
    np.random.seed(42)

    jobs = []

    # Job size distribution (realistic HPC workload)
    # 60% small (1-32 nodes)
    # 30% medium (33-128 nodes)
    # 10% large (129-512 nodes)

    # Arrival times: Poisson process with varying rate
    # Mean inter-arrival time: 5 seconds
    current_time = 0

    for i in range(num_jobs):
        # Determine job size
        r = random.random()
        if r < 0.6:  # Small job
            nodes = random.randint(1, 32)
            duration_base = random.randint(60, 600)  # 1-10 minutes
        elif r < 0.9:  # Medium job
            nodes = random.randint(33, 128)
            duration_base = random.randint(300, 3600)  # 5-60 minutes
        else:  # Large job
            nodes = random.randint(129, min(512, total_nodes // 2))
            duration_base = random.randint(1800, 7200)  # 30-120 minutes

        # Add some variability to duration
        duration = duration_base + random.randint(-duration_base // 10, duration_base // 10)
        duration = max(60, duration)  # At least 1 minute

        # Arrival time (Poisson process)
        inter_arrival = np.random.exponential(5.0)  # Mean 5 seconds between arrivals
        current_time += inter_arrival

        jobs.append({
            'job_submit_time': int(current_time),  # Integer seconds for epoch format
            'num_nodes': nodes,
            'exit_status': 0,
            'queue': 'pbatch',
            'time_limit': duration
        })

    # Write to CSV
    with open(output_file, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=['job_submit_time', 'num_nodes', 'exit_status', 'queue', 'time_limit'])
        writer.writeheader()
        writer.writerows(jobs)

    # Print statistics
    print(f"Generated {num_jobs} jobs")
    print(f"Output: {output_file}")
    print(f"\nJob size distribution:")
    small = sum(1 for j in jobs if j['num_nodes'] <= 32)
    medium = sum(1 for j in jobs if 33 <= j['num_nodes'] <= 128)
    large = sum(1 for j in jobs if j['num_nodes'] > 128)
    print(f"  Small (1-32 nodes): {small} ({100*small/num_jobs:.1f}%)")
    print(f"  Medium (33-128 nodes): {medium} ({100*medium/num_jobs:.1f}%)")
    print(f"  Large (129+ nodes): {large} ({100*large/num_jobs:.1f}%)")

    print(f"\nDuration range: {min(j['time_limit'] for j in jobs)}-{max(j['time_limit'] for j in jobs)} seconds")
    print(f"Arrival time range: 0-{jobs[-1]['job_submit_time']:.1f} seconds ({jobs[-1]['job_submit_time']/60:.1f} minutes)")
    print(f"Mean inter-arrival: {jobs[-1]['job_submit_time']/num_jobs:.2f} seconds")

    total_node_seconds = sum(j['num_nodes'] * j['time_limit'] for j in jobs)
    print(f"\nTotal demand: {total_node_seconds/1e6:.2f}M node-seconds")
    print(f"If system has {total_nodes} nodes and runs for {jobs[-1]['job_submit_time']/60:.1f} min:")
    print(f"  Theoretical max capacity: {total_nodes * jobs[-1]['job_submit_time']/1e6:.2f}M node-seconds")
    print(f"  Load factor: {100*total_node_seconds/(total_nodes * jobs[-1]['job_submit_time']):.1f}%")

    return jobs

if __name__ == '__main__':
    import sys

    num_jobs = int(sys.argv[1]) if len(sys.argv) > 1 else 500
    total_nodes = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
    output = sys.argv[3] if len(sys.argv) > 3 else f"test_traces/sim_{num_jobs}jobs.csv"

    print(f"Generating {num_jobs}-job test trace")
    print(f"System: {total_nodes} nodes")
    print("-" * 60)

    generate_job_trace(num_jobs, total_nodes, output)

    print(f"\nTo run DR_EVT simulation:")
    # This trace has no actual_run_time column, so we must use run_time_mode=limit
    # (jobs run for their full time_limit). This is unrealistic but useful for
    # testing worst-case scheduler behavior.
    print(f"  ./build/simulator {output} \\")
    print(f"    --total_nodes {total_nodes} \\")
    print(f"    --trace_format simple \\")
    print(f"    --timestamp_format epoch \\")
    print(f"    --run_time_mode limit \\")
    print(f"    --outfile sim_{num_jobs}jobs_output.csv")
