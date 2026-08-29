#!/usr/bin/env python3
"""
Example: DR_EVT Streaming API usage

Demonstrates how to:
1. Create simulator and load trace
2. Submit jobs incrementally
3. Monitor resource usage
4. Get queue and scheduling statistics
"""

import dr_evt

def main():
    # Configure simulation
    params = dr_evt.SimParams()
    params.infile = "examples/sample_trace.csv"
    params.total_nodes = 100
    params.trace_format = "simple"
    params.timestamp_format = "epoch"
    params.duration_mode = dr_evt.DurationMode.EXACT
    params.backfill_policy = dr_evt.BackfillPolicy.EASY
    params.priority_policy = dr_evt.PriorityPolicy.FCFS
    params.verbose = False

    # Create simulator
    sim = dr_evt.Simulation(params)

    # Load trace (required before streaming)
    num_jobs = sim.initialize_trace(max_jobs=0)  # 0 = load all
    print(f"Loaded {num_jobs} jobs from trace")

    print("\n" + "="*60)
    print("Streaming Simulation with Monitoring")
    print("="*60)

    # Submit and run jobs incrementally
    for job_idx in range(min(10, num_jobs)):
        # Submit job at time t = job_idx * 10
        submit_time = job_idx * 10.0
        sim.insert_job(job_idx, submit_time)

        # Advance to submit time
        sim.run_until_inclusive(submit_time)

        # Monitor state after each job
        print(f"\nTime {sim.get_current_time():.1f}:")
        print(f"  Job {job_idx} submitted")
        print(f"  Nodes in use: {sim.get_nodes_in_use()}/{params.total_nodes}")
        print(f"  Available: {sim.get_available_nodes()}")
        print(f"  Wait queue: {sim.get_wait_queue_size()} jobs")

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
    sim.run_until_inclusive(10000.0)

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
