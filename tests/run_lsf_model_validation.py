#!/usr/bin/env python3
"""Validate the documented homogeneous LSF approximation, not an LSF daemon.

Requires DR_EVT_LEGACY_RESERVATION_BARRIER=ON. The independent reference uses
fixed sessions, FCFS, one protected head, and strict completion-before-start
backfill. Finite candidate limits exercise DR_EVT's model, not LSF accounting.
"""

import argparse
import csv
import json
import math
import pathlib
import random
import subprocess
import tempfile

from read_build_config import reservation_barrier


def reference(jobs, interval, depth, capacity=4):
    """Use tuples (submit, nodes, limit, actual), observing releases at ticks."""
    pending, running, starts = [], [], {}
    arrivals = list(enumerate(jobs))
    tick = interval
    while arrivals or pending or running:
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
        assert tick < 100000, "reference did not drain"
    return [starts[index] for index in range(len(jobs))]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("simulator", type=pathlib.Path)
    parser.add_argument("--config-header", type=pathlib.Path,
                        help="Generated header for a nonstandard install layout")
    parser.add_argument("--flexible-simulator", type=pathlib.Path,
                        help="Optional default-barrier build for a negative control")
    parser.add_argument("--flexible-config-header", type=pathlib.Path)
    parser.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args()
    simulator = args.simulator.resolve()
    try:
        barrier, config_header = reservation_barrier(simulator, args.config_header)
        if not barrier:
            print("SKIP: simplified LSF checks require a barrier-ON build; "
                  "{} records OFF.".format(config_header))
            return
        if args.flexible_simulator:
            control_barrier, _ = reservation_barrier(args.flexible_simulator,
                                                      args.flexible_config_header)
            if control_barrier:
                print("SKIP: flexible control records barrier ON; "
                      "running the legacy checks only.")
                args.flexible_simulator = None
        elif args.flexible_config_header:
            parser.error("--flexible-config-header requires --flexible-simulator")
    except ValueError as error:
        parser.error(str(error))
    results = []
    with tempfile.TemporaryDirectory(prefix="dr_evt_lsf_model_") as directory:
        work = pathlib.Path(directory)

        def run_case(name, jobs, interval=5, depth=0, expected=None,
                     actual_mode=False, binary=simulator):
            input_path = work / (name + ".csv")
            output_path = work / (name + ".out.csv")
            with input_path.open("w", newline="") as stream:
                writer = csv.writer(stream)
                writer.writerow(["job_submit_time", "num_nodes", "time_limit",
                                 "actual_run_time"])
                writer.writerows(jobs)
            oracle = reference(jobs, interval, depth)
            wanted = oracle if expected is None else expected
            if expected is not None and binary == simulator:
                assert oracle == expected, (name, "reference", oracle, expected)
            command = [str(binary), str(input_path), "--total_nodes", "4",
                       "--trace_format", "simple", "--timestamp_format", "epoch",
                       "--run_time_mode", "actual" if actual_mode else "limit",
                       "--msec_output", "--max_jobs", "9999", "--queue_impl", "circular",
                       "--priority_policy", "fcfs", "--backfill_policy", "easy",
                       "--dispatch_interval", str(interval),
                       "--dispatch_queue_depth", str(depth), "--outfile", str(output_path)]
            result = subprocess.run(command, text=True, capture_output=True, timeout=30)
            assert result.returncode == 0, (name, result.stdout, result.stderr)
            with output_path.open() as stream:
                rows = list(csv.DictReader(stream))
            observed = sorted((float(row["job_submit_time"]), int(row["num_nodes"]),
                               float(row["time_limit"]), float(row["begin_time"]),
                               float(row["end_time"])) for row in rows)
            expected_rows = sorted((submit, nodes, limit, start,
                                    start + (actual if actual_mode else limit))
                                   for (submit, nodes, limit, actual), start
                                   in zip(jobs, wanted))
            assert len(observed) == len(expected_rows), (name, observed, expected_rows)
            assert all(math.isclose(value, expected_value, abs_tol=1e-6)
                       for row, expected_row in zip(observed, expected_rows)
                       for value, expected_value in zip(row, expected_row)), (
                           name, observed, expected_rows)
            results.append({"case": name, "interval": interval, "depth": depth,
                            "starts": wanted, "observed_schedule": observed})

        # Origin 07:59:55: the first tick at 5 s is IBM's 08:00 start.
        # IBM does not specify job2's duration; use 30 minutes here.
        first = (0, 1, 7200, 7200)
        head = (10, 4, 1800, 1800)
        run_case("ibm_short_backfill", [first, head, (1805, 2, 3600, 3600)],
                 expected=[5, 7205, 1805])
        run_case("ibm_long_backfill", [first, head, (1805, 2, 7200, 7200)],
                 expected=[5, 7205, 9005])
        run_case("strict_equal_boundary", [first, head, (1805, 2, 5400, 5400)],
                 expected=[5, 7205, 9005])
        run_case("strict_just_before_boundary", [first, head, (1805, 2, 5399, 5399)],
                 expected=[5, 7205, 1805])
        run_case("limit_not_actual", [first, head, (1805, 2, 7200, 1800)],
                 expected=[5, 7205, 9005], actual_mode=True)
        run_case("early_release", [(0, 1, 7200, 3600), head,
                                   (1805, 2, 3600, 3600)],
                 expected=[5, 5405, 1805], actual_mode=True)
        run_case("session_cadence", [(2.3, 4, 2, 2), (6, 4, 2, 2)],
                 expected=[5, 10])
        run_case("fresh_candidate_limit", [(0, 1, 40, 40)] * 4,
                 depth=1, expected=[5, 10, 15, 20])
        run_case("unlimited_candidates", [(0, 1, 40, 40)] * 4,
                 expected=[5, 5, 5, 5])
        overlap = [(0, 3, 120, 120), (6, 2, 20, 20), (6, 1, 200, 200)]
        run_case("legacy_blocks_safe_overlap", overlap, expected=[5, 125, 125])
        if args.flexible_simulator:
            run_case("default_allows_safe_overlap", overlap,
                     expected=[5, 125, 10], binary=args.flexible_simulator.resolve())

        for seed in range(12):
            rng = random.Random(seed)
            jobs = []
            for _ in range(30):
                limit = rng.randrange(1, 41)
                jobs.append((rng.randrange(0, 101) + rng.choice((0, 0.25)),
                             rng.randrange(1, 5), limit, rng.randrange(1, limit + 1)))
            jobs.sort(key=lambda job: job[0])
            for interval in (1, 5):
                for depth in (0, 1, 3, 8):
                    run_case("random_{}_{}_{}".format(seed, interval, depth), jobs,
                             interval=interval, depth=depth, actual_mode=True)

    report = {"scope": "DR_EVT simplified LSF model; no LSF daemon comparison",
              "config_header": str(config_header),
              "reservation_rule": "legacy completion strictly before protected start",
              "checks": len(results), "results": results}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    print("Simplified LSF model: {} checks passed (96 randomized).".format(len(results)))


if __name__ == "__main__":
    main()
