#!/usr/bin/env python3
"""Compare baseline and count-model simulators, optionally with observation."""

import argparse
import csv
import hashlib
import json
import platform
import random
import resource
import statistics
import subprocess
import time
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit(schedule, capacity, expected_jobs):
    events = {}
    jobs = 0
    with schedule.open() as stream:
        for row in csv.DictReader(stream):
            jobs += 1
            submit = float(row["job_submit_time"])
            start, end = float(row["begin_time"]), float(row["end_time"])
            quantity = int(row["num_nodes"])
            assert start >= submit and end - start == float(row["time_limit"])
            events[start] = events.get(start, 0) + quantity
            events[end] = events.get(end, 0) - quantity
    occupied = 0
    for timestamp in sorted(events):
        occupied += events[timestamp]
        assert 0 <= occupied <= capacity, (timestamp, occupied)
    assert occupied == 0
    assert jobs == expected_jobs, (jobs, expected_jobs)


def paired_change(baseline, candidate):
    ratios = [b / a - 1 for a, b in zip(baseline, candidate)]
    rng = random.Random(2718)
    samples = sorted(statistics.median(rng.choices(ratios, k=len(ratios)))
                     for _ in range(2000))
    return {
        "median_percent": 100 * statistics.median(ratios),
        "bootstrap_95_percent": [100 * samples[50], 100 * samples[1949]],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--hooks", type=Path,
                        help="Optional explicitly instrumented or historical binary")
    parser.add_argument("--runtime-diagnostics", action="store_true",
                        help="Enable runtime diagnostics only for the --hooks variant")
    parser.add_argument("--outdir", type=Path, required=True)
    parser.add_argument("--revision", required=True,
                        help="Baseline revision; candidate changes stay uncommitted")
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--build-settings", required=True)
    parser.add_argument("--runs", type=int, default=9)
    parser.add_argument("--batch-size", type=int, default=1,
                        help="Executions averaged into each timing sample")
    parser.add_argument("--cases", nargs="+",
                        help="Optional subset of case names to measure")
    parser.add_argument("--stress-jobs", type=int, default=20000)
    parser.add_argument("--per-pass-jobs", type=int, default=8000)
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    if (args.runs < 3 or args.batch_size < 1 or args.stress_jobs < 1 or
            args.per_pass_jobs < 1):
        parser.error("use at least three measured runs and one stress job")
    if args.runtime_diagnostics and args.hooks is None:
        parser.error("--runtime-diagnostics requires --hooks")
    root = args.outdir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    binaries = {name: getattr(args, name).resolve()
                for name in ("baseline", "model")}
    if args.hooks is not None:
        binaries["hooks"] = args.hooks.resolve()
    inputs = {}
    # Both fixtures are deterministic; the stress fixture matches the existing
    # fixed conservative profile's input generation.
    for name, count, capacity, burst, max_nodes, max_duration, seed in (
        ("mixed", 2000, 756, 24, 128, 300, 7351),
        ("stress", args.stress_jobs, 128, 4, 8, 30, 2718),
        ("per-pass-stress", args.per_pass_jobs, 128, 4, 8, 30, 2718),
    ):
        path = root / (name + ".csv")
        rng = random.Random(seed)
        with path.open("w") as stream:
            writer = csv.writer(stream, lineterminator="\n")
            writer.writerow(["job_submit_time", "num_nodes", "time_limit"])
            writer.writerows((i // burst, rng.randint(1, max_nodes),
                              rng.randint(1, max_duration)) for i in range(count))
        inputs[name] = {"path": path, "capacity": capacity, "sha256": digest(path),
                        "jobs": count, "seed": seed}
    cases = [
        ("mixed-" + policy, "mixed", policy, ["--per_pass_accounting",
          "--backfill_queue_depth", "64", "--reservation_depth", "32"])
        for policy in ("none", "easy", "hybrid", "conservative")
    ] + [
        ("stress-per-pass-conservative", "per-pass-stress", "conservative",
         ["--per_pass_accounting", "--backfill_queue_depth", "0"]),
        ("stress-cached-conservative", "stress", "conservative",
         ["--dispatch_interval", "1", "--dispatch_queue_depth", "10000"]),
    ]
    if args.cases:
        names = {case[0] for case in cases}
        if set(args.cases) - names:
            parser.error("unknown cases: " + ", ".join(sorted(set(args.cases) - names)))
        cases = [case for case in cases if case[0] in args.cases]
    report = {
        "baseline_revision": args.revision,
        "compiler": args.compiler,
        "build_settings": args.build_settings,
        "platform": platform.platform(),
        "machine": platform.machine(),
        "runs": args.runs,
        "batch_size": args.batch_size,
        "warmups": 1,
        "binaries": {name: {"path": str(path), "sha256": digest(path)}
                     for name, path in binaries.items()},
        "inputs": {name: {key: str(value) if isinstance(value, Path) else value
                           for key, value in entry.items()}
                   for name, entry in inputs.items()},
        "cases": {},
    }
    for name, input_name, policy, extra in cases:
        fixture = inputs[input_name]
        timings = {variant: {"wall_ms": [], "cpu_ms": []} for variant in binaries}
        expected_hashes = None
        commands = {}
        for iteration in range(args.runs + 1):
            # Rotate execution order to reduce systematic thermal/order bias.
            order = list(binaries)
            shift = iteration % len(order)
            order = order[shift:] + order[:shift]
            for variant in order:
                output = root / (name + "-" + variant + "-schedule.csv")
                history = root / (name + "-" + variant + "-resources.csv")
                command = [str(binaries[variant]), str(fixture["path"]),
                           "--total_nodes", str(fixture["capacity"]),
                           "--trace_format", "simple", "--timestamp_format", "epoch",
                           "--run_time_mode", "limit", "--queue_impl", "circular",
                           "--backfill_policy", policy, "--outfile", str(output),
                           "--resource_trace", str(history)] + extra
                if variant == "hooks" and args.runtime_diagnostics:
                    command += ["--resource_model_diagnostics"]
                commands[variant] = command
                elapsed = cpu = 0
                for _ in range(args.batch_size):
                    before = resource.getrusage(resource.RUSAGE_CHILDREN)
                    started = time.perf_counter()
                    result = subprocess.run(command, capture_output=True, text=True,
                                            timeout=args.timeout)
                    elapsed += 1000 * (time.perf_counter() - started)
                    after = resource.getrusage(resource.RUSAGE_CHILDREN)
                    cpu += 1000 * (after.ru_utime + after.ru_stime -
                                   before.ru_utime - before.ru_stime)
                    if result.returncode:
                        raise RuntimeError(" ".join(command) + "\n" +
                                           result.stdout + result.stderr)
                    hashes = {"schedule": digest(output), "resources": digest(history)}
                    if expected_hashes is None:
                        expected_hashes = hashes
                        audit(output, fixture["capacity"], fixture["jobs"])
                    assert hashes == expected_hashes, (name, variant, iteration, hashes)
                if iteration:
                    timings[variant]["wall_ms"].append(elapsed / args.batch_size)
                    timings[variant]["cpu_ms"].append(cpu / args.batch_size)
        for values in timings.values():
            for metric in ("wall_ms", "cpu_ms"):
                values["median_" + metric] = statistics.median(values[metric])
        changes = {
            "abstraction_wall": paired_change(timings["baseline"]["wall_ms"],
                                               timings["model"]["wall_ms"]),
            "abstraction_cpu": paired_change(timings["baseline"]["cpu_ms"],
                                              timings["model"]["cpu_ms"]),
        }
        if "hooks" in timings:
            changes["observation_wall"] = paired_change(
                timings["model"]["wall_ms"], timings["hooks"]["wall_ms"])
        report["cases"][name] = {"timings": timings, "changes": changes,
                                  "output_hashes": expected_hashes,
                                  "commands": commands}
        (root / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
        print(name + ": " + ", ".join(
            variant + "=" + format(values["median_wall_ms"], ".2f") + "ms"
            for variant, values in timings.items()), flush=True)
    print("Exact schedule/resource output comparison and capacity audits passed.")


if __name__ == "__main__":
    main()
