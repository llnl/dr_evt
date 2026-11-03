#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

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
from typing import List, Optional, Dict
import heapq

class Job:
    """Job record"""

    def __init__(self, idx: int, submit_time: float, nodes: int,
                 duration: float, actual_run_time: Optional[float] = None):
        self.idx = idx
        self.submit_time = submit_time
        self.nodes = nodes
        self.duration = duration
        self.actual_run_time = (duration if actual_run_time is None
                                else actual_run_time)
        self.start_time = None
        self.end_time = None

class Event:
    """Simulation event"""

    def __init__(self, time: float, event_type: str, job_idx: int):
        self.time = time
        self.type = event_type  # 'SUBMIT', 'START', 'END', 'RELEASE'
        self.job_idx = job_idx

    def __lt__(self, other):
        # Sort by time first, then END before RELEASE before SUBMIT (freeing
        # nodes takes effect before a same-timestamp new arrival is evaluated
        # against them - matches the C++ simulator's explicit same-timestamp
        # batching, which always processes completions and node releases
        # before making new scheduling decisions), then by job_idx for
        # stable ordering.
        if self.time != other.time:
            return self.time < other.time
        if self.type != other.type:
            type_order = {'END': 0, 'RELEASE': 1, 'START': 2, 'SUBMIT': 3}
            return type_order[self.type] < type_order[other.type]
        return self.job_idx < other.job_idx

