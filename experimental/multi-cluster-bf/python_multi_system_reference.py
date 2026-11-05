#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

"""
Python reference for FCFS scheduling over multiple systems with spatial
backfilling.

One FCFS wait queue is shared by K systems of (possibly unequal) capacity.
A job is bound to a system only when it starts.  At every scheduling pass:

1. While the queue head fits now on at least one system where it is
   admissible, start it on the system chosen by the placement policy.
2. Otherwise compute the head's projected start S_k on every admissible
   system (``+inf`` where it is not admissible), T* = min_k S_k, and
   k* = argmin_k S_k (lowest index on ties).  Scan the remaining jobs in
   FCFS order; a candidate may start on system k when it fits on k now and
   ``t + time_limit < W_k`` (strict), with the window W_k set by the
   spatial backfill policy:

   target_only    W_k = T* on k*, +inf elsewhere   (spatial EASY)
   every_system   W_k = S_k                          (spatial conservative)
   global_shadow  W_k = T* on every admissible system, +inf elsewhere

The resource model is isolated in ``CountResourceModel`` so that a
heterogeneous model can replace it without touching the policy code.

Outputs (written to --outdir, default /tmp):
  <basename>_msbf_reference.csv            per-job schedule with system
  <basename>_msbf_reference_resources.csv  per-system allocation timeline
"""

import argparse
import csv
import heapq
import math
import os
import sys
from typing import Dict, List, Optional, Sequence, Tuple

SPATIAL_POLICIES = ("target_only", "every_system", "global_shadow")
PLACEMENT_POLICIES = ("best_fit", "first_fit", "worst_fit")


class Job:
    """Job record."""

    def __init__(self, idx: int, submit_time: float, nodes: int,
                 duration: float, actual_run_time: Optional[float] = None):
        self.idx = idx
        self.submit_time = submit_time
        self.nodes = nodes
        self.duration = duration
        self.actual_run_time = (duration if actual_run_time is None
                                else actual_run_time)
        self.start_time: Optional[float] = None
        self.end_time: Optional[float] = None
        self.system: Optional[int] = None


class Event:
    """Simulation event, ordered END < SUBMIT at equal times."""

    ORDER = {"END": 0, "SUBMIT": 1}

    def __init__(self, time: float, event_type: str, job_idx: int):
        self.time = time
        self.type = event_type
        self.job_idx = job_idx

    def __lt__(self, other: "Event") -> bool:
        if self.time != other.time:
            return self.time < other.time
        if self.type != other.type:
            return self.ORDER[self.type] < self.ORDER[other.type]
        return self.job_idx < other.job_idx


class System:
    """One schedulable system or partition."""

    def __init__(self, name: str, capacity: int):
        self.name = name
        self.capacity = capacity


class CountResourceModel:
    """Homogeneous node-count model: one integer per system.

    This class is the seam between the policy and the resource
    representation.  A heterogeneous model implements the same methods
    with vector capacities and system-dependent demands.
    """

    def __init__(self, systems: Sequence[System]):
        self.systems = list(systems)
        self.capacity = [s.capacity for s in self.systems]
        self.used = [0 for _ in self.systems]

    def num_systems(self) -> int:
        return len(self.systems)

    def admissible(self, k: int, job: Job) -> bool:
        """True when the job can ever run on system k."""
        return job.nodes <= self.capacity[k]

    def demand(self, k: int, job: Job) -> int:
        """Resources the job consumes on system k."""
        return job.nodes

    def free(self, k: int) -> int:
        return self.capacity[k] - self.used[k]

    def fits_now(self, k: int, job: Job) -> bool:
        return self.demand(k, job) <= self.free(k)

    def leftover(self, k: int, job: Job) -> int:
        """Free resources on k after the job would start there."""
        return self.free(k) - self.demand(k, job)

    def earliest_fit(self, k: int, job: Job,
                     releases: Sequence[Tuple[float, int]],
                     now: float) -> float:
        """Earliest time at or after ``now`` when the job fits on k.

        ``releases`` lists (projected_end, demand) of jobs running on k.
        Returns +inf when the job is not admissible on k or the running
        jobs can never free enough.
        """
        if not self.admissible(k, job):
            return math.inf
        need = self.demand(k, job)
        available = self.free(k)
        if need <= available:
            return now
        for end_time, freed in sorted(releases):
            available += freed
            if available >= need:
                return end_time
        return math.inf

    def allocate(self, k: int, job: Job) -> None:
        self.used[k] += self.demand(k, job)

    def release(self, k: int, job: Job) -> None:
        self.used[k] -= self.demand(k, job)


