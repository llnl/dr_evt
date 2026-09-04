#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

"""
Python Reference CONSERVATIVE Backfilling Scheduler

Pure implementation of CONSERVATIVE backfilling algorithm for verification.
No dependencies, no fancy features - just correct CONSERVATIVE logic.

IMPORTANT: This script writes output files to /tmp by default to avoid
polluting the working directory. Use --outdir to specify a different location.

Algorithm:
1. Jobs arrive and enter wait queue (FCFS order)
2. ALL jobs in queue get RESERVATIONS (guaranteed start times)
3. A job can BACKFILL only if:
   - It fits in current free nodes
   - It will complete BEFORE ANY waiting job ahead of it needs its reservation
4. When jobs complete, run scheduler again

Key difference from EASY:
- EASY: Only first job gets a reservation
- CONSERVATIVE: ALL jobs get reservations, backfill must not delay any of them

This is the REFERENCE implementation for verifying DR_EVT conservative backfilling.
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
    actual_run_time: Optional[float] = None  # actual run time (<= duration), defaults to duration

    # Simulation results (filled in by scheduler)
    start_time: Optional[float] = None
    end_time: Optional[float] = None

    def __post_init__(self):
        # Default actual_run_time to duration if not specified
        if self.actual_run_time is None:
            self.actual_run_time = self.duration

@dataclass
class Event:
    """Simulation event"""
    time: float
    type: str  # 'SUBMIT', 'START', 'END'
    job_idx: int

    def __lt__(self, other):
        # Sort by time first, then END before SUBMIT, then by job_idx
        if self.time != other.time:
            return self.time < other.time
        if self.type != other.type:
            type_order = {'END': 0, 'START': 1, 'SUBMIT': 2}
            return type_order[self.type] < type_order[other.type]
        return self.job_idx < other.job_idx

class ConservativeBackfillingScheduler:
    """
    Minimal CONSERVATIVE backfilling scheduler

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

    def build_resource_timeline(self):
        """
        Build a timeline of when resources will be freed (ONCE per scheduling event)
        Returns: list of (time, cumulative_free_nodes) sorted by time
        """
        events = []
        for job_idx, (start_time, actual_end, pessimistic_end, nodes) in self.running.items():
            events.append((pessimistic_end, nodes))

        events.sort()

        # Build cumulative free nodes over time
        timeline = [(self.current_time, self.get_free_nodes())]
        cumulative_free = self.get_free_nodes()

        for time, freed_nodes in events:
            cumulative_free += freed_nodes
            timeline.append((time, cumulative_free))

        return timeline

    def calculate_job_reservation_time_from_timeline(self, job: Job, timeline: list) -> float:
        """
        Calculate when a specific job can start using pre-built timeline

        Returns the earliest time when enough nodes will be free for this job
        """
        for time, available_nodes in timeline:
            if available_nodes >= job.nodes:
                return time

        # Should never reach here if system is not oversubscribed
        return self.current_time

    def calculate_conservative_window_with_running(self, backfill_pos: int, free_nodes: int,
                                                     effective_running: dict, exclude_jobs: set) -> Optional[float]:
        """
        Calculate the conservative backfill window considering effective running jobs

        Returns the earliest reservation time among all jobs ahead of backfill_pos
        effective_running: running jobs including already-backfilled jobs
        exclude_jobs: set of job IDs to exclude from wait_queue (already backfilled)
        """
        if backfill_pos >= len(self.wait_queue):
            return None

        earliest_reservation = float('inf')

        # Check all jobs ahead of this position
        for i in range(backfill_pos):
            if i >= len(self.wait_queue):
                break
            job = self.wait_queue[i]

            # Skip jobs that were already backfilled
            if job.idx in exclude_jobs:
                continue

            # Calculate reservation using effective running jobs
            reservation = self.calculate_job_reservation_time_with_running(job, effective_running)
            earliest_reservation = min(earliest_reservation, reservation)

        return earliest_reservation if earliest_reservation < float('inf') else None

    def calculate_job_reservation_time_with_running(self, job: Job, effective_running: dict) -> float:
        """
        Calculate when a job can start given effective running jobs (includes backfilled)
        """
        # Calculate free nodes from effective running
        total_used = sum(nodes for _, _, _, nodes in effective_running.values())
        current_free = self.total_nodes - total_used

        if job.nodes <= current_free:
            return self.current_time

        # Build timeline from effective running jobs
        events = []
        for job_idx, (start_time, actual_end, pessimistic_end, nodes) in effective_running.items():
            events.append((pessimistic_end, nodes))

        events.sort()

        # Find when enough nodes will be free
        cumulative_free = current_free
        for time, freed_nodes in events:
            cumulative_free += freed_nodes
            if cumulative_free >= job.nodes:
                return time

        return self.current_time

    def try_start_jobs(self):
        """
        Try to start jobs from wait queue

        CONSERVATIVE algorithm:
        1. All jobs in queue get reservations
        2. Start any jobs from the front that fit now (cascading)
        3. Other jobs can backfill if they fit AND finish before ANY waiting job's reservation
        """
        free_nodes = self.get_free_nodes()

        # Step 1: Start jobs from the front as long as they fit
        while self.wait_queue and self.wait_queue[0].nodes <= free_nodes:
            head = self.wait_queue.pop(0)
            self.log(f"Starting FCFS head job {head.idx}")
            self.start_job(head)
            free_nodes -= head.nodes

        if not self.wait_queue:
            return

        # Step 2: Try conservative backfilling
        # For each job in the queue (after the head), check if it can backfill

        remaining = []
        backfilled_jobs = {}  # Track backfilled jobs as {job_id: (start_time, pessimistic_end)}

        for pos, job in enumerate(self.wait_queue):
            # Check if job fits in available resources
            if job.nodes > free_nodes:
                remaining.append(job)
                continue

            # Calculate conservative window: earliest reservation among jobs ahead
            # Include both truly running jobs and already-backfilled jobs from this cycle
            effective_running = self.running.copy()
            for jid, (start_time, pess_end, nodes) in backfilled_jobs.items():
                # Add backfilled job as if it's running
                effective_running[jid] = (start_time, pess_end, pess_end, nodes)

            conservative_window = self.calculate_conservative_window_with_running(pos, free_nodes, effective_running, set(backfilled_jobs.keys()))

            if conservative_window is None:
                # No jobs ahead - shouldn't happen, but safe to backfill
                self.log(f"Backfilling job {job.idx} (no jobs ahead)")
                self.start_job(job)
                free_nodes -= job.nodes
                continue

            # Job can backfill if it completes before the earliest reservation
            job_end = self.current_time + job.duration
            if job_end < conservative_window:
                self.log(f"Conservative backfill job {job.idx} (ends {job_end} < earliest reservation {conservative_window})")
                self.start_job(job)
                free_nodes -= job.nodes

                # Track this backfilled job so next jobs see it as running
                pessimistic_end = self.current_time + job.duration
                backfilled_jobs[job.idx] = (self.current_time, pessimistic_end, job.nodes)
                continue
            else:
                self.log(f"Blocking job {job.idx} (would end {job_end} >= earliest reservation {conservative_window})")
                remaining.append(job)

        self.wait_queue = remaining

    def start_job(self, job: Job):
        """Start a job immediately"""
        job.start_time = self.current_time
        job.end_time = self.current_time + job.actual_run_time

        # Track both actual and pessimistic (scheduler's view) end times
        pessimistic_end = self.current_time + job.duration
        self.running[job.idx] = (self.current_time, job.end_time, pessimistic_end, job.nodes)

        # Schedule END event
        heapq.heappush(self.events, Event(job.end_time, 'END', job.idx))

        self.record_resource_state()

    def handle_submit(self, job: Job):
        """Handle job submission: enqueue it"""
        self.log(f"Job {job.idx} submitted (nodes={job.nodes}, duration={job.duration})")
        self.wait_queue.append(job)

    def handle_end(self, job_idx: int):
        """Handle job completion: free its resources"""
        if job_idx not in self.running:
            return

        start_time, actual_end, pessimistic_end, nodes = self.running.pop(job_idx)
        self.log(f"Job {job_idx} completed")

        self.record_resource_state()

    def simulate(self, jobs: List[Job]) -> List[Job]:
        """
        Run CONSERVATIVE backfilling simulation

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

            # Drain all events at this exact timestamp before scheduling
            same_time_events = [event]
            while self.events and self.events[0].time == self.current_time:
                same_time_events.append(heapq.heappop(self.events))

            same_time_events.sort()

            for evt in same_time_events:
                if evt.type == 'SUBMIT':
                    self.handle_submit(job_map[evt.job_idx])
                elif evt.type == 'END':
                    self.handle_end(evt.job_idx)

            self.try_start_jobs()

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

            # Check for actual_run_time column
            actual_run_time = None
            if 'actual_run_time' in row and row['actual_run_time']:
                actual_run_time = float(row['actual_run_time'])

            jobs.append(Job(
                idx=idx,
                submit_time=submit_time,
                nodes=nodes,
                duration=duration,
                actual_run_time=actual_run_time
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
        print("Usage: python_conservative_scheduler.py <trace.csv> [--verbose] [--nodes N] [--outdir DIR]")
        print()
        print("Python reference CONSERVATIVE backfilling scheduler for verification")
        print()
        print("Options:")
        print("  --verbose    Show detailed simulation progress")
        print("  --nodes N    Total nodes in system (default: 1000)")
        print("  --outdir DIR Output directory for generated files (default: /tmp)")
        print()
        print("Generates reference output for CONSERVATIVE backfilling verification")
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

    print(f"=== Python Reference CONSERVATIVE Backfilling Scheduler ===", file=sys.stderr)
    print(f"Trace: {trace_file}", file=sys.stderr)
    print(f"Total nodes: {total_nodes}", file=sys.stderr)
    print(f"Output directory: {outdir}", file=sys.stderr)
    print(f"Verbose: {verbose}", file=sys.stderr)
    print(file=sys.stderr)

    # Load trace
    jobs = load_trace(trace_file)
    print(f"Loaded {len(jobs)} jobs", file=sys.stderr)

    # Run scheduler
    scheduler = ConservativeBackfillingScheduler(total_nodes, verbose=verbose)
    jobs = scheduler.simulate(jobs)

    # Write output files to specified directory
    basename = os.path.basename(trace_file).replace('.csv', '')
    output_file = os.path.join(outdir, f'{basename}_conservative_reference.csv')
    write_scheduler_output(jobs, output_file)

    # Write resource trace
    resource_file = os.path.join(outdir, f'{basename}_conservative_reference_resources.csv')
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
