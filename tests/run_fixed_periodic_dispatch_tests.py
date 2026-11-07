#!/usr/bin/env python3
"""Hand-computed schedules and a simple independent FCFS tick reference."""
import csv
import pathlib
import random
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "scripts"))
from python_periodic_scheduler import fcfs_reference, event_reference

simulator = str(pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/simulator").resolve())
checks = 0


with tempfile.TemporaryDirectory(prefix="dr_evt_fixed_periodic_dispatch_") as directory:
    work = pathlib.Path(directory)

    def run_case(name, jobs, expected, *options):
        global checks
        input_path, output_path = work / (name + ".csv"), work / (name + ".out.csv")
        with input_path.open("w") as stream:
            writer = csv.writer(stream)
            writer.writerow(["job_submit_time", "num_nodes", "time_limit"])
            writer.writerows(jobs)
        command = [simulator, str(input_path), "--total_nodes", "4",
                   "--trace_format", "simple", "--timestamp_format", "epoch",
                   "--run_time_mode", "limit", "--msec_output", "--max_jobs", "9999",
                   "--outfile", str(output_path), *options]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, name + ": " + result.stdout + result.stderr
        with output_path.open() as stream:
            rows = list(csv.DictReader(stream))
        observed = sorted((float(row["job_submit_time"]), int(row["num_nodes"]),
                           float(row["time_limit"]), float(row["begin_time"]),
                           float(row["end_time"])) for row in rows)
        wanted = sorted((submit, nodes, duration, start, start + duration)
                        for (submit, nodes, duration), start in zip(jobs, expected))
        assert observed == wanted, (name, observed, wanted)
        checks += 1

    for policy in ("none", "easy", "hybrid", "conservative"):
        run_case("prefix_" + policy, [(0, 1, 20)] * 4, [5, 5, 10, 10],
                 "--dispatch_interval", "5", "--dispatch_queue_depth", "2",
                 "--backfill_policy", policy, "--reservation_depth", "1")

    run_case("events", [(2, 4, 2), (7, 4, 5), (15, 4, 1), (30, 4, 1)],
             [5, 10, 15, 30], "--dispatch_interval", "5", "--backfill_policy", "none")
    run_case("fractional", [(2.3, 4, 2)], [5], "--dispatch_interval", "5",
             "--backfill_policy", "none")
    run_case("empty", [], [], "--dispatch_interval", "5", "--backfill_policy", "none")

    jobs = [(0, 3, 120), (6, 2, 20), (6, 4, 100), (6, 1, 200)]
    for depth, starts in ((1, [5, 125, 210, 10]), (2, [5, 125, 145, 245])):
        run_case("reservations_" + str(depth), jobs, starts,
                 "--dispatch_interval", "5", "--dispatch_queue_depth", "3",
                 "--backfill_policy", "hybrid", "--reservation_depth", str(depth))
    run_case("easy", jobs, [5, 125, 210, 10], "--dispatch_interval", "5",
             "--dispatch_queue_depth", "3", "--backfill_policy", "easy")
    run_case("conservative", jobs, [5, 125, 145, 245], "--dispatch_interval", "5",
             "--dispatch_queue_depth", "3", "--backfill_policy", "conservative")

    run_case("fractional_tick", [(0, 4, 1), (0, 4, 1)], [1.25, 2.5],
             "--dispatch_interval", "1.25", "--backfill_policy", "none")
    capacity = work / "capacity_schedule.csv"
    capacity.write_text("time,total_nodes\n0,0\n7,4\n")
    run_case("capacity", [(0, 4, 1)], [10], "--dispatch_interval", "5",
             "--backfill_policy", "none", "--capacity_schedule", str(capacity))
    run_case("cleanup", [(0, 4, 1), (0, 4, 1)], [5, 10],
             "--dispatch_interval", "5", "--backfill_policy", "none",
             "--node_release_delay", "2")

    for seed in range(4):
        rng = random.Random(seed)
        jobs = sorted((rng.randrange(0, 100), rng.randrange(1, 5), rng.randrange(1, 15))
                      for _ in range(40))
        for depth in (0, 1, 2, 5):
            run_case("fcfs_{}_{}".format(seed, depth), jobs, fcfs_reference(jobs, depth),
                     "--dispatch_interval", "5", "--dispatch_queue_depth", str(depth),
                     "--backfill_policy", "none")

    event_options = ("--dispatch_interval", "5", "--dispatch_on_events")
    for policy in ("none", "easy", "hybrid", "conservative"):
        run_case("event_prefix_" + policy, [(0, 1, 20)] * 4, [0, 0, 5, 5],
                 *event_options, "--dispatch_queue_depth", "2",
                 "--backfill_policy", policy, "--reservation_depth", "1")
    run_case("arrival_preserves_phase", [(0, 1, 20)] * 4 + [(7, 1, 20)],
             [0, 5, 7, 10, 20], *event_options,
             "--dispatch_queue_depth", "1", "--backfill_policy", "none")
    run_case("arrival_on_tick", [(0, 1, 20)] * 4 + [(5, 1, 20)],
             [0, 5, 10, 15, 20], *event_options,
             "--dispatch_queue_depth", "1", "--backfill_policy", "none")
    run_case("completion_arrival", [(0, 4, 2), (0, 4, 5), (7, 4, 1)],
             [0, 5, 10], *event_options, "--backfill_policy", "none")
    run_case("event_fractional", [(2.3, 4, 2)], [2.3],
             *event_options, "--backfill_policy", "none")
    run_case("event_empty", [], [], *event_options, "--backfill_policy", "none")
    run_case("event_capacity", [(0, 4, 1)], [10], *event_options,
             "--backfill_policy", "none", "--capacity_schedule", str(capacity))
    run_case("event_cleanup", [(0, 4, 1), (0, 4, 1)], [0, 5],
             *event_options, "--backfill_policy", "none", "--node_release_delay", "2")
    for depth, starts in ((1, [0, 120, 210, 6]), (2, [0, 120, 140, 240])):
        run_case("event_reservations_" + str(depth),
                 [(0, 3, 120), (6, 2, 20), (6, 4, 100), (6, 1, 200)], starts,
                 *event_options, "--dispatch_queue_depth", "3",
                 "--backfill_policy", "hybrid", "--reservation_depth", str(depth))
    for seed in range(4):
        rng = random.Random(seed)
        jobs = sorted((rng.randrange(0, 100), rng.randrange(1, 5), rng.randrange(1, 15))
                      for _ in range(40))
        for depth in (0, 1, 2, 5):
            run_case("event_fcfs_{}_{}".format(seed, depth), jobs,
                     event_reference(jobs, depth), *event_options,
                     "--dispatch_queue_depth", str(depth), "--backfill_policy", "none")

    # Backfilled entries leave holes in a bounded prefix. Later ticks must
    # skip them, preserve future arrivals, and retain the live candidate cap
    # through queue compaction.
    for notified in (False, True):
        for depth in (2, 3):
            for policy in ("hybrid", "conservative"):
                initial = 0 if notified else 1
                small_start = initial + (1 if depth == 2 else 0)
                options = ["--dispatch_interval", "1", "--dispatch_queue_depth", str(depth),
                           "--backfill_policy", policy, "--reservation_depth", "1",
                           "--wait_queue_capacity", "4"]
                if notified:
                    options.append("--dispatch_on_events")
                run_case("prefix_holes_{}_{}_{}".format(notified, depth, policy),
                         [(0, 3, 12), (0, 4, 2)] + [(0, 1, 1)] * 8 + [(30, 1, 20)] * 2,
                         [initial, initial + 12] + list(range(small_start, small_start + 8))
                         + [30, 30], *options)

    # Large conservative prefixes exercise the search-bound planner. None of
    # the two-node borrowers fits the lone idle node before the head starts.
    for notified in (False, True):
        for horizon in (None, "30"):
            initial = 0 if notified else 1
            options = ["--dispatch_interval", "1", "--dispatch_queue_depth", "256",
                       "--backfill_policy", "conservative"]
            if notified:
                options.append("--dispatch_on_events")
            if horizon is not None:
                options.extend(["--planning_horizon", horizon])
            run_case("large_conservative_{}_{}".format(notified, horizon),
                     [(0, 3, 300), (0, 4, 1)] + [(0, 2, 1)] * 200,
                     [initial, initial + 300] +
                     [initial + 301 + i // 2 for i in range(200)], *options)

    # Explicit zero must preserve the existing event-driven schedule bytes.
    run_case("default", [(0, 4, 2), (3, 4, 2)], [0, 3], "--backfill_policy", "none")
    run_case("zero", [(0, 4, 2), (3, 4, 2)], [0, 3], "--backfill_policy", "none",
             "--dispatch_interval", "0", "--dispatch_queue_depth", "0")
    assert (work / "default.out.csv").read_bytes() == (work / "zero.out.csv").read_bytes()
    checks += 1

    for options in (
        ["--dispatch_on_events"],
        ["--dispatch_on_events", "--dispatch_interval", "0"],
        ["--dispatch_on_events", "--backfill_interval", "5"],
        ["--dispatch_interval", "-1"], ["--dispatch_interval", "nan"],
        ["--dispatch_interval", "inf"], ["--dispatch_interval", "nope"],
        ["--dispatch_interval", "5junk"], ["--dispatch_queue_depth", "2"],
        ["--dispatch_interval", "5", "--backfill_interval", "5"],
        ["--dispatch_interval", "5", "--backfill_queue_depth", "2"],
        ["--dispatch_interval", "5", "--queue_impl", "deque"],
        ["--dispatch_interval", "5", "--priority_policy", "sjf"],
        ["--dispatch_interval", "5", "--backfill_policy", "hybrid", "--reservation_depth", "0"],
    ):
        result = subprocess.run([simulator, str(work / "default.csv"), "--total_nodes", "4",
                                 "--trace_format", "simple", *options],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode != 0, "accepted invalid options: " + repr(options)
        checks += 1

print("Fixed periodic dispatch (ticks and optional arrival wakes): {} checks passed".format(checks))