class MultiSystemScheduler:
    """FCFS over K systems with spatial backfilling."""

    def __init__(self, model: CountResourceModel, spatial_backfill: str,
                 placement: str, backfill: bool = True,
                 verbose: bool = False):
        if spatial_backfill not in SPATIAL_POLICIES:
            raise ValueError("unknown spatial backfill policy: "
                             + spatial_backfill)
        if placement not in PLACEMENT_POLICIES:
            raise ValueError("unknown placement policy: " + placement)
        self.model = model
        self.spatial_backfill = spatial_backfill
        self.placement = placement
        self.backfill = backfill
        self.verbose = verbose

        self.current_time = 0.0
        self.wait_queue: List[Job] = []
        # job_idx -> (system, pessimistic_end, demand)
        self.running: Dict[int, Tuple[int, float, int]] = {}
        self.events: List[Event] = []
        # (time, system, used) samples, one per allocation change
        self.resource_timeline: List[Tuple[float, int, int]] = []
        self.last_reservation: Tuple[float, Optional[int]] = (math.inf, None)
        # Indices of jobs rejected at submission as larger than every system
        self.rejected: List[int] = []

    def log(self, msg: str) -> None:
        if self.verbose:
            print(f"[t={self.current_time:8.0f}] {msg}", file=sys.stderr)

    # -- placement ---------------------------------------------------------

    def choose_system(self, job: Job, candidates: Sequence[int]) -> int:
        """Pick one system among the admitted candidates (ascending index)."""
        if self.placement == "first_fit":
            return candidates[0]
        best = candidates[0]
        best_left = self.model.leftover(best, job)
        for k in candidates[1:]:
            left = self.model.leftover(k, job)
            if self.placement == "best_fit":
                better = left < best_left
            else:  # worst_fit
                better = left > best_left
            if better:
                best, best_left = k, left
        return best

    # -- scheduling pass ---------------------------------------------------

    def releases_by_system(self) -> List[List[Tuple[float, int]]]:
        releases: List[List[Tuple[float, int]]] = [
            [] for _ in range(self.model.num_systems())]
        for system, pessimistic_end, demand in self.running.values():
            if pessimistic_end > self.current_time:
                releases[system].append((pessimistic_end, demand))
        return releases

    def reservation_plan(self, k: int, releases: Sequence[Tuple[float, int]]) -> List[Tuple[float, int]]:
        free = self.model.free(k)
        plan = [(self.current_time, free)]
        for time, nodes in sorted(releases):
            free += nodes
            if plan[-1][0] == time:
                plan[-1] = (time, free)
            else:
                plan.append((time, free))
        return plan

    @staticmethod
    def plan_fits(plan: List[Tuple[float, int]], start: float, nodes: int, duration: float) -> bool:
        end = start + duration
        index = max(i for i, (time, _) in enumerate(plan) if time <= start)
        return all(available >= nodes for time, available in plan[index:]
                   if time < end or time == plan[index][0])

    @staticmethod
    def reserve(plan: List[Tuple[float, int]], start: float, nodes: int, duration: float) -> None:
        end = start + duration
        for boundary in (start, end):
            if not any(time == boundary for time, _ in plan):
                index = max(i for i, (time, _) in enumerate(plan) if time < boundary)
                plan.insert(index + 1, (boundary, plan[index][1]))
        for i, (time, available) in enumerate(plan):
            if start <= time < end:
                plan[i] = (time, available - nodes)

    def try_start_jobs(self) -> None:
        model = self.model
        num_systems = model.num_systems()

        # Step 1: start heads while one fits somewhere.
        while self.wait_queue:
            head = self.wait_queue[0]
            fits = [k for k in range(num_systems)
                    if model.admissible(k, head) and model.fits_now(k, head)]
            if not fits:
                break
            k = self.choose_system(head, fits)
            self.wait_queue.pop(0)
            self.log(f"Starting head job {head.idx} on {model.systems[k].name}")
            self.start_job(head, k)

        if not self.wait_queue:
            self.last_reservation = (math.inf, None)
            return

        # Step 2: the head is blocked everywhere.  Project it per system.
        head = self.wait_queue[0]
        releases = self.releases_by_system()
        shadows = [model.earliest_fit(k, head, releases[k], self.current_time)
                   for k in range(num_systems)]
        t_star = min(shadows)
        k_star = shadows.index(t_star) if t_star < math.inf else None
        self.last_reservation = (t_star, k_star)

        if not self.backfill:
            return

        if self.spatial_backfill == "target_only":
            windows = [t_star if k == k_star else math.inf
                       for k in range(num_systems)]
        elif self.spatial_backfill == "every_system":
            windows = shadows
        else:  # global_shadow
            windows = [t_star if shadows[k] < math.inf else math.inf
                       for k in range(num_systems)]

        plans = [self.reservation_plan(k, releases[k])
                 for k in range(num_systems)]
        for k, window in enumerate(windows):
            if window < math.inf and self.plan_fits(
                    plans[k], window, head.nodes, head.duration):
                self.reserve(plans[k], window, head.nodes, head.duration)

        remaining = [head]
        for job in self.wait_queue[1:]:
            candidates = [k for k in range(num_systems)
                          if model.fits_now(k, job) and
                          (windows[k] == math.inf or self.plan_fits(
                              plans[k], self.current_time, job.nodes,
                              job.duration))]
            if candidates:
                k = self.choose_system(job, candidates)
                self.log(f"Backfilling job {job.idx} on "
                         f"{model.systems[k].name} (preserves reservation)")
                self.start_job(job, k)
                self.reserve(plans[k], self.current_time, job.nodes,
                             job.duration)
            else:
                remaining.append(job)
        self.wait_queue = remaining

    def start_job(self, job: Job, k: int) -> None:
        job.start_time = self.current_time
        job.end_time = self.current_time + job.actual_run_time
        job.system = k
        demand = self.model.demand(k, job)
        self.model.allocate(k, job)
        self.running[job.idx] = (k, self.current_time + job.duration, demand)
        heapq.heappush(self.events, Event(job.end_time, "END", job.idx))
        self.resource_timeline.append(
            (self.current_time, k, self.model.used[k]))

    def handle_end(self, job: Job) -> None:
        if job.idx not in self.running:
            return
        k, _, _ = self.running.pop(job.idx)
        self.model.release(k, job)
        self.resource_timeline.append(
            (self.current_time, k, self.model.used[k]))
        self.log(f"Job {job.idx} completed on {self.model.systems[k].name}")

    def simulate(self, jobs: List[Job]) -> List[Job]:
        """Run the simulation; returns the jobs with results filled in.

        Jobs larger than the largest system are rejected at submission with
        a warning and left unscheduled, matching the core simulator's
        treatment of impossible requests. They are listed in
        ``self.rejected``.
        """
        max_capacity = max(self.model.capacity)
        self.rejected = []
        for job in jobs:
            if job.nodes > max_capacity:
                print(f"Job {job.idx} rejected: requests {job.nodes} nodes, "
                      f"more than the largest system ({max_capacity}); "
                      f"it can never run", file=sys.stderr)
                self.rejected.append(job.idx)
                continue
            heapq.heappush(self.events, Event(job.submit_time, "SUBMIT",
                                              job.idx))
        job_map = {job.idx: job for job in jobs}

        while self.events:
            event = heapq.heappop(self.events)
            self.current_time = event.time
            same_time = [event]
            while self.events and self.events[0].time == self.current_time:
                same_time.append(heapq.heappop(self.events))
            same_time.sort()
            for evt in same_time:
                if evt.type == "SUBMIT":
                    self.wait_queue.append(job_map[evt.job_idx])
                else:
                    self.handle_end(job_map[evt.job_idx])
            self.try_start_jobs()

        return list(job_map.values())


