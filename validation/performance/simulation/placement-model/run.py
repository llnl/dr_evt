#!/usr/bin/env python3
"""Build isolated placement trials and audit their repeated measurements."""
import argparse
import csv
import hashlib
import json
import platform
import random
import statistics
import subprocess
import tempfile
from pathlib import Path


def command(args, **kwargs):
    return subprocess.check_output(args, text=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="clang++")
    parser.add_argument("--baseline", default="a2d45c7")
    parser.add_argument("--trace", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=9)
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    here = Path(__file__).resolve().parent
    repo = here.parents[3]
    revision = command(["git", "rev-parse", args.baseline], cwd=repo).strip()
    variants = ["baseline", "fast", "batch", "scratch", "boolean", "combined"]
    flags = ["-std=c++20", "-O3", "-Wall", "-Wextra", "-Werror"]
    with tempfile.TemporaryDirectory(prefix="dr-evt-placement-") as directory:
        temporary = Path(directory)
        trace = args.trace.resolve() if args.trace else temporary / "jobs.csv"
        if not args.trace:
            rng = random.Random(2718)
            with trace.open("w") as stream:
                writer = csv.writer(stream, lineterminator="\n")
                writer.writerow(["job_submit_time", "num_nodes", "time_limit"])
                writer.writerows((i // 4, rng.randint(1, 8), rng.randint(1, 30))
                                 for i in range(20000))
        headers = ["src/dr_evt_types.hpp"] + command(
            ["git", "ls-tree", "-r", "--name-only", revision,
             "src/sim/resource"], cwd=repo).splitlines()
        for index, variant in enumerate(variants):
            root = temporary / variant
            root.mkdir()
            if variant != "combined":
                for header in headers:
                    target = root / header
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_text(command(["git", "show", f"{revision}:{header}"], cwd=repo))
                if variant != "baseline":
                    subprocess.run(["patch", "-s", "-p1", "-i", str(here / f"{variant}.patch")],
                                   cwd=root, check=True)
            include = repo / "src" if variant == "combined" else root / "src"
            for source in ["benchmark", "queries"]:
                subprocess.run([args.compiler, *flags,
                                f"-DDR_EVT_PLACEMENT_TRIAL={index}",
                                f"-I{include}", f"-I{repo / 'build'}",
                                str(here / f"{source}.cpp"), "-o", str(root / source)], check=True)
        samples = []
        for scenario in ["easy", "hybrid", "earliest", "boolean"]:
            for iteration in range(args.runs + 1):
                shift = iteration % len(variants)
                for variant in variants[shift:] + variants[:shift]:
                    root = temporary / variant
                    query = scenario in ["earliest", "boolean"]
                    invocation = ([str(root / "queries"), scenario] if query else
                                  [str(root / "benchmark"), str(trace), "500", scenario, "exact"])
                    row = json.loads(command(invocation))
                    row.update(variant=variant, scenario=scenario, iteration=iteration)
                    samples.append(row)
            rows = [row for row in samples if row["scenario"] == scenario]
            keys = (["hash"] if scenario in ["earliest", "boolean"] else
                    ["schedule_hash", "identities_hash", "reservations_hash", "completed",
                     "passes", "fit_queries", "future_reservations", "max_pending", "max_running"])
            assert len({tuple(row[k] for k in keys) for row in rows}) == 1, scenario
        medians = {}
        for scenario in ["easy", "hybrid", "earliest", "boolean"]:
            key = "ms" if scenario in ["earliest", "boolean"] else "scheduling_ms"
            medians[scenario] = {
                variant: statistics.median(row[key] for row in samples
                    if row["scenario"] == scenario and row["variant"] == variant and row["iteration"])
                for variant in variants}
        report = dict(baseline=revision, compiler=command([args.compiler, "--version"]).splitlines()[0],
                      flags=flags, platform=platform.platform(), machine=platform.machine(),
                      trace_sha256=hashlib.sha256(trace.read_bytes()).hexdigest(),
                      trace_origin=str(args.trace) if args.trace else "random seed 2718, 20000 jobs",
                      warmups=1, measured_runs=args.runs, nodes=500, medians_ms=medians, samples=samples)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(medians, indent=2))


if __name__ == "__main__":
    main()
