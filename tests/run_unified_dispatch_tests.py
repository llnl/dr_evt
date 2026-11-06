#!/usr/bin/env python3
"""Hand-computed schedules and a simple independent FCFS tick reference."""
import csv
import pathlib
import random
import subprocess
import sys
import tempfile

simulator = str(pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/simulator").resolve())
checks = 0


def fcfs_reference(jobs, depth, interval=5, capacity=4):
    pending, running, starts = [], [], {}
    unseen = list(enumerate(jobs))
    tick = interval
    while unseen or pending or running:
        running = [(end, nodes) for end, nodes in running if end > tick]
        while unseen and unseen[0][1][0] <= tick:
            pending.append(unseen.pop(0))
        free = capacity - sum(nodes for _, nodes in running)
        for index, (submit, nodes, duration) in pending[:depth or len(pending)]:
            if nodes > free:
                break
            starts[index] = tick
            free -= nodes
            running.append((tick + duration, nodes))
        pending = [(index, job) for index, job in pending if index not in starts]
        tick += interval
        assert tick < 10000, "reference failed to drain"
    return [starts[i] for i in range(len(jobs))]


with tempfile.TemporaryDirectory(prefix="dr_evt_unified_") as directory:
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

    # Explicit zero must preserve the existing event-driven schedule bytes.
    run_case("default", [(0, 4, 2), (3, 4, 2)], [0, 3], "--backfill_policy", "none")
    run_case("zero", [(0, 4, 2), (3, 4, 2)], [0, 3], "--backfill_policy", "none",
             "--dispatch_interval", "0", "--dispatch_queue_depth", "0")
    assert (work / "default.out.csv").read_bytes() == (work / "zero.out.csv").read_bytes()
    checks += 1

    for options in (
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

print("Unified periodic dispatch: {} checks passed".format(checks))
