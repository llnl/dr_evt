#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

"""
Python Reference HYBRID/CONSERVATIVE Backfilling Scheduler

Pure implementation of CONSERVATIVE backfilling algorithm for verification.
No dependencies, no fancy features - just correct CONSERVATIVE logic.

IMPORTANT: This script writes output files to /tmp by default to avoid
polluting the working directory. Use --outdir to specify a different location.

Limitation: the reference has no capacity schedule. Capacity is constant for
the whole run, so the simulator's capacity-aware reservations
(--capacity_schedule) are checked by its native tests, not by comparison with
this script.

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
import bisect
import math
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
    # True when the job can never fit the machine; it is never enqueued.
    rejected: bool = False

    def __post_init__(self):
        # Default actual_run_time to duration if not specified
        if self.actual_run_time is None:
            self.actual_run_time = self.duration

@dataclass
class Event:
    """Simulation event"""
    time: float
    type: str  # 'SUBMIT', 'START', 'END', or 'TICK'
    job_idx: int

    def __lt__(self, other):
        # Sort by time first, then END/SUBMIT before periodic scheduling.
        if self.time != other.time:
            return self.time < other.time
        if self.type != other.type:
            type_order = {'END': 0, 'START': 1, 'SUBMIT': 2, 'TICK': 3}
            return type_order[self.type] < type_order[other.type]
        return self.job_idx < other.job_idx

class HybridBackfillingScheduler:
    """
    Minimal HYBRID/CONSERVATIVE backfilling scheduler

    This is a REFERENCE implementation - kept simple and obvious
    so we can manually verify it's correct.
    """

    def __init__(self, total_nodes: int, verbose: bool = False,
                 reservation_depth: int = 0, backfill_queue_depth: int = 0,
                 backfill_interval: float = 0.0,
                 planning_time_resolution: float = 0.0,
                 planning_horizon: float = float('inf')):
        self.total_nodes = total_nodes
        self.verbose = verbose
        # Waiting jobs that hold reservations; 0 means every waiting job,
        # 1 reproduces EASY (hybrid depth as in Slurm and Fluxion).
        self.reservation_depth = reservation_depth
        # Waiting jobs examined at one scheduling timestamp; 0 means unlimited.
        self.backfill_queue_depth = backfill_queue_depth
        self.backfill_depth_time = None
        self.backfill_depth_examined = 0
        self.backfill_depth_head_accounted = False
        # Zero preserves event-driven backfill. A positive interval runs FCFS
        # dispatch on ordinary events and backfill only at ticks.
        self.backfill_interval = backfill_interval
        # Planning timestamps are rounded only after their start is known.
        self.planning_time_resolution = planning_time_resolution
        # Reservations outside this periodic planning window are omitted.
        self.planning_horizon = planning_horizon

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

    def planned_end(self, start_time: float, duration: float) -> float:
        """Return the projected end timestamp on the planning grid."""
        end_time = start_time + duration
        if self.planning_time_resolution:
            end_time = (math.ceil(end_time / self.planning_time_resolution) *
                        self.planning_time_resolution)
        return end_time

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
        # This is a point-in-time capacity query: start at the segment which
        # contains current_time rather than rechecking older segments.  It
        # mirrors Reservation_Timeline::earliest_fit(..., duration=0) in the
        # native EASY and conservative schedulers.
        segment = bisect.bisect_right(
            timeline, (self.current_time, float('inf'))) - 1
        for index in range(segment, len(timeline)):
            time, available_nodes = timeline[index]
            if available_nodes >= job.nodes:
                return max(self.current_time, time)

        # Should never reach here if system is not oversubscribed
        return self.current_time

    def build_resource_timeline_from_running(self, running: dict) -> list:
        """Return projected free capacity after each estimated release."""
        current_free = self.total_nodes - sum(
            nodes for _, _, _, nodes in running.values())
        releases = sorted((pessimistic_end, nodes)
                          for _, _, pessimistic_end, nodes in running.values())
        timeline = [(self.current_time, current_free)]
        for release_time, released_nodes in releases:
            current_free += released_nodes
            timeline.append((release_time, current_free))
        return timeline

    @staticmethod
    def reserve_in_timeline(timeline: list, start: float, end: float,
                            nodes: int) -> None:
        """Reserve nodes over [start, end) in a free-capacity timeline."""
        def split(time: float) -> int:
            index = bisect.bisect_left(timeline, (time,))
            if index < len(timeline) and timeline[index][0] == time:
                return index
            # All reservations begin no earlier than current_time, which is
            # the first timeline segment.
            timeline.insert(index, (time, timeline[index - 1][1]))
            return index

        start_index = split(start)
        end_index = split(end)
        for index in range(start_index, end_index):
            time, available = timeline[index]
            timeline[index] = (time, available - nodes)

    def calculate_conservative_window(self, backfill_pos: int,
                                      timeline: list) -> Optional[float]:
        """
        Calculate the conservative backfill window considering effective running jobs

        Returns the earliest reservation time among all jobs ahead of backfill_pos
        ``timeline`` includes already-started backfill jobs.  The protected
        plan is incrementally extended across the queue scan, just like the
        native scheduler; rebuilding it after every accepted candidate makes
        large conservative traces needlessly cubic.
        """
        if backfill_pos >= len(self.wait_queue):
            return None

        # Keep a private free-node profile for this pass.  Each protected job
        # consumes its complete planned interval before the next reservation
        # is found; adding only a future release would leave its nodes free.
        # Candidates are visited in queue order, so retain and extend this
        # prefix plan for the whole scan.  Accepted backfills are inserted in
        # place below, preserving its validity without rebuilding prefixes.
        cache_key = id(timeline)
        cache = getattr(self, '_conservative_plan_cache', {})
        cached = cache.get(cache_key)
        if cached is None:
            cached = {
                'plan': list(timeline), 'processed': 0, 'reserved': 0,
                'earliest': float('inf'), 'excluded': set(),
            }
            cache[cache_key] = cached
            self._conservative_plan_cache = cache
        plan = cached['plan']

        def earliest_fit(job: Job) -> Optional[float]:
            start = self.current_time
            while True:
                segment = bisect.bisect_right(plan, (start, float('inf'))) - 1
                end = self.planned_end(start, job.duration)
                i = segment
                while i < len(plan) and (i == segment or plan[i][0] < end):
                    if plan[i][1] < job.nodes:
                        break
                    i += 1
                if i == len(plan) or (i != segment and plan[i][0] >= end):
                    return start
                while i < len(plan) and plan[i][1] < job.nodes:
                    i += 1
                if i == len(plan):
                    return None
                start = plan[i][0]

        def reserve(start: float, job: Job) -> None:
            self.reserve_in_timeline(
                plan, start, self.planned_end(start, job.duration), job.nodes)

        # Check the reserved jobs ahead of this position: all of them, or
        # only the first reservation_depth when a depth is configured
        for i in range(cached['processed'], backfill_pos):
            if i >= len(self.wait_queue):
                break
            job = self.wait_queue[i]

            # Skip jobs that were already backfilled
            if job.idx in cached['excluded']:
                continue
            if (self.reservation_depth and
                    cached['reserved'] >= self.reservation_depth):
                break

            reservation = earliest_fit(job)
            if reservation is None:
                continue
            if reservation - self.current_time > self.planning_horizon:
                continue
            cached['reserved'] += 1
            cached['earliest'] = min(cached['earliest'], reservation)
            reserve(reservation, job)

        cached['processed'] = backfill_pos

        return (cached['earliest'] if cached['earliest'] < float('inf')
                else None)

    def reserve_backfill_in_plans(self, job: Job, timeline: list) -> None:
        """Extend availability and protected plans after starting a backfill."""
        end = self.planned_end(self.current_time, job.duration)
        self.reserve_in_timeline(timeline, self.current_time, end, job.nodes)
        cached = self._conservative_plan_cache.get(id(timeline))
        if cached is not None:
            self.reserve_in_timeline(cached['plan'], self.current_time, end,
                                     job.nodes)
            cached['excluded'].add(job.idx)

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

    def try_start_jobs(self, allow_backfill: bool = True):
        """
        Try to start jobs from wait queue

        CONSERVATIVE algorithm:
        1. All jobs in queue get reservations
        2. Start any jobs from the front that fit now (cascading)
        3. Other jobs can backfill if they fit AND finish before ANY waiting job's reservation
        """
        # Reservation-prefix caches are valid only for this scheduling pass.
        self._conservative_plan_cache = {}
        free_nodes = self.get_free_nodes()
        started = False

        # Step 1: Start jobs from the front as long as they fit
        while self.wait_queue and self.wait_queue[0].nodes <= free_nodes:
            head = self.wait_queue.pop(0)
            self.log(f"Starting FCFS head job {head.idx}")
            self.start_job(head)
            free_nodes -= head.nodes
            started = True

        if not self.wait_queue:
            return started

        if not allow_backfill:
            return started

        # Step 2: Try conservative backfilling
        # For each job in the queue (after the head), check if it can backfill

        remaining = []
        # Build availability once and update it in place as backfills start.
        # This mirrors the native scheduler's reusable reservation plans.
        effective_timeline = self.build_resource_timeline_from_running(
            self.running)

        # Repeated scheduling passes at one timestamp share the same depth
        # budget. The blocked head is charged once, when it has an in-horizon
        # reservation; out-of-horizon jobs remain free to reconsider.
        if self.backfill_depth_time != self.current_time:
            self.backfill_depth_time = self.current_time
            self.backfill_depth_examined = 0
            self.backfill_depth_head_accounted = False
        for pos, job in enumerate(self.wait_queue):
            reservation = self.calculate_job_reservation_time_from_timeline(
                job, effective_timeline)
            # An out-of-horizon job stays queued and holds no reservation.
            # Event-driven scheduling follows Fluxion, which takes a job it
            # cannot reserve out of its scheduled count, so it is not charged
            # to queue depth; its reservation is recomputed on the next pass.
            # Periodic scheduling follows Slurm, which counts a tested job
            # toward bf_max_job_test before it finds the planned start beyond
            # bf_window, so such a job uses a depth slot.
            in_horizon = (reservation - self.current_time <=
                          self.planning_horizon)
            if not in_horizon and self.backfill_interval <= 0.0:
                remaining.append(job)
                continue
            if pos == 0 and self.backfill_depth_head_accounted:
                pass
            else:
                if (self.backfill_queue_depth and
                        self.backfill_depth_examined >= self.backfill_queue_depth):
                    remaining.extend(self.wait_queue[pos:])
                    break
                self.backfill_depth_examined += 1
                if pos == 0:
                    self.backfill_depth_head_accounted = True
            if not in_horizon:
                remaining.append(job)  # Counted, but cannot be backfilled.
                continue
            # Check if job fits in available resources
            if job.nodes > free_nodes:
                remaining.append(job)
                continue

            # Calculate the earliest reservation among jobs ahead.  Include
            # jobs already backfilled during this pass in the same projected
            # resource view used by the C++ implementation.
            conservative_window = self.calculate_conservative_window(
                pos, effective_timeline)

            if conservative_window is None:
                # No jobs ahead - shouldn't happen, but safe to backfill
                self.log(f"Backfilling job {job.idx} (no jobs ahead)")
                self.start_job(job)
                free_nodes -= job.nodes
                started = True
                self.reserve_backfill_in_plans(job, effective_timeline)
                continue

            # Job can backfill if it completes before the earliest reservation
            job_end = self.planned_end(self.current_time, job.duration)
            if job_end < conservative_window:
                self.log(f"Conservative backfill job {job.idx} (ends {job_end} < earliest reservation {conservative_window})")
                self.start_job(job)
                free_nodes -= job.nodes
                started = True

                self.reserve_backfill_in_plans(job, effective_timeline)
                continue
            else:
                self.log(f"Blocking job {job.idx} (would end {job_end} >= earliest reservation {conservative_window})")
                remaining.append(job)

        self.wait_queue = remaining
        return started

    def start_job(self, job: Job):
        """Start a job immediately"""
        job.start_time = self.current_time
        job.end_time = self.current_time + job.actual_run_time

        # Track both actual and pessimistic (scheduler's view) end times
        pessimistic_end = self.planned_end(self.current_time, job.duration)
        self.running[job.idx] = (self.current_time, job.end_time, pessimistic_end, job.nodes)

        # Schedule END event
        heapq.heappush(self.events, Event(job.end_time, 'END', job.idx))

        self.record_resource_state()

    def handle_submit(self, job: Job):
        """Handle job submission: enqueue it, or reject it when it can never fit"""
        if job.nodes > self.total_nodes:
            # Same rule as the C++ simulator: a job larger than the machine
            # never enters the wait queue. Leaving it there would block every
            # job behind it in FCFS order and, in periodic mode, keep the tick
            # loop alive forever.
            job.rejected = True
            print(f"Job {job.idx} rejected: requests {job.nodes} nodes, exceeds "
                  f"total nodes ({self.total_nodes}); this job can never be "
                  f"scheduled.", file=sys.stderr)
            return
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
        if self.backfill_interval > 0.0:
            heapq.heappush(self.events, Event(self.backfill_interval, 'TICK', -1))

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

            periodic_tick = any(evt.type == 'TICK' for evt in same_time_events)
            while self.try_start_jobs(allow_backfill=(self.backfill_interval == 0.0 or
                                                      periodic_tick)):
                pass

            if periodic_tick and (self.wait_queue or self.running or
                                  any(evt.type == 'SUBMIT' for evt in self.events)):
                heapq.heappush(self.events, Event(
                    self.current_time + self.backfill_interval, 'TICK', -1))

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
            time_limit = float(row.get('time_limit', row.get('duration', 100)))
            duration = time_limit

            # Check for actual_run_time column
            actual_run_time = None
            if 'actual_run_time' in row and row['actual_run_time']:
                actual_run_time = float(row['actual_run_time'])

            jobs.append(Job(
                idx=idx,
                submit_time=submit_time,
                nodes=nodes,
                duration=duration,
                actual_run_time=(time_limit if actual_run_time is None else
                                 actual_run_time)
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
        print("Usage: python_hybrid_scheduler.py <trace.csv> [--verbose] [--nodes N] [--outdir DIR] [--reservation-depth N] [--backfill-queue-depth N] [--backfill-interval SECONDS] [--planning-time-resolution SECONDS] [--planning-horizon SECONDS]")
        print()
        print("Python reference HYBRID/CONSERVATIVE backfilling scheduler for verification")
        print()
        print("Options:")
        print("  --verbose    Show detailed simulation progress")
        print("  --nodes N    Total nodes in system (default: 1000)")
        print("  --outdir DIR Output directory for generated files (default: /tmp)")
        print("  --reservation-depth N  Reserve for only the first N waiting jobs")
        print("               (default: 0 = all; 1 reproduces EASY)")
        print("  --backfill-queue-depth N  Examine at most N waiting jobs per pass")
        print("               (default: 0 = all)")
        print("  --backfill-interval SECONDS  Run backfill only on periodic ticks")
        print("               (default: 0 = event-driven backfill)")
        print("  --planning-time-resolution SECONDS  Round planning durations up")
        print("               to this grid (default: 0 = exact planning)")
        print("  --planning-horizon SECONDS  Limit event-driven and periodic reservations to this")
        print("               look-ahead (default: unlimited)")
        print()
        print("Generates reference output for HYBRID/CONSERVATIVE backfilling verification")
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

    # Parse reservation depth (0 = every waiting job holds a reservation)
    reservation_depth = 0
    if '--reservation-depth' in sys.argv:
        idx = sys.argv.index('--reservation-depth')
        reservation_depth = int(sys.argv[idx + 1])

    backfill_queue_depth = 0
    if '--backfill-queue-depth' in sys.argv:
        idx = sys.argv.index('--backfill-queue-depth')
        backfill_queue_depth = int(sys.argv[idx + 1])

    backfill_interval = 0.0
    if '--backfill-interval' in sys.argv:
        idx = sys.argv.index('--backfill-interval')
        backfill_interval = float(sys.argv[idx + 1])
        if backfill_interval < 0.0:
            raise ValueError('--backfill-interval must be nonnegative')

    planning_time_resolution = 0.0
    if '--planning-time-resolution' in sys.argv:
        idx = sys.argv.index('--planning-time-resolution')
        planning_time_resolution = float(sys.argv[idx + 1])
        if planning_time_resolution < 0.0:
            raise ValueError('--planning-time-resolution must be nonnegative')

    planning_horizon = float('inf')
    if '--planning-horizon' in sys.argv:
        idx = sys.argv.index('--planning-horizon')
        planning_horizon = float(sys.argv[idx + 1])
        if planning_horizon < 0.0:
            raise ValueError('--planning-horizon must be nonnegative')

    print(f"=== Python Reference HYBRID/CONSERVATIVE Backfilling Scheduler ===", file=sys.stderr)
    print(f"Trace: {trace_file}", file=sys.stderr)
    print(f"Total nodes: {total_nodes}", file=sys.stderr)
    print(f"Output directory: {outdir}", file=sys.stderr)
    print(f"Reservation depth: {reservation_depth or 'all'}", file=sys.stderr)
    print(f"Queue depth: {backfill_queue_depth or 'all'}", file=sys.stderr)
    print(f"Backfill interval: {backfill_interval or 'event-driven'}", file=sys.stderr)
    print(f"Planning resolution: {planning_time_resolution or 'exact'}", file=sys.stderr)
    print(f"Planning horizon: {planning_horizon if math.isfinite(planning_horizon) else 'unlimited'}", file=sys.stderr)
    print(f"Verbose: {verbose}", file=sys.stderr)
    print(file=sys.stderr)

    # Load trace
    jobs = load_trace(trace_file)
    print(f"Loaded {len(jobs)} jobs", file=sys.stderr)

    # Run scheduler
    scheduler = HybridBackfillingScheduler(total_nodes, verbose=verbose,
                                                 reservation_depth=reservation_depth,
                                                 backfill_queue_depth=backfill_queue_depth,
                                                 backfill_interval=backfill_interval,
                                                 planning_time_resolution=planning_time_resolution,
                                                 planning_horizon=planning_horizon)
    jobs = scheduler.simulate(jobs)

    # Write output files to specified directory
    basename = os.path.basename(trace_file).replace('.csv', '')
    output_file = os.path.join(outdir, f'{basename}_hybrid_reference.csv')
    write_scheduler_output(jobs, output_file)

    # Write resource trace
    resource_file = os.path.join(outdir, f'{basename}_hybrid_reference_resources.csv')
    write_resource_trace(jobs, resource_file, total_nodes)

    print(f"\nReference output written to: {output_file}", file=sys.stderr)
    print(f"Resource trace written to: {resource_file}", file=sys.stderr)
    print(f"Final simulation time: {scheduler.current_time}", file=sys.stderr)

    # Verify all admitted jobs completed; rejected jobs are reported apart.
    completed = sum(1 for job in jobs if job.start_time is not None)
    rejected = sum(1 for job in jobs if job.rejected)
    print(f"Jobs completed: {completed}/{len(jobs)}"
          + (f" ({rejected} rejected)" if rejected else ""), file=sys.stderr)

    if completed + rejected != len(jobs):
        print("\n⚠ WARNING: Not all jobs completed!", file=sys.stderr)
        for job in jobs:
            if job.start_time is None and not job.rejected:
                print(f"  Job {job.idx}: never started", file=sys.stderr)

if __name__ == '__main__':
    main()
