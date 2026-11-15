#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

"""
Python Reference Exact-Placement Periodic Backfilling Scheduler

Independent reference for whole-node placement with event-driven FCFS
dispatch and periodic hybrid backfill, written to cross-check DR_EVT against
a default-configured production scheduler that plans future starts without
holding running jobs in its reservation profile.

IMPORTANT: This script writes output files to /tmp by default to avoid
polluting the working directory. Use --outdir to specify a different location.

Scope: one homogeneous pool of numbered exclusive nodes, equal node weights,
FCFS order, constant capacity, no node-release delay. Times are used as given;
pass absolute timestamps when the compared scheduler aligns its release
grouping to an absolute clock.

Algorithm:
1. Arrivals join the FCFS queue. Completions free their exact nodes.
2. After every event batch the queue head starts while it fits on idle nodes
   (no backfill between timer ticks).
3. On each periodic tick, a fresh reservation profile is built from waiting
   jobs only, then up to Q queued jobs are examined in order:
   - Usable nodes are those free in every profile segment touching the job's
     requested window, including its end instant.
   - A job that fits now on idle usable nodes starts immediately and its
     window is booked so later candidates see it.
   - Otherwise running jobs are released in time-limit order in widening
     groups (30 s, then doubling), grid-aligned on the input clock, until the
     job fits. Nodes are picked by contiguous best fit; the planned start is
     the latest limit among released jobs whose nodes were picked.
   - A planned window that collides with an earlier booking is retried from
     a later profile boundary.
   - The first R planned jobs are booked; later ones may only start now.

Node choice ("contiguous best fit"): take the shortest run of consecutive
free node IDs that covers the remaining request; if none does, consume the
longest run; ties go to the lowest ID.
"""

import csv
import heapq
import os
import sys
from dataclasses import dataclass, field
from typing import List, Optional, Set

INITIAL_GROUP = 30          # first release-grouping width, seconds
GROUP_CAP = 365 * 86400     # stop widening past one year


@dataclass
class Job:
    """Job record"""
    idx: int
    submit_time: float
    nodes: int
    duration: float  # time limit used for planning
    actual_run_time: Optional[float] = None

    start_time: Optional[float] = None
    end_time: Optional[float] = None
    node_ids: List[int] = field(default_factory=list)
    rejected: bool = False

    def __post_init__(self):
        if self.actual_run_time is None:
            self.actual_run_time = self.duration


@dataclass
class Event:
    """Simulation event"""
    time: float
    type: str  # 'END', 'SUBMIT', or 'TICK'
    job_idx: int

    def __lt__(self, other):
        if self.time != other.time:
            return self.time < other.time
        if self.type != other.type:
            order = {'END': 0, 'SUBMIT': 1, 'TICK': 2}
            return order[self.type] < order[other.type]
        return self.job_idx < other.job_idx


def contiguous_best_fit(free: Set[int], count: int) -> Optional[List[int]]:
    """Pick `count` IDs from `free` by contiguous-run best fit."""
    runs: List[List[int]] = []
    for node in sorted(free):
        if runs and runs[-1][-1] == node - 1:
            runs[-1].append(node)
        else:
            runs.append([node])
    picked: List[int] = []
    needed = count
    while needed > 0:
        choice, choice_covers = None, False
        for run in runs:
            if not run:
                continue
            covers = len(run) >= needed
            if (choice is None or (covers and not choice_covers)
                    or (covers and choice_covers and len(run) < len(choice))
                    or (not covers and not choice_covers
                        and len(run) > len(choice))):
                choice, choice_covers = run, covers
        if choice is None:
            return None
        taken = choice[:needed]
        picked.extend(taken)
        del choice[:len(taken)]
        needed -= len(taken)
    return sorted(picked)


