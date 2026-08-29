#!/usr/bin/env python3
"""
Minimal EASY Backfilling Oracle

Pure implementation of EASY backfilling algorithm for verification.
No dependencies, no fancy features - just correct EASY logic.

Algorithm:
1. Jobs arrive and enter wait queue (FCFS order)
2. First job in queue gets a RESERVATION (guaranteed start time)
3. Other jobs can BACKFILL if:
   - They fit in current free nodes
   - They will complete BEFORE the first job's reservation
4. When jobs complete, run scheduler again

This is the REFERENCE implementation for verifying DR_EVT.
"""

import csv
import sys
from dataclasses import dataclass
from typing import List, Optional, Dict
import heapq

@dataclass
class Job:
    """Job record"""
    idx: int
    submit_time: float
    nodes: int
    duration: float  # time_limit (what scheduler sees)

    # Simulation results (filled in by scheduler)
    start_time: Optional[float] = None
    end_time: Optional[float] = None

@dataclass
class Event:
    """Simulation event"""
    time: float
    type: str  # 'SUBMIT', 'START', 'END'
    job_idx: int

    def __lt__(self, other):
        # Sort by time first, then by job_idx for stable ordering
        if self.time != other.time:
            return self.time < other.time
        return self.job_idx < other.job_idx

class MinimalEasyOracle:
    """
    Minimal EASY backfilling scheduler

    This is a REFERENCE implementation - kept simple and obvious
    so we can manually verify it's correct.
    """

    def __init__(self, total_nodes: int, verbose: bool = False):
        self.total_nodes = total_nodes
        self.verbose = verbose

        # State
        self.current_time = 0.0
        self.wait_queue = []  # Jobs waiting to run (FCFS order)
        self.running = {}  # job_idx -> (start_time, end_time, nodes)
        self.completed = []  # List of completed jobs

        # Event queue
        self.events = []

    def log(self, msg: str):
        if self.verbose:
            print(f"[t={self.current_time:6.0f}] {msg}")

    def get_free_nodes(self) -> int:
        """Calculate currently free nodes"""
        used = sum(nodes for _, _, nodes in self.running.values())
        return self.total_nodes - used

    def find_next_free_time(self, nodes_needed: int) -> float:
        """
        Find the next time when nodes_needed will be available.

        This is the RESERVATION time for the first job in queue.
        """
        if nodes_needed > self.total_nodes:
            raise ValueError(f"Job needs {nodes_needed} but system only has {self.total_nodes}")

        # If we have space now
        if self.get_free_nodes() >= nodes_needed:
            return self.current_time

        # Find when enough nodes will be free
        # Sort running jobs by end time
        completions = sorted(
            [(end_time, nodes) for _, end_time, nodes in self.running.values()]
        )

        cumulative_freed = 0
        for end_time, nodes in completions:
            cumulative_freed += nodes
            if self.get_free_nodes() + cumulative_freed >= nodes_needed:
                return end_time

        # Should never reach here if nodes_needed <= total_nodes
        raise ValueError(f"Cannot find reservation time for {nodes_needed} nodes")

    def can_backfill(self, job: Job, reservation_time: float) -> bool:
        """
        Check if job can backfill.

        Criteria:
        1. Fits in current free nodes
        2. Will complete before reservation_time
        """
        if self.get_free_nodes() < job.nodes:
            return False

        estimated_completion = self.current_time + job.duration
        if estimated_completion >= reservation_time:
            return False

        return True

    def start_job(self, job: Job):
        """Start a job running"""
        job.start_time = self.current_time
        job.end_time = self.current_time + job.duration

        self.running[job.idx] = (job.start_time, job.end_time, job.nodes)

        # Schedule END event
        heapq.heappush(self.events, Event(job.end_time, 'END', job.idx))

        self.log(f"START job {job.idx}: {job.nodes} nodes, duration={job.duration}")

    def complete_job(self, job_idx: int):
        """Complete a job"""
        start_time, end_time, nodes = self.running.pop(job_idx)
        self.log(f"END job {job_idx}: freed {nodes} nodes")

    def schedule(self, jobs: Dict[int, Job]):
        """
        Run EASY backfilling scheduler.

        Main scheduling logic - this is what we're verifying!
        """
        if not self.wait_queue:
            self.log("No jobs in wait queue")
            return

        self.log(f"Scheduling: {len(self.wait_queue)} jobs waiting, {self.get_free_nodes()} nodes free")

        # EASY Algorithm:
        # 1. First job in queue gets reservation
        first_job_idx = self.wait_queue[0]
        first_job = jobs[first_job_idx]

        reservation_time = self.find_next_free_time(first_job.nodes)
        self.log(f"First job {first_job_idx} needs {first_job.nodes} nodes, reservation at t={reservation_time}")

        # 2. Try to start first job if possible
        if self.get_free_nodes() >= first_job.nodes:
            self.wait_queue.pop(0)
            self.start_job(first_job)
            # Recurse - might be able to start more
            self.schedule(jobs)
            return

        # 3. Try backfilling other jobs
        # Check each job in queue (after first) to see if it can backfill
        backfilled = []
        for i in range(1, len(self.wait_queue)):
            job_idx = self.wait_queue[i]
            job = jobs[job_idx]

            if self.can_backfill(job, reservation_time):
                self.log(f"BACKFILL job {job_idx}: fits in {self.get_free_nodes()} free nodes, "
                        f"completes at {self.current_time + job.duration} < {reservation_time}")
                backfilled.append(i)
                self.start_job(job)

        # Remove backfilled jobs from queue (in reverse order to preserve indices)
        for i in reversed(backfilled):
            self.wait_queue.pop(i)

        # If we backfilled anything, try scheduling again
        if backfilled:
            self.schedule(jobs)

    def simulate(self, jobs: List[Job]) -> List[Job]:
        """
        Run full simulation.

        Returns list of jobs with start_time and end_time filled in.
        """
        # Create job dict for fast lookup
        job_dict = {job.idx: job for job in jobs}

        # Schedule all SUBMIT events
        for job in jobs:
            heapq.heappush(self.events, Event(job.submit_time, 'SUBMIT', job.idx))

        # Main event loop
        while self.events:
            event = heapq.heappop(self.events)
            self.current_time = event.time

            if event.type == 'SUBMIT':
                job = job_dict[event.job_idx]
                self.log(f"SUBMIT job {event.job_idx}: {job.nodes} nodes, duration={job.duration}")
                self.wait_queue.append(event.job_idx)
                self.schedule(job_dict)

            elif event.type == 'END':
                # Process ALL END events at current_time before scheduling
                # This ensures consistent resource view
                self.complete_job(event.job_idx)

                # Process remaining END events at same time
                while self.events and self.events[0].time == self.current_time and self.events[0].type == 'END':
                    next_event = heapq.heappop(self.events)
                    self.complete_job(next_event.job_idx)

                # Now call scheduler with all resources freed
                self.schedule(job_dict)

        return jobs

