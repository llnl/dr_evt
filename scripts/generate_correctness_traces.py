#!/usr/bin/env python3
"""
Generate comprehensive scheduler correctness test traces

Creates workloads that:
- Gradually saturate the system
- Create backlog in the queue
- Test backfilling under various conditions
- Stress resource accounting
"""

import random
import csv


def generate_workload(
    num_jobs=100,
    total_nodes=100,
    duration_sec=1000,
    oracle_mode=True
):
    """
    Generate a representative job workload

    Args:
        num_jobs: Number of jobs to generate
        total_nodes: Total nodes in system
        duration_sec: Simulation duration
        oracle_mode: If True, limit=duration. If False, limit=duration*2
    """
    jobs = []

    # Job size distribution (in percentage of total_nodes)
    # 10% large (60-80%), 30% medium (10-30%), 60% small (1-10%)

    current_time = 0
    arrival_rate = duration_sec / num_jobs  # Average time between arrivals

    random.seed(42)  # Deterministic generation

    for job_id in range(num_jobs):
        # Determine job size
        rand = random.random()
        if rand < 0.10:  # 10% large jobs
            nodes = random.randint(int(total_nodes * 0.6), int(total_nodes * 0.8))
            duration = random.randint(100, 300)
        elif rand < 0.40:  # 30% medium jobs
            nodes = random.randint(int(total_nodes * 0.1), int(total_nodes * 0.3))
            duration = random.randint(50, 150)
        else:  # 60% small jobs
            nodes = random.randint(1, int(total_nodes * 0.1))
            duration = random.randint(10, 50)

        # Job arrival time (with some clustering to create saturation)
        if job_id < num_jobs * 0.3:
            # First 30% arrive quickly (create initial saturation)
            current_time += random.uniform(0, arrival_rate * 0.5)
        elif job_id < num_jobs * 0.7:
            # Middle 40% arrive at normal rate
            current_time += random.uniform(0, arrival_rate * 1.2)
        else:
            # Last 30% arrive slower (let system drain)
            current_time += random.uniform(0, arrival_rate * 2.0)

        submit_time = int(current_time)

        # Set time limit (scheduler uses this to plan)
        if oracle_mode:
            limit = duration  # Perfect information
        else:
            # Overestimate by 1.5x to 2.5x (realistic user behavior)
            limit = int(duration * random.uniform(1.5, 2.5))

        # For SIMULATION mode: NO begin_time or end_time
        # The scheduler computes those based on submit_time + time_limit
        jobs.append({
            'job_submit_time': submit_time,
            'num_nodes': nodes,
            'exit_status': 0,
            'queue': 'pbatch',
            'time_limit': limit,
            'actual_duration': duration  # Hidden field for validation
        })

    return jobs


def write_trace(filename, jobs):
    """Write jobs to CSV trace file"""
    with open(filename, 'w', newline='') as f:
        # SIMULATION format: no begin_time/end_time (scheduler computes them)
        fieldnames = [
            'job_submit_time', 'num_nodes', 'exit_status',
            'queue', 'time_limit', 'actual_duration'
        ]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(jobs)

    # Print statistics
    total_jobs = len(jobs)
    large_jobs = sum(1 for j in jobs if j['num_nodes'] >= 60)
    medium_jobs = sum(1 for j in jobs if 10 <= j['num_nodes'] < 60)
    small_jobs = sum(1 for j in jobs if j['num_nodes'] < 10)

    total_duration = max(j['end_time'] for j in jobs)
    avg_duration = sum(j['end_time'] - j['begin_time'] for j in jobs) / total_jobs

    print(f"Generated: {filename}")
    print(f"  Total jobs: {total_jobs}")
    print(f"  Large jobs (60-80 nodes): {large_jobs}")
    print(f"  Medium jobs (10-59 nodes): {medium_jobs}")
    print(f"  Small jobs (1-9 nodes): {small_jobs}")
    print(f"  Simulation duration: {total_duration} sec")
    print(f"  Average job duration: {avg_duration:.1f} sec")
    print()


def main():
    print("Generating comprehensive scheduler correctness test traces")
    print("=" * 60)
    print()

    # 50-job traces (moderate scale)
    print("Generating 50-job traces...")
    jobs_50_oracle = generate_workload(
        num_jobs=50,
        total_nodes=100,
        duration_sec=500,
        oracle_mode=True
    )
    write_trace('tests/test_traces/correctness_50jobs_oracle.csv', jobs_50_oracle)

    jobs_50_realistic = generate_workload(
        num_jobs=50,
        total_nodes=100,
        duration_sec=500,
        oracle_mode=False
    )
    write_trace('tests/test_traces/correctness_50jobs_realistic.csv', jobs_50_realistic)

    # 100-job traces (large scale)
    print("Generating 100-job traces...")
    jobs_100_oracle = generate_workload(
        num_jobs=100,
        total_nodes=100,
        duration_sec=1000,
        oracle_mode=True
    )
    write_trace('tests/test_traces/correctness_100jobs_oracle.csv', jobs_100_oracle)

    jobs_100_realistic = generate_workload(
        num_jobs=100,
        total_nodes=100,
        duration_sec=1000,
        oracle_mode=False
    )
    write_trace('tests/test_traces/correctness_100jobs_realistic.csv', jobs_100_realistic)

    print("=" * 60)
    print("Trace generation complete!")
    print()
    print("Usage:")
    print("  python3 tests/test_scheduler_correctness.py")
    print("  # Edit to use new trace files")


if __name__ == '__main__':
    main()
