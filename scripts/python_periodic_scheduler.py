#!/usr/bin/env python3
"""Independent reference for fixed periodic scheduling in a homogeneous pool.

Supports FCFS without backfill, with optional arrival-triggered passes, and
FCFS/EASY with strict completion-before-reservation admission on ticks only.
The EASY reference does not implement resource-time overlap admission.
Capacity changes, cleanup delays, warm starts, horizon/resolution controls,
and general hybrid/conservative backfill are outside this reference's scope.
"""

import argparse
import csv
import math
from pathlib import Path
import sys


def periodic_fcfs_reference(jobs, depth=0, interval=5, capacity=4,
                            dispatch_on_events=False):
    """Return starts for sorted (submit, nodes, runtime) tuples.

    The first tick is at interval after time zero. Each tick or enabled
    arrival batch gets a fresh candidate prefix; completions alone do not
    request a pass. Simultaneous arrival and timer wakes share one pass.
    """
    pending, running, starts = [], [], {}
    unseen = list(enumerate(jobs))
    tick = interval
    while unseen or pending:
        # Empty queue ticks cannot change a start. Preserve the phase while
        # jumping to the next arrival, including traces with epoch timestamps.
        if not pending and unseen and unseen[0][1][0] > tick:
            tick = math.ceil(unseen[0][1][0] / interval) * interval
        time = (min(unseen[0][1][0] if unseen else float("inf"),
                    min((end for end, _ in running), default=float("inf")), tick)
                if dispatch_on_events else tick)
        running = [(end, nodes) for end, nodes in running if end > time]
        notification = bool(unseen and unseen[0][1][0] == time)
        timer_wake = time == tick
        while unseen and unseen[0][1][0] <= time:
            pending.append(unseen.pop(0))
        if timer_wake:
            tick += interval
        if not (timer_wake or (dispatch_on_events and notification)):
            continue
        free = capacity - sum(nodes for _, nodes in running)
        for index, (_, nodes, duration) in pending[:depth or len(pending)]:
            if nodes > free:
                break
            starts[index] = time
            free -= nodes
            running.append((time + duration, nodes))
        pending = [(index, job) for index, job in pending if index not in starts]
    return [starts[index] for index in range(len(jobs))]


def fcfs_reference(jobs, depth, interval=5, capacity=4):
    """Tick-only FCFS reference used by the regression suite."""
    return periodic_fcfs_reference(jobs, depth, interval, capacity)


def event_reference(jobs, depth, interval=5, capacity=4):
    """FCFS reference with arrival wakes used by the regression suite."""
    return periodic_fcfs_reference(jobs, depth, interval, capacity, True)


def legacy_easy_reference(jobs, interval, depth, capacity=4):
    """Use tuples (submit, nodes, limit, actual), observing releases at ticks."""
    pending, running, starts = [], [], {}
    arrivals = list(enumerate(jobs))
    tick = interval
    while arrivals or pending:
        if not pending and arrivals and arrivals[0][1][0] > tick:
            tick = math.ceil(arrivals[0][1][0] / interval) * interval
        running = [job for job in running if job[0] > tick]
        while arrivals and arrivals[0][1][0] <= tick:
            pending.append(arrivals.pop(0))
        free = capacity - sum(nodes for _, _, nodes in running)
        prefix = pending[:depth or len(pending)]
        blocked = None
        for position, (index, (_, nodes, limit, actual)) in enumerate(prefix):
            if nodes > free:
                blocked = position
                break
            starts[index] = tick
            running.append((tick + actual, tick + limit, nodes))
            free -= nodes
        if blocked is not None:
            head_nodes = prefix[blocked][1][1]
            future_free = free
            protected_start = None
            for release, nodes in sorted((estimate, nodes)
                                         for _, estimate, nodes in running):
                future_free += nodes
                if future_free >= head_nodes:
                    protected_start = release
                    break
            assert protected_start is not None
            for index, (_, nodes, limit, actual) in prefix[blocked + 1:]:
                if nodes <= free and tick + limit < protected_start:
                    starts[index] = tick
                    running.append((tick + actual, tick + limit, nodes))
                    free -= nodes
        pending = [(index, job) for index, job in pending if index not in starts]
        tick += interval
    return [starts[index] for index in range(len(jobs))]