# -- input / output ----------------------------------------------------------

def parse_systems_inline(text: str) -> List[System]:
    """Parse ``name:nodes[,name:nodes...]``."""
    systems = []
    for item in text.split(","):
        item = item.strip()
        if not item:
            continue
        name, _, size = item.partition(":")
        if not size:
            raise ValueError(f"system '{item}' needs the form name:nodes")
        systems.append(System(name.strip(), int(size)))
    return systems


def load_systems_file(filename: str) -> List[System]:
    """Parse a systems table (``#machine,size[,...]``; '#' lines ignored)."""
    systems = []
    with open(filename, "r") as stream:
        for line in stream:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 2:
                raise ValueError(f"malformed systems line: {line}")
            systems.append(System(parts[0], int(parts[1])))
    return systems


def validate_systems(systems: List[System]) -> None:
    if not systems:
        raise ValueError("at least one system is required")
    names = [s.name for s in systems]
    if len(set(names)) != len(names):
        raise ValueError("system names must be unique")
    if any(s.capacity <= 0 for s in systems):
        raise ValueError("system capacities must be positive")


def load_trace(filename: str) -> List[Job]:
    jobs = []
    with open(filename, "r") as stream:
        reader = csv.DictReader(stream)
        for idx, row in enumerate(reader):
            submit_time = float(row.get("job_submit_time",
                                        row.get("submit_time", 0)))
            nodes = int(row.get("num_nodes", row.get("nodes", 1)))
            duration = float(row.get("time_limit", row.get("duration", 100)))
            actual = None
            if row.get("actual_run_time"):
                actual = float(row["actual_run_time"])
            jobs.append(Job(idx, submit_time, nodes, duration, actual))
    return jobs