class ReservationProfile:
    """Contiguous time segments [begin, end) with the node IDs still free."""

    def __init__(self, now: float, until: float, pool: Set[int]):
        self.pool = frozenset(pool)
        self.segments = [[now, until, frozenset(pool)]]

    def book(self, begin: float, end: float, nodes: Set[int]) -> None:
        """Remove `nodes` from every segment within [begin, end)."""
        segs = self.segments
        begin = max(begin, segs[0][0])
        end = max(end, begin + 1)
        # Split at `begin`.
        before = 0
        k = 0
        found = False
        while k < len(segs):
            if segs[k][1] > begin:
                segs.insert(k + 1, [begin, segs[k][1], segs[k][2]])
                segs[k][1] = begin
                found = True
                break
            if segs[k][1] == begin:
                found = True
                break
            before = k
            k += 1
        after = -1
        while found:
            k += 1
            if k >= len(segs):
                break
            if end < segs[k][1]:
                segs.insert(k + 1, [end, segs[k][1], segs[k][2]])
                segs[k][1] = end
            segs[k][2] = segs[k][2] - nodes
            if end == segs[k][1]:
                if k + 1 < len(segs):
                    after = k + 1
                break
        # Coalesce at most one adjacent pair with identical free sets.
        k = before
        while k != after:
            nxt = k + 1
            if nxt >= len(segs):
                break
            if segs[k][2] != segs[nxt][2]:
                k = nxt
                continue
            segs[k][1] = segs[nxt][1]
            del segs[nxt]
            break

    def collides(self, nodes: Set[int], begin: float, end: float) -> bool:
        """True if any segment overlapping (begin, end) lacks some of `nodes`."""
        return any(e > begin and b < end and not nodes <= free
                   for b, e, free in self.segments)

    def usable(self, earliest: float, finish: float):
        """Intersect free sets over [earliest, finish] and find a retry point.

        Returns (usable_nodes, retry_at). retry_at is the first boundary after
        `earliest` at which the next segment frees nodes the current one does
        not, or a later boundary when the window ends before the profile does;
        0 means no later attempt can change the answer.
        """
        segs = self.segments
        usable = self.pool
        retry_at = 0
        for k, (b, e, free) in enumerate(segs):
            if e > earliest and k + 1 < len(segs) and retry_at == 0:
                if not (self.pool & segs[k + 1][2]) <= (usable & free):
                    retry_at = e
            if e <= earliest:
                continue
            if b <= finish:
                usable = usable & free
            else:
                if retry_at == 0 and k + 2 < len(segs):
                    retry_at = segs[k + 1][1]
                break
        return usable, retry_at