def load_trace(filename: str) -> List[Job]:
    """Load trace in DR_EVT format"""
    jobs = []
    with open(filename, 'r') as f:
        reader = csv.DictReader(f)
        for idx, row in enumerate(reader):
            jobs.append(Job(
                idx=idx,
                submit_time=float(row['job_submit_time']),
                nodes=int(row['num_nodes']),
                duration=float(row['time_limit'])
            ))
    return jobs

def write_oracle_output(jobs: List[Job], filename: str):
    """Write oracle results in format comparable to DR_EVT"""
    with open(filename, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['job_idx', 'submit_time', 'start_time', 'end_time', 'nodes', 'duration'])
        for job in jobs:
            writer.writerow([
                job.idx,
                job.submit_time,
                job.start_time if job.start_time is not None else '',
                job.end_time if job.end_time is not None else '',
                job.nodes,
                job.duration
            ])

def write_resource_trace(jobs: List[Job], filename: str, total_nodes: int):
    """Generate resource trace from job trace"""
    # Collect all events (start and end)
    events = []
    for job in jobs:
        if job.start_time is not None:
            events.append((job.start_time, +job.nodes, 'START'))
        if job.end_time is not None:
            events.append((job.end_time, -job.nodes, 'END'))

    # Sort by time
    events.sort()

    # Track resource state changes
    allocated = 0
    resource_history = []

    for time, delta, event_type in events:
        allocated += delta
        free = total_nodes - allocated
        resource_history.append((time, free, allocated))

    # Write resource trace
    with open(filename, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['time', 'free_nodes', 'allocated_nodes'])
        for time, free, allocated in resource_history:
            writer.writerow([time, free, allocated])

def main():
    if len(sys.argv) < 2:
        print("Usage: minimal_easy_oracle.py <trace.csv> [--verbose] [--nodes N]")
        print("\nGenerates oracle output for EASY backfilling verification")
        sys.exit(1)

    trace_file = sys.argv[1]
    verbose = '--verbose' in sys.argv

    # Parse total_nodes
    total_nodes = 1000  # default
    if '--nodes' in sys.argv:
        idx = sys.argv.index('--nodes')
        total_nodes = int(sys.argv[idx + 1])

    print(f"=== Minimal EASY Backfilling Oracle ===")
    print(f"Trace: {trace_file}")
    print(f"Total nodes: {total_nodes}")
    print(f"Verbose: {verbose}")
    print()

    # Load trace
    jobs = load_trace(trace_file)
    print(f"Loaded {len(jobs)} jobs")

    # Run oracle
    oracle = MinimalEasyOracle(total_nodes, verbose=verbose)
    jobs = oracle.simulate(jobs)

    # Write output
    output_file = trace_file.replace('.csv', '_oracle.csv')
    write_oracle_output(jobs, output_file)

    # Write resource trace
    resource_file = trace_file.replace('.csv', '_oracle_resources.csv')
    write_resource_trace(jobs, resource_file, total_nodes)

    print(f"\nOracle output written to: {output_file}")
    print(f"Resource trace written to: {resource_file}")
    print(f"Final simulation time: {oracle.current_time}")

    # Verify all jobs completed
    completed = sum(1 for job in jobs if job.start_time is not None)
    print(f"Jobs completed: {completed}/{len(jobs)}")

    if completed != len(jobs):
        print("\n⚠ WARNING: Not all jobs completed!")
        for job in jobs:
            if job.start_time is None:
                print(f"  Job {job.idx}: never started")

if __name__ == '__main__':
    main()