def write_schedule(jobs: List[Job], systems: List[System],
                   filename: str) -> None:
    with open(filename, "w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["job_idx", "submit_time", "start_time", "end_time",
                         "nodes", "duration", "system"])
        for job in jobs:
            writer.writerow([
                job.idx, job.submit_time,
                job.start_time if job.start_time is not None else "",
                job.end_time if job.end_time is not None else "",
                job.nodes, job.duration,
                systems[job.system].name if job.system is not None else "",
            ])


def write_resource_trace(scheduler: MultiSystemScheduler,
                         systems: List[System], filename: str) -> None:
    """One row per allocation change with every system's state."""
    used = [0 for _ in systems]
    header = ["time"]
    for system in systems:
        header += [f"free_{system.name}", f"allocated_{system.name}"]
    with open(filename, "w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(header)
        for time, k, value in scheduler.resource_timeline:
            used[k] = value
            row = [time]
            for index, system in enumerate(systems):
                row += [system.capacity - used[index], used[index]]
            writer.writerow(row)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Reference multi-system FCFS scheduler with spatial "
                    "backfilling")
    parser.add_argument("trace")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--systems", help="name:nodes[,name:nodes...]")
    group.add_argument("--systems_file", help="systems table CSV")
    parser.add_argument("--spatial_backfill", choices=SPATIAL_POLICIES,
                        default="target_only")
    parser.add_argument("--placement_policy", choices=PLACEMENT_POLICIES,
                        default="best_fit")
    parser.add_argument("--no_backfill", action="store_true",
                        help="strict FCFS across systems, no backfilling")
    parser.add_argument("--outdir", default="/tmp")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    systems = (parse_systems_inline(args.systems) if args.systems
               else load_systems_file(args.systems_file))
    validate_systems(systems)

    jobs = load_trace(args.trace)
    scheduler = MultiSystemScheduler(CountResourceModel(systems),
                                     args.spatial_backfill,
                                     args.placement_policy,
                                     backfill=not args.no_backfill,
                                     verbose=args.verbose)
    jobs = scheduler.simulate(jobs)

    basename = os.path.basename(args.trace)
    if basename.endswith(".csv"):
        basename = basename[:-4]
    schedule_file = os.path.join(args.outdir, f"{basename}_msbf_reference.csv")
    resource_file = os.path.join(args.outdir,
                                 f"{basename}_msbf_reference_resources.csv")
    write_schedule(jobs, systems, schedule_file)
    write_resource_trace(scheduler, systems, resource_file)

    started = sum(1 for job in jobs if job.start_time is not None)
    rejected = len(scheduler.rejected)
    print(f"Systems: {', '.join(f'{s.name}:{s.capacity}' for s in systems)}",
          file=sys.stderr)
    print(f"Policy: {args.spatial_backfill}, placement: "
          f"{args.placement_policy}, backfill: {not args.no_backfill}",
          file=sys.stderr)
    print(f"Jobs started: {started}/{len(jobs)} (rejected: {rejected})",
          file=sys.stderr)
    print(f"Schedule written to: {schedule_file}", file=sys.stderr)
    print(f"Resource trace written to: {resource_file}", file=sys.stderr)
    return 0 if started + rejected == len(jobs) else 1


if __name__ == "__main__":
    sys.exit(main())