class PlacementBackfillingScheduler:
    """Reference exact-placement scheduler (see module docstring)."""

    def __init__(self, total_nodes: int, reservation_depth: int = 32,
                 backfill_queue_depth: int = 64, backfill_interval: float = 30.0,
                 planning_window: float = 4320 * 60, verbose: bool = False):
        self.total_nodes = total_nodes
        self.reservation_depth = reservation_depth
        self.backfill_queue_depth = backfill_queue_depth
        self.backfill_interval = backfill_interval
        self.planning_window = planning_window
        self.verbose = verbose

        self.current_time = 0.0
        self.wait_queue: List[Job] = []
        self.idle: Set[int] = set(range(total_nodes))
        # job_idx -> (limit_end, nodes)
        self.running = {}
        self.events: List[Event] = []

    def log(self, msg: str):
        if self.verbose:
            print(f"[t={self.current_time:.0f}] {msg}", file=sys.stderr)

    def start_job(self, job: Job, nodes: List[int]):
        job.start_time = self.current_time
        job.end_time = self.current_time + job.actual_run_time
        job.node_ids = nodes
        chosen = frozenset(nodes)
        self.idle -= chosen
        self.running[job.idx] = (self.current_time + job.duration, chosen)
        self.wait_queue.remove(job)
        heapq.heappush(self.events, Event(job.end_time, 'END', job.idx))
        self.log(f"start job {job.idx} on {len(nodes)} nodes")

    def dispatch_head(self):
        """Start queue heads in order while they fit on idle nodes."""
        while self.wait_queue and self.wait_queue[0].nodes <= len(self.idle):
            head = self.wait_queue[0]
            self.start_job(head, contiguous_best_fit(self.idle, head.nodes))

    def grouped_release_fit(self, job: Job, usable: Set[int], earliest: float,
                            bound: float):
        """Earliest (start, nodes) by releasing running jobs in widening groups.

        `bound` (nonzero) stops releasing jobs whose limit ends after it.
        """
        now = self.current_time
        candidates = usable & self.idle
        if earliest <= now and len(candidates) >= job.nodes:
            return now, contiguous_best_fit(candidates, job.nodes)
        ordered = sorted(self.running.values(), key=lambda r: r[0])
        pending = []
        for limit_end, nodes in ordered:
            if earliest > now and limit_end <= earliest:
                candidates |= nodes & usable
            else:
                pending.append((limit_end, nodes))
        if earliest > now and len(candidates) >= job.nodes:
            return now, contiguous_best_fit(candidates, job.nodes)
        width = INITIAL_GROUP
        group_edge = None
        released = []
        k = 0
        while True:
            took_any = False
            exhausted = True
            while True:
                if k >= len(pending) or (bound and pending[k][0] > bound):
                    break
                limit_end, nodes = pending[k]
                k += 1
                if not nodes & usable:
                    continue
                if group_edge is None:
                    group_edge = limit_end + (width - limit_end % width)
                candidates |= nodes & usable
                released.append((limit_end, nodes))
                took_any = True
                if k >= len(pending):
                    break
                if pending[k][0] > group_edge + width:
                    exhausted = False
                    break
            if not took_any:
                return None, None
            if len(candidates) >= job.nodes:
                picked = contiguous_best_fit(candidates, job.nodes)
                chosen = set(picked)
                start = now
                for limit_end, nodes in reversed(released):
                    if nodes & chosen:
                        start = max(now, limit_end)
                        break
                return start, picked
            if exhausted:
                return None, None
            upcoming = pending[k][0]
            while width <= GROUP_CAP:
                width *= 2
                if not upcoming > group_edge + width:
                    break

    def plan_job(self, profile: ReservationProfile, job: Job, now_only: bool):
        """Find (start, nodes) for one examined job, or (None, None)."""
        now = self.current_time
        retry = now
        while True:
            earliest = retry
            finish = job.duration + max(earliest, now)
            usable, retry = profile.usable(earliest, finish)
            if len(usable) < job.nodes:
                if retry and not now_only:
                    continue
                return None, None
            if now_only:
                if earliest > now:
                    return None, None
                free = usable & self.idle
                if len(free) < job.nodes:
                    return None, None
                return now, contiguous_best_fit(free, job.nodes)
            start, picked = self.grouped_release_fit(job, usable, earliest, retry)
            if start is None:
                if retry:
                    continue
                return None, None
            start = max(start, earliest)
            if start > now and profile.collides(frozenset(picked), start,
                                                start + job.duration):
                retry = start + (1 if start == earliest else 0)
                continue
            if retry and start > retry:
                continue
            return start, picked

    def backfill_cycle(self):
        now = self.current_time
        profile = ReservationProfile(now, now + self.planning_window,
                                     set(range(self.total_nodes)))
        booked = 0
        examined = self.wait_queue[:self.backfill_queue_depth or None]
        for job in list(examined):
            now_only = bool(self.reservation_depth) and booked >= self.reservation_depth
            start, picked = self.plan_job(profile, job, now_only)
            if start is None:
                continue
            if start <= now:
                self.start_job(job, picked)
                profile.book(now, now + job.duration, frozenset(picked))
                continue
            if now_only:
                continue
            profile.book(start, start + job.duration, frozenset(picked))
            booked += 1

    def simulate(self, jobs: List[Job]) -> List[Job]:
        for job in jobs:
            heapq.heappush(self.events, Event(job.submit_time, 'SUBMIT', job.idx))
        origin = min((job.submit_time for job in jobs), default=0.0)
        if self.backfill_interval > 0.0:
            heapq.heappush(self.events,
                           Event(origin + self.backfill_interval, 'TICK', -1))
        job_map = {job.idx: job for job in jobs}

        while self.events:
            event = heapq.heappop(self.events)
            self.current_time = event.time
            batch = [event]
            while self.events and self.events[0].time == self.current_time:
                batch.append(heapq.heappop(self.events))
            batch.sort()
            for evt in batch:
                if evt.type == 'END':
                    _, nodes = self.running.pop(evt.job_idx)
                    self.idle |= nodes
                elif evt.type == 'SUBMIT':
                    job = job_map[evt.job_idx]
                    if job.nodes > self.total_nodes or job.nodes <= 0:
                        job.rejected = True
                        continue
                    self.wait_queue.append(job)
            self.dispatch_head()
            tick = any(evt.type == 'TICK' for evt in batch)
            if tick:
                self.backfill_cycle()
                if self.wait_queue or self.running or any(
                        e.type == 'SUBMIT' for e in self.events):
                    heapq.heappush(self.events, Event(
                        self.current_time + self.backfill_interval, 'TICK', -1))
        return list(job_map.values())