def positive_int(value):
    value = int(value)
    if value <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return value


def nonnegative_int(value):
    value = int(value)
    if value < 0:
        raise argparse.ArgumentTypeError("must be nonnegative")
    return value


def positive_seconds(value):
    value = float(value)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError("must be finite and positive")
    return value


def load_jobs(path, capacity, actual_mode):
    """Read simple epoch CSV, retaining input row numbers for stable ties."""
    jobs = []
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        required = {"job_submit_time", "num_nodes", "time_limit"}
        if not required.issubset(reader.fieldnames or []):
            raise ValueError("trace requires job_submit_time,num_nodes,time_limit")
        for index, row in enumerate(reader):
            try:
                submit = float(row["job_submit_time"])
                nodes = int(row["num_nodes"])
                limit = float(row["time_limit"])
                actual_text = row.get("actual_run_time")
                actual = float(actual_text) if actual_mode and actual_text else limit
                if (not all(math.isfinite(value) for value in (submit, limit, actual))
                        or submit < 0 or limit <= 0 or actual < 0
                        or nodes <= 0 or nodes > capacity):
                    raise ValueError("require finite nonnegative submission/runtime, "
                                     "positive time limit, and nodes in 1..capacity")
                jobs.append((index, submit, nodes, limit, min(actual, limit)))
            except (TypeError, ValueError) as error:
                raise ValueError("row {}: {}".format(index + 2, error)) from error
    return jobs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--nodes", type=positive_int, default=4)
    parser.add_argument("--dispatch-interval", type=positive_seconds, default=5)
    parser.add_argument("--dispatch-queue-depth", type=nonnegative_int, default=0)
    parser.add_argument("--dispatch-on-events", action="store_true")
    parser.add_argument("--backfill-policy", choices=("none", "easy"), default="none",
                        help="easy supports strict reservation admission only")
    parser.add_argument("--run-time-mode", choices=("limit", "actual"), default="limit")
    parser.add_argument("--outfile", type=Path, help="schedule CSV (default: stdout)")
    args = parser.parse_args()
    if args.backfill_policy == "easy" and args.dispatch_on_events:
        parser.error("arrival-triggered EASY is not supported by this reference")
    if args.outfile and args.outfile.resolve() == args.trace.resolve():
        parser.error("--outfile must differ from the input trace")
    try:
        jobs = load_jobs(args.trace, args.nodes, args.run_time_mode == "actual")
        ordered = sorted(jobs, key=lambda job: (job[1], job[0]))
        if args.backfill_policy == "easy":
            starts = legacy_easy_reference(
                [job[1:] for job in ordered], args.dispatch_interval,
                args.dispatch_queue_depth, args.nodes)
        else:
            starts = periodic_fcfs_reference(
                [(submit, nodes, actual) for _, submit, nodes, _, actual in ordered],
                args.dispatch_queue_depth, args.dispatch_interval, args.nodes,
                args.dispatch_on_events)
        starts_by_index = {job[0]: start for job, start in zip(ordered, starts)}
        stream = args.outfile.open("w", newline="") if args.outfile else sys.stdout
        try:
            writer = csv.writer(stream)
            writer.writerow(["job_idx", "job_submit_time", "begin_time", "end_time",
                             "num_nodes", "time_limit"])
            for index, submit, nodes, limit, actual in jobs:
                start = starts_by_index[index]
                writer.writerow([index, submit, start, start + actual, nodes, limit])
        finally:
            if args.outfile:
                stream.close()
    except (OSError, ValueError, csv.Error) as error:
        parser.error(str(error))
    print("Priority policy: fcfs; backfill policy: {}; interval: {} s; "
          "queue depth: {}; arrival passes: {}; jobs: {}".format(
              args.backfill_policy, args.dispatch_interval,
              args.dispatch_queue_depth, args.dispatch_on_events, len(jobs)),
          file=sys.stderr)
    if args.backfill_policy == "easy":
        print("Reservation admission: strict completion before protected start",
              file=sys.stderr)


if __name__ == "__main__":
    main()