class EasyBackfillingScheduler:
    """
    Minimal EASY backfilling scheduler

    This is a REFERENCE implementation - kept simple and obvious
    so we can manually verify it's correct.
    """

    def __init__(self, total_nodes: int, verbose: bool = False,
                 release_delay: float = 0.0,
                 planning_horizon: float = float('inf'),
                 backfill_queue_depth: int = 0):
        self.total_nodes = total_nodes
        self.verbose = verbose
        # Node release delay (constant model, block release): after a job
        # ends, all of its nodes stay unavailable for this long before
        # another job can start on them. 0 releases nodes at the job's end.
        # The scheduler does not plan with it: as in Slurm, Flux, and LSF, a
        # running job's projected release is its pessimistic end, and the
        # cleanup time shows up when the nodes fail to come back. Once a job
        # has ended, its nodes are observably releasing and their actual
        # release time is used.
        self.release_delay = release_delay
        # A head whose reservation starts more than this far ahead is not
        # planned: it holds no reservation, so candidates are not tested
        # against it (Slurm bf_window). Infinity plans every reservation.
        self.planning_horizon = planning_horizon
        # Waiting jobs examined in one backfill pass, the blocked head
        # included when its reservation is planned; 0 means unlimited.
        # Matches the simulator's --backfill_queue_depth.
        self.backfill_queue_depth = backfill_queue_depth

        # State
        self.current_time = 0.0
        self.wait_queue = []  # Jobs waiting to run (FCFS order)
        self.running = {}  # job_idx -> (start_time, actual_end, pessimistic_end, nodes)
        self.releasing = {}  # job_idx -> (release_time, nodes): ended, nodes not yet reusable
        self.completed = []  # List of completed jobs

        # Event queue
        self.events = []

        # Resource tracking
        self.resource_timeline = []  # List of (time, nodes_used, nodes_free, running_jobs)

    def log(self, msg: str):
        if self.verbose:
            print(f"[t={self.current_time:6.0f}] {msg}", file=sys.stderr)

    def get_releasing_nodes(self) -> int:
        """Nodes whose job has ended but which are not yet reusable"""
        return sum(nodes for _, nodes in self.releasing.values())

    def get_free_nodes(self) -> int:
        """Calculate currently free nodes (releasing nodes are not free)"""
        used = sum(nodes for _, _, _, nodes in self.running.values())
        return self.total_nodes - used - self.get_releasing_nodes()

    def record_resource_state(self):
        """Record current resource state to timeline"""
        nodes_used = sum(nodes for _, _, _, nodes in self.running.values())
        nodes_releasing = self.get_releasing_nodes()
        nodes_free = self.total_nodes - nodes_used - nodes_releasing
        running_jobs = ','.join(str(idx) for idx in sorted(self.running.keys()))

        self.resource_timeline.append((
            self.current_time,
            nodes_used,
            nodes_free,
            nodes_releasing,
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
        # Look at running jobs and their pessimistic end times (the delay is
        # not planned for), and at ended jobs whose nodes are still releasing
        # (their actual release time is known).
        events = []
        for job_idx, (start_time, actual_end, pessimistic_end, nodes) in self.running.items():
            events.append((pessimistic_end, +nodes, job_idx))
        for job_idx, (release_time, nodes) in self.releasing.items():
            events.append((release_time, +nodes, job_idx))

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

        Step 1 loops internally: once a head starts (consuming some free
        nodes), the *new* front of the queue is immediately re-checked and
        started too if it fits. It must NOT be evaluated as a backfill
        candidate against a reservation_time computed before the earlier
        head started -- that stale reservation_time is what caused jobs to
        be blocked even when free resources were actually available for them.

        free_nodes is computed once here and decremented incrementally as
        jobs start, rather than calling get_free_nodes() (an O(len(running))
        sum) on every loop iteration.
        """
        free_nodes = self.get_free_nodes()
        started = False

        # Step 1: keep starting the front of the queue as long as it fits.
        while self.wait_queue and self.wait_queue[0].nodes <= free_nodes:
            head = self.wait_queue.pop(0)
            self.log(f"Starting FCFS head job {head.idx}")
            self.start_job(head)
            free_nodes -= head.nodes
            started = True

        if not self.wait_queue:
            return started

        # Step 2/3: the head is now genuinely blocked - compute one
        # reservation and try to backfill the rest of the queue against it.
        # free_nodes still holds the correct current value from Step 1.
        reservation_time = self.calculate_reservation_time()
        head_within_horizon = (
            reservation_time is not None and
            reservation_time - self.current_time <= self.planning_horizon)

        # Position 0 is the head; the loop above just proved it's blocked
        # (that's why we're here), so it stays parked at the front.
        # enumerate() gives a lightweight view over the existing list (no
        # copy) - unlike wait_queue[1:], which would allocate a new list.
        # Safe to iterate self.wait_queue directly here since nothing
        # mutates it during the loop; we only build the separate
        # `remaining` list and reassign self.wait_queue after the loop ends.
        remaining = [self.wait_queue[0]]
        # Queue depth: the planned head takes one slot, and every job behind
        # it that is visited takes one, whether or not it can start.
        examined = 1 if head_within_horizon else 0
        for i, job in enumerate(self.wait_queue):
            if i == 0:
                continue  # head - already handled above, skip
            if self.backfill_queue_depth and examined >= self.backfill_queue_depth:
                remaining.extend(self.wait_queue[i:])
                break
            examined += 1
            if job.nodes <= free_nodes:
                # Tested on its projected end; the release delay is not
                # planned for, the same as for the reservation above.
                job_end = self.current_time + job.duration
                if reservation_time is not None and (
                        not head_within_horizon or job_end < reservation_time):
                    # Backfiller - only if it completes before reservation
                    self.log(f"Backfilling job {job.idx} (ends at {job_end} < reservation {reservation_time})")
                    self.start_job(job)
                    free_nodes -= job.nodes
                    started = True
                    continue  # started - don't keep it in the queue
                else:
                    # Would interfere with FCFS head reservation
                    self.log(f"Blocking job {job.idx} (would end at {job_end} >= reservation {reservation_time})")
            remaining.append(job)

        self.wait_queue = remaining
        return started

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
        """Handle job submission: just enqueue it. The scheduling decision
        (does it fit, can it backfill) is made uniformly by try_start_jobs(),
        called once after all same-timestamp events are drained - not here.
        Making the decision here, immediately, was the bug: when multiple
        jobs completed at the same timestamp as this submission, each
        completion triggered its own try_start_jobs() call before the next
        completion at the same instant was known about, letting backfill
        candidates race ahead and consume capacity that should have gone
        to the earlier-priority head first, once the full combined freed
        capacity was known.
        """
        self.log(f"Job {job.idx} submitted (nodes={job.nodes}, duration={job.duration})")
        self.wait_queue.append(job)

    def handle_end(self, job_idx: int):
        """Handle job completion: free its resources only. Does not call
        try_start_jobs() itself - see handle_submit()'s docstring for why
        scheduling decisions are deferred to a single batched call after
        all same-timestamp events (multiple jobs can complete at the exact
        same instant) are drained."""
        if job_idx not in self.running:
            return

        start_time, actual_end, pessimistic_end, nodes = self.running.pop(job_idx)
        self.log(f"Job {job_idx} completed")
        if self.release_delay > 0:
            # Block release: all nodes return together after the delay.
            release_time = actual_end + self.release_delay
            self.releasing[job_idx] = (release_time, nodes)
            heapq.heappush(self.events, Event(release_time, 'RELEASE', job_idx))

        self.record_resource_state()

    def handle_release(self, job_idx: int):
        """Handle a node release: an ended job's nodes become reusable.
        Like handle_end(), scheduling is deferred to the batched call."""
        if job_idx not in self.releasing:
            return
        self.releasing.pop(job_idx)
        self.log(f"Job {job_idx} nodes released")

        self.record_resource_state()

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

            # Drain every event at this exact same timestamp before making
            # any scheduling decision - see handle_submit()/handle_end()'s
            # docstrings for why this matters. Event.__lt__ already orders
            # END before SUBMIT for ties at the same timestamp, so this
            # loop naturally processes all completions (freeing capacity)
            # before any arrivals at the same instant are enqueued.
            same_time_events = [event]
            while self.events and self.events[0].time == self.current_time:
                same_time_events.append(heapq.heappop(self.events))

            # heapq.heappop already returns these in Event.__lt__ order
            # (time, then END-before-SUBMIT, then job_idx), so this sort
            # is a no-op in practice - but making the ordering explicit
            # here means correctness doesn't rely on a reader trusting
            # that heap-pop order coincides with intended processing
            # order, and stays correct even if event storage is later
            # refactored away from a heap.
            same_time_events.sort()

            for evt in same_time_events:
                if evt.type == 'SUBMIT':
                    self.handle_submit(job_map[evt.job_idx])
                elif evt.type == 'END':
                    self.handle_end(evt.job_idx)
                elif evt.type == 'RELEASE':
                    self.handle_release(evt.job_idx)

            # The simulator calls schedule() again at the same time step
            # while a call starts jobs; each call is a fresh pass with its
            # own queue depth. Without a depth a repeat starts nothing.
            while self.try_start_jobs():
                pass

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

            # Check for actual_run_time column (for early completion tests)
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

def write_resource_trace(jobs: List[Job], filename: str, total_nodes: int,
                         release_delay: float = 0.0):
    """Generate resource trace from job trace"""
    # Collect all events (start, end, and node release). The numeric order
    # key matches the simulator: end, release, start at one timestamp.
    events = []
    for job in jobs:
        if job.start_time is not None:
            events.append((job.start_time, 2, job.nodes, 'START'))
        if job.end_time is not None:
            events.append((job.end_time, 0, job.nodes, 'END'))
            if release_delay > 0:
                events.append((job.end_time + release_delay, 1, job.nodes,
                               'RELEASE'))

    # Sort by time
    events.sort()

    # Track resource state changes
    allocated = 0
    releasing = 0
    resource_history = []

    for time, _, nodes, event_type in events:
        if event_type == 'START':
            allocated += nodes
        elif event_type == 'END':
            allocated -= nodes
            if release_delay > 0:
                releasing += nodes
        else:
            releasing -= nodes
        free = total_nodes - allocated - releasing
        resource_history.append((time, free, allocated, releasing))

    # Write resource trace; the releasing column appears only with a delay,
    # matching the simulator's own header.
    with open(filename, 'w', newline='') as f:
        writer = csv.writer(f)
        if release_delay > 0:
            writer.writerow(['time', 'free_nodes', 'allocated_nodes',
                             'releasing_nodes'])
            for row in resource_history:
                writer.writerow(row)
        else:
            writer.writerow(['time', 'free_nodes', 'allocated_nodes'])
            for time, free, allocated, _ in resource_history:
                writer.writerow([time, free, allocated])

def main():
    if len(sys.argv) < 2:
        print("Usage: python_reference_scheduler.py <trace.csv> [--verbose] [--nodes N] [--outdir DIR] [--release-delay SECONDS] [--planning-horizon SECONDS] [--backfill-queue-depth N]")
        print()
        print("Python reference EASY backfilling scheduler for verification")
        print()
        print("Options:")
        print("  --verbose    Show detailed simulation progress")
        print("  --nodes N    Total nodes in system (default: 1000)")
        print("  --outdir DIR Output directory for generated files (default: /tmp)")
        print("  --release-delay SECONDS")
        print("               Constant node release delay: an ended job's nodes")
        print("               stay unavailable this long (default: 0). Matches")
        print("               the simulator's --node_release_delay with the")
        print("               constant model.")
        print("  --planning-horizon SECONDS")
        print("               Do not plan a head reservation starting more than")
        print("               this far ahead; candidates are then not tested")
        print("               against it. Matches the simulator's")
        print("               --planning_horizon (default: unlimited).")
        print("  --backfill-queue-depth N")
        print("               Examine at most N waiting jobs per backfill pass,")
        print("               head included. Matches the simulator's")
        print("               --backfill_queue_depth (default: 0 = unlimited).")
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

    # Parse the constant node release delay
    release_delay = 0.0
    if '--release-delay' in sys.argv:
        idx = sys.argv.index('--release-delay')
        release_delay = float(sys.argv[idx + 1])
        if release_delay < 0:
            print("--release-delay must be nonnegative", file=sys.stderr)
            sys.exit(1)

    planning_horizon = float('inf')
    if '--planning-horizon' in sys.argv:
        idx = sys.argv.index('--planning-horizon')
        planning_horizon = float(sys.argv[idx + 1])
        if planning_horizon < 0:
            print("--planning-horizon must be nonnegative", file=sys.stderr)
            sys.exit(1)

    backfill_queue_depth = 0
    if '--backfill-queue-depth' in sys.argv:
        idx = sys.argv.index('--backfill-queue-depth')
        backfill_queue_depth = int(sys.argv[idx + 1])
        if backfill_queue_depth < 0:
            print("--backfill-queue-depth must be nonnegative", file=sys.stderr)
            sys.exit(1)

    print(f"=== Python Reference EASY Backfilling Scheduler ===", file=sys.stderr)
    print(f"Trace: {trace_file}", file=sys.stderr)
    print(f"Total nodes: {total_nodes}", file=sys.stderr)
    print(f"Output directory: {outdir}", file=sys.stderr)
    print(f"Node release delay: {release_delay}", file=sys.stderr)
    print(f"Planning horizon: {planning_horizon}", file=sys.stderr)
    print(f"Queue depth: {backfill_queue_depth or 'all'}", file=sys.stderr)
    print(f"Verbose: {verbose}", file=sys.stderr)
    print(file=sys.stderr)

    # Load trace
    jobs = load_trace(trace_file)
    print(f"Loaded {len(jobs)} jobs", file=sys.stderr)

    # Run scheduler
    scheduler = EasyBackfillingScheduler(total_nodes, verbose=verbose,
                                         release_delay=release_delay,
                                         planning_horizon=planning_horizon,
                                         backfill_queue_depth=backfill_queue_depth)
    jobs = scheduler.simulate(jobs)

    # Write output files to specified directory
    basename = os.path.basename(trace_file).replace('.csv', '')
    output_file = os.path.join(outdir, f'{basename}_reference.csv')
    write_scheduler_output(jobs, output_file)

    # Write resource trace
    resource_file = os.path.join(outdir, f'{basename}_reference_resources.csv')
    write_resource_trace(jobs, resource_file, total_nodes, release_delay)

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