def load_trace(filename: str) -> List[Job]:
    """Load job trace from CSV"""
    jobs = []
    with open(filename, 'r', newline='') as f:
        for idx, row in enumerate(csv.DictReader(f)):
            limit = float(row.get('time_limit', row.get('duration', 100)))
            actual = row.get('actual_run_time')
            jobs.append(Job(
                idx=idx,
                submit_time=float(row.get('job_submit_time', row.get('submit_time', 0))),
                nodes=int(row.get('num_nodes', row.get('nodes', 1))),
                duration=limit,
                actual_run_time=float(actual) if actual else limit))
    return jobs


def write_scheduler_output(jobs: List[Job], filename: str):
    """Write results comparable to DR_EVT, plus selected node IDs."""
    with open(filename, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['job_idx', 'submit_time', 'start_time', 'end_time',
                         'nodes', 'duration', 'node_ids'])
        for job in jobs:
            writer.writerow([job.idx, job.submit_time,
                             '' if job.start_time is None else job.start_time,
                             '' if job.end_time is None else job.end_time,
                             job.nodes, job.duration,
                             ';'.join(map(str, job.node_ids))])


def option(name, default, cast):
    if name in sys.argv:
        return cast(sys.argv[sys.argv.index(name) + 1])
    return default


def main():
    if len(sys.argv) < 2:
        print("Usage: python_periodic_backfill_placement_scheduler.py <trace.csv> [--verbose] "
              "[--nodes N] [--outdir DIR] [--reservation-depth N] "
              "[--backfill-queue-depth N] [--backfill-interval SECONDS]")
        print()
        print("Python reference exact-placement periodic backfilling scheduler")
        print()
        print("Options:")
        print("  --nodes N    Total nodes, numbered 0..N-1 (default: 756)")
        print("  --outdir DIR Output directory (default: /tmp)")
        print("  --reservation-depth N  Booked waiting jobs per cycle (default: 32)")
        print("  --backfill-queue-depth N  Examined jobs per cycle (default: 64; 0 = all)")
        print("  --backfill-interval SECONDS  Backfill tick period (default: 30)")
        print("Times are used as given; supply absolute timestamps when the")
        print("compared scheduler groups releases on an absolute clock.")
        sys.exit(1)

    trace_file = sys.argv[1]
    total_nodes = option('--nodes', 756, int)
    outdir = option('--outdir', '/tmp', str)
    reservation_depth = option('--reservation-depth', 32, int)
    queue_depth = option('--backfill-queue-depth', 64, int)
    interval = option('--backfill-interval', 30.0, float)
    if total_nodes <= 0 or reservation_depth < 0 or queue_depth < 0 or interval <= 0:
        raise ValueError('nodes and interval must be positive; depths nonnegative')

    jobs = load_trace(trace_file)
    scheduler = PlacementBackfillingScheduler(
        total_nodes, reservation_depth, queue_depth, interval,
        verbose='--verbose' in sys.argv)
    jobs = scheduler.simulate(jobs)

    basename = os.path.basename(trace_file).replace('.csv', '')
    output_file = os.path.join(outdir, f'{basename}_periodic_backfill_placement_reference.csv')
    write_scheduler_output(jobs, output_file)
    started = sum(job.start_time is not None for job in jobs)
    rejected = sum(job.rejected for job in jobs)
    print(f"Reference output written to: {output_file}", file=sys.stderr)
    print(f"Jobs started: {started}/{len(jobs)}"
          + (f" ({rejected} rejected)" if rejected else ""), file=sys.stderr)


if __name__ == '__main__':
    main()
