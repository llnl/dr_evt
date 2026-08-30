#!/usr/bin/env python3
"""
Python Reference EASY Backfilling Scheduler

Pure implementation of EASY backfilling algorithm for verification.
No dependencies, no fancy features - just correct EASY logic.

IMPORTANT: This script writes output files to /tmp by default to avoid
polluting the working directory. Use --outdir to specify a different location.

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
import os
from dataclasses import dataclass
from typing import List, Optional, Dict
import heapq

@dataclass
class Job:
    """Job record"""
    idx: int
    submit_time: float
    nodes: int
    duration: float  # time_limit (what scheduler sees for planning)
    actual_runtime: Optional[float] = None  # actual runtime (≤ duration), defaults to duration

    # Simulation results (filled in by scheduler)
    start_time: Optional[float] = None
    end_time: Optional[float] = None

    def __post_init__(self):
        # Default actual_runtime to duration if not specified
        if self.actual_runtime is None:
            self.actual_runtime = self.duration

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

class EasyBackfillingScheduler:
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
        self.running = {}  # job_idx -> (start_time, actual_end, pessimistic_end, nodes)
        self.completed = []  # List of completed jobs

        # Event queue
        self.events = []

        # Resource tracking
        self.resource_timeline = []  # List of (time, nodes_used, nodes_free, running_jobs)

    def log(self, msg: str):
        if self.verbose:
            print(f"[t={self.current_time:6.0f}] {msg}", file=sys.stderr)

    def get_free_nodes(self) -> int:
        """Calculate currently free nodes"""
        used = sum(nodes for _, _, _, nodes in self.running.values())
        return self.total_nodes - used

    def record_resource_state(self):
        """Record current resource state to timeline"""
        nodes_used = sum(nodes for _, _, _, nodes in self.running.values())
        nodes_free = self.total_nodes - nodes_used
        running_jobs = ','.join(str(idx) for idx in sorted(self.running.keys()))

        self.resource_timeline.append((
            self.current_time,
            nodes_used,
            nodes_free,
            running_jobs if running_jobs else ''
        ))

    def calculate_reservation_time(self) -> Optional[float]:
        """
        Calculate when the FCFS head (first waiting job) can start

        Returns None if no job is waiting
        """
        if not self.wait_queue:
            return None

        fcfs_head = self.wait_queue[0]

        # If it fits now, no need for reservation
        if fcfs_head.nodes <= self.get_free_nodes():
            return self.current_time

        # Find earliest time when enough nodes will be free
        # Look at running jobs and their pessimistic end times
        events = []
        for job_idx, (start_time, actual_end, pessimistic_end, nodes) in self.running.items():
            events.append((pessimistic_end, +nodes, job_idx))

        events.sort()

        # Simulate freeing nodes over time
        cumulative_free = self.get_free_nodes()
        for time, freed_nodes, job_idx in events:
            cumulative_free += freed_nodes
            if cumulative_free >= fcfs_head.nodes:
                return time

        # Should never reach here if system is not oversubscribed
        return None

    def try_start_jobs(self):
        """
        Try to start jobs from wait queue

        EASY algorithm:
        1. FCFS head gets reservation
        2. Other jobs can backfill if they fit AND finish before reservation
        """
        if not self.wait_queue:
            return

        reservation_time = self.calculate_reservation_time()
        free_nodes = self.get_free_nodes()

        # Try to start jobs
        started = []
        for i, job in enumerate(self.wait_queue):
            if job.nodes <= free_nodes:
                # Check backfill constraint
                job_end = self.current_time + job.duration

                if i == 0:
                    # FCFS head - always start if it fits
                    self.log(f"Starting FCFS head job {job.idx}")
                    self.start_job(job)
                    started.append(i)
                    free_nodes -= job.nodes
                elif reservation_time is not None and job_end < reservation_time:
                    # Backfiller - only if it completes before reservation
                    self.log(f"Backfilling job {job.idx} (ends at {job_end} < reservation {reservation_time})")
                    self.start_job(job)
                    started.append(i)
                    free_nodes -= job.nodes
                else:
                    # Would interfere with FCFS head reservation
                    self.log(f"Blocking job {job.idx} (would end at {job_end} >= reservation {reservation_time})")

        # Remove started jobs from wait queue (in reverse to preserve indices)
        for i in reversed(started):
            self.wait_queue.pop(i)

    def start_job(self, job: Job):
        """Start a job immediately"""
        job.start_time = self.current_time
        job.end_time = self.current_time + job.actual_runtime

        # Track both actual and pessimistic (scheduler's view) end times
        pessimistic_end = self.current_time + job.duration
        self.running[job.idx] = (self.current_time, job.end_time, pessimistic_end, job.nodes)

        # Schedule END event
        heapq.heappush(self.events, Event(job.end_time, 'END', job.idx))

        self.record_resource_state()

    def handle_submit(self, job: Job):
        """Handle job submission"""
        self.log(f"Job {job.idx} submitted (nodes={job.nodes}, duration={job.duration})")
        self.wait_queue.append(job)
        self.try_start_jobs()

    def handle_end(self, job_idx: int):
        """Handle job completion"""
        if job_idx not in self.running:
            return

        start_time, actual_end, pessimistic_end, nodes = self.running.pop(job_idx)
        self.log(f"Job {job_idx} completed")

        self.record_resource_state()
        self.try_start_jobs()

    def simulate(self, jobs: List[Job]) -> List[Job]:
        """
        Run EASY backfilling simulation

        Returns jobs with start_time and end_time filled in
        """
        # Schedule SUBMIT events
        for job in jobs:
            heapq.heappush(self.events, Event(job.submit_time, 'SUBMIT', job.idx))

        # Create job lookup
        job_map = {job.idx: job for job in jobs}

        # Event loop
        while self.events:
            event = heapq.heappop(self.events)
            self.current_time = event.time

            if event.type == 'SUBMIT':
                self.handle_submit(job_map[event.job_idx])
            elif event.type == 'END':
                self.handle_end(event.job_idx)

        # Return jobs with simulation results
        return list(job_map.values())

def load_trace(filename: str) -> List[Job]:
    """Load job trace from CSV"""
    jobs = []
    with open(filename, 'r') as f:
        reader = csv.DictReader(f)
        for idx, row in enumerate(reader):
            # Handle both simple format and full format
            submit_time = float(row.get('job_submit_time', row.get('submit_time', 0)))
            nodes = int(row.get('num_nodes', row.get('nodes', 1)))
            duration = float(row.get('time_limit', row.get('duration', 100)))

            # Check for actual_duration column (for early completion tests)
            actual_runtime = None
            if 'actual_duration' in row and row['actual_duration']:
                actual_runtime = float(row['actual_duration'])

            jobs.append(Job(
                idx=idx,
                submit_time=submit_time,
                nodes=nodes,
                duration=duration,
                actual_runtime=actual_runtime
            ))
    return jobs

def write_scheduler_output(jobs: List[Job], filename: str):
    """Write scheduler results in format comparable to DR_EVT"""
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
        print("Usage: python_reference_scheduler.py <trace.csv> [--verbose] [--nodes N] [--outdir DIR]")
        print()
        print("Python reference EASY backfilling scheduler for verification")
        print()
        print("Options:")
        print("  --verbose    Show detailed simulation progress")
        print("  --nodes N    Total nodes in system (default: 1000)")
        print("  --outdir DIR Output directory for generated files (default: /tmp)")
        print()
        print("Generates reference output for EASY backfilling verification")
        print("Output files are written to /tmp by default to avoid directory pollution")
        sys.exit(1)

    trace_file = sys.argv[1]
    verbose = '--verbose' in sys.argv

    # Parse total_nodes
    total_nodes = 1000  # default
    if '--nodes' in sys.argv:
        idx = sys.argv.index('--nodes')
        total_nodes = int(sys.argv[idx + 1])

    # Parse output directory (default to /tmp to avoid pollution)
    outdir = '/tmp'
    if '--outdir' in sys.argv:
        idx = sys.argv.index('--outdir')
        outdir = sys.argv[idx + 1]

    print(f"=== Python Reference EASY Backfilling Scheduler ===", file=sys.stderr)
    print(f"Trace: {trace_file}", file=sys.stderr)
    print(f"Total nodes: {total_nodes}", file=sys.stderr)
    print(f"Output directory: {outdir}", file=sys.stderr)
    print(f"Verbose: {verbose}", file=sys.stderr)
    print(file=sys.stderr)

    # Load trace
    jobs = load_trace(trace_file)
    print(f"Loaded {len(jobs)} jobs", file=sys.stderr)

    # Run scheduler
    scheduler = EasyBackfillingScheduler(total_nodes, verbose=verbose)
    jobs = scheduler.simulate(jobs)

    # Write output files to specified directory
    basename = os.path.basename(trace_file).replace('.csv', '')
    output_file = os.path.join(outdir, f'{basename}_reference.csv')
    write_scheduler_output(jobs, output_file)

    # Write resource trace
    resource_file = os.path.join(outdir, f'{basename}_reference_resources.csv')
    write_resource_trace(jobs, resource_file, total_nodes)

    print(f"\nReference output written to: {output_file}", file=sys.stderr)
    print(f"Resource trace written to: {resource_file}", file=sys.stderr)
    print(f"Final simulation time: {scheduler.current_time}", file=sys.stderr)

    # Verify all jobs completed
    completed = sum(1 for job in jobs if job.start_time is not None)
    print(f"Jobs completed: {completed}/{len(jobs)}", file=sys.stderr)

    if completed != len(jobs):
        print("\n⚠ WARNING: Not all jobs completed!", file=sys.stderr)
        for job in jobs:
            if job.start_time is None:
                print(f"  Job {job.idx}: never started", file=sys.stderr)

if __name__ == '__main__':
    main()
