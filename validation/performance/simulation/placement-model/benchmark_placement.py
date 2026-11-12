#!/usr/bin/env python3
"""Audit and time complete simulator runs; no build, Flux install, or profiler required."""

import argparse
from collections import Counter
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import re
import resource
import statistics
import shutil
import subprocess
import sys
import time

FEATURES = ("none", "fast", "batch", "scratch", "boolean", "all")
REPO = Path(__file__).resolve().parents[4]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    with path.open("rb") as stream:
        result = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def generate_trace(path, jobs, seed):
    rng = random.Random(seed)
    with path.open("w", newline="") as stream:
        rows = csv.writer(stream, lineterminator="\n")
        rows.writerow(["job_submit_time", "num_nodes", "time_limit"])
        rows.writerows((i // 4, rng.randint(1, 8), rng.randint(1, 30))
                      for i in range(jobs))


def read_trace(path, nodes):
    """Limit-runtime, whole-second simple traces with no oversized/rejected jobs."""
    jobs = Counter()
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        require(reader.fieldnames and set(reader.fieldnames) ==
                {"job_submit_time", "num_nodes", "time_limit"},
                "input must have exactly job_submit_time,num_nodes,time_limit columns")
        for row in reader:
            submit, quantity, limit = (float(row[name]) for name in
                                      ("job_submit_time", "num_nodes", "time_limit"))
            require(all(math.isfinite(value) and value.is_integer()
                        for value in (submit, quantity, limit)),
                    "benchmark inputs must contain finite whole-second times and integer quantities")
            require(submit >= 0 and 0 < quantity <= nodes and limit > 0,
                    "input has negative submission, nonpositive runtime, or empty/oversized request")
            jobs[(submit, int(quantity), limit)] += 1
    require(jobs, "input trace has no jobs")
    return jobs


def audit(schedule, history, nodes, expected):
    """Independently check conservation, durations and grouped-event capacity."""
    observed, events = Counter(), {}
    with schedule.open(newline="") as stream:
        for row in csv.DictReader(stream):
            submit, start, end, limit = (float(row[name]) for name in
                                        ("job_submit_time", "begin_time", "end_time", "time_limit"))
            quantity = int(row["num_nodes"])
            require(all(math.isfinite(value) for value in (submit, start, end, limit)),
                    "schedule contains nonfinite times")
            require(start >= submit and end - start == limit,
                    "job started before submission or did not run for its limit")
            observed[(submit, quantity, limit)] += 1
            events[start] = events.get(start, 0) + quantity
            events[end] = events.get(end, 0) - quantity
    require(observed == expected, "schedule omitted, duplicated or changed input jobs")
    occupied, peak = 0, 0
    profile = []
    for timestamp in sorted(events):
        occupied += events[timestamp]
        require(0 <= occupied <= nodes, "schedule exceeds capacity at {}".format(timestamp))
        peak = max(peak, occupied)
        profile.append((timestamp, occupied))
    require(occupied == 0, "schedule does not release all resources")
    cursor, expected_occupied, previous, last = 0, 0, None, None

    def check_final(timestamp, allocation):
        nonlocal cursor, expected_occupied
        while cursor < len(profile) and profile[cursor][0] <= timestamp:
            expected_occupied = profile[cursor][1]
            cursor += 1
        require(allocation == expected_occupied,
                "resource history disagrees with completed event batch at {}".format(timestamp))

    with history.open(newline="") as stream:
        for row in csv.DictReader(stream):
            timestamp = float(row["time"])
            free, allocated = int(row["free_nodes"]), int(row["allocated_nodes"])
            releasing = int(row.get("releasing_nodes", 0))
            require(math.isfinite(timestamp) and (previous is None or timestamp >= previous),
                    "resource history is nonfinite or goes backwards")
            require(free >= 0 and allocated >= 0 and releasing == 0 and free + allocated == nodes,
                    "resource history violates static capacity accounting")
            if previous is not None and timestamp != previous:
                check_final(previous, last)
            previous, last = timestamp, allocated
    require(previous is not None, "empty resource history")
    check_final(previous, last)
    require(previous >= profile[-1][0] and last == 0, "resource history omits final releases")
    return {"jobs": sum(observed.values()), "peak_allocated_nodes": peak,
            "makespan": profile[-1][0] - min(job[0] for job in expected)}


def file_info(path):
    return {"path": str(path), "sha256": digest(path), "bytes": path.stat().st_size}


def build_info(binary, cache, libraries):
    info = {"executable": file_info(binary), "supplied_libraries": [file_info(p) for p in libraries]}
    if cache and cache.is_file():
        fields = {}
        for line in cache.read_text().splitlines():
            match = re.match(r"([^/#][^:]*):[^=]+=(.*)$", line)
            if match and (match[1].startswith("DR_EVT_") or match[1].startswith("CMAKE_CXX_") or
                          match[1] in ("CMAKE_BUILD_TYPE", "BUILD_SHARED_LIBS", "CMAKE_INSTALL_PREFIX")):
                fields[match[1]] = match[2]
        info["cmake_cache"] = {**file_info(cache), "fields": fields}
        compiler = fields.get("CMAKE_CXX_COMPILER")
        if compiler and Path(compiler).is_file():
            version = subprocess.run([compiler, "--version"], capture_output=True, text=True, timeout=10)
            info["compiler_version"] = version.stdout.strip()
    return info


def command_for(binary, trace, output, history, args, policy, model, features, coarse, diagnostic=False):
    command = [str(binary), str(trace), "--priority_policy", "fcfs", "--queue_impl", "circular",
               "--total_nodes", str(args.nodes), "--trace_format", "simple",
               "--timestamp_format", "epoch", "--run_time_mode", "limit", "--backfill_policy", policy,
               "--outfile", str(output), "--resource_trace", str(history)]
    if policy == "hybrid":
        command += ["--reservation_depth", str(args.reservation_depth)]
    if policy in ("hybrid", "conservative"):
        command += ["--backfill_queue_depth", str(args.queue_depth)]
    if model:
        command += ["--resource_model", model]
    if features:
        command += ["--placement_optimizations", features]
    if coarse:
        command += ["--placement_coarse_filter"]
    if diagnostic:
        command += ["--resource_model_diagnostics"]
    return command


def execute(command, timeout, log):
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    begin = time.perf_counter()
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        log.write_text("Command: {}\nTIMEOUT after {} seconds\n{}\n{}\n".format(
            json.dumps(command), timeout, error.stdout, error.stderr))
        raise RuntimeError("run timed out; see {}".format(log)) from error
    wall = 1000 * (time.perf_counter() - begin)
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    cpu = 1000 * (after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime)
    log.write_text("Command: {}\nExit: {}\n{}\n{}".format(
        json.dumps(command), result.returncode, result.stdout, result.stderr))
    require(result.returncode == 0, "simulator failed; see {}".format(log))
    return wall, cpu, result.stdout


def run(args, report, artifacts):
    binary = args.binary.resolve()
    require(binary.is_file() and os.access(binary, os.X_OK), "binary is missing or not executable")
    trace = args.trace.resolve() if args.trace else artifacts / "jobs-{}-seed{}.csv".format(args.jobs, args.seed)
    if not args.trace:
        generate_trace(trace, args.jobs, args.seed)
    expected = read_trace(trace, args.nodes)
    cache = args.cmake_cache.resolve() if args.cmake_cache else binary.parent / "CMakeCache.txt"
    report.update(build=build_info(binary, cache, [p.resolve() for p in args.library]),
                  trace=file_info(trace), jobs=sum(expected.values()),
                  generator=None if args.trace else {"seed": args.seed, "arrivals_per_second": 4,
                                                     "nodes": [1, 8], "limits": [1, 30]})
    header_candidates = [args.config_header.resolve()] if args.config_header else [
        binary.parent / "dr_evt_config.hpp", binary.parent.parent / "include/dr_evt_config.hpp"]
    header = next((path for path in header_candidates if path.is_file()), None)
    if args.config_header and header is None:
        raise ValueError("supplied config header does not exist")
    if header:
        report["build"]["generated_config"] = file_info(header)
        shutil.copyfile(header, artifacts / "dr_evt_config.hpp")
    if cache.is_file():
        shutil.copyfile(cache, artifacts / "CMakeCache.txt")
    baseline = args.capacity_baseline.resolve() if args.capacity_baseline else None
    if baseline:
        require(baseline.is_file() and os.access(baseline, os.X_OK), "capacity baseline is not executable")
        report["capacity_baseline"] = file_info(baseline)
    modes = ["capacity"] + list(FEATURES)
    configurations = {mode: (binary, "capacity" if mode == "capacity" else "placement",
                             None if mode == "capacity" else mode, args.coarse_filter)
                      for mode in modes}
    if args.compare_coarse_filter:
        require(not args.coarse_filter, "choose --coarse-filter or --compare-coarse-filter")
        for mode in FEATURES:
            configurations[mode + "-coarse"] = (binary, "placement", mode, True)
    if baseline:
        configurations["capacity-baseline"] = (baseline, None, None, False)
    modes = list(configurations)
    for policy in (["easy", "hybrid"] if args.policy == "all" else [args.policy]):
        case = {"reservation_depth": 1 if policy == "easy" else args.reservation_depth if policy == "hybrid" else None,
                "queue_depth": 0 if policy == "easy" else args.queue_depth,
                "samples_ms": {mode: [] for mode in modes}, "cpu_samples_ms": {mode: [] for mode in modes},
                "commands": {}, "decisions": {}, "audits": {}, "run_order": [], "diagnostics": {}}
        report["cases"][policy] = case
        directory = artifacts / policy
        directory.mkdir()
        for iteration in range(args.warmups + args.runs):
            shift = iteration % len(modes)
            order = modes[shift:] + modes[:shift]
            case["run_order"].append(order)
            for mode in order:
                output, history = directory / (mode + "-jobs.csv"), directory / (mode + "-resources.csv")
                # Missing output from a failed execution must never reuse an earlier run's files.
                for path in (output, history):
                    if path.exists():
                        path.unlink()
                executable, model, features, coarse = configurations[mode]
                command = command_for(executable, trace, output, history, args, policy, model, features, coarse)
                case["commands"][mode] = command
                wall, cpu, _ = execute(command, args.timeout, directory / ("{}-{:03d}.log".format(mode, iteration)))
                case["audits"][mode] = audit(output, history, args.nodes, expected)
                hashes = {"jobs_sha256": digest(output), "resources_sha256": digest(history)}
                require(mode not in case["decisions"] or case["decisions"][mode] == hashes,
                        "nondeterministic outputs: {}/{}".format(policy, mode))
                case["decisions"][mode] = hashes
                if iteration >= args.warmups:
                    case["samples_ms"][mode].append(wall)
                    case["cpu_samples_ms"][mode].append(cpu)
            reference = case["decisions"]["none"]
            require(all(case["decisions"][mode] == reference for mode in modes if mode.startswith(FEATURES)),
                    "placement optimization/filter changed schedule or resource history")
            if baseline:
                require(case["decisions"]["capacity"] == case["decisions"]["capacity-baseline"],
                        "current and baseline capacity outputs differ")
            print("{}: {}/{} measured rounds".format(policy, max(0, iteration + 1 - args.warmups), args.runs), flush=True)
        for coarse in sorted({config[3] for config in configurations.values() if config[1] == "placement"}):
            label = "coarse" if coarse else "exact-only"
            command = command_for(binary, trace, directory / (label + "-diagnostic-jobs.csv"),
                                  directory / (label + "-diagnostic-resources.csv"), args, policy,
                                  "placement", "all", coarse, True)
            _, _, stdout = execute(command, args.timeout, directory / (label + "-diagnostics.log"))
            counters = dict((name, int(calls)) for name, calls in
                            re.findall(r"^  (\w+): (\d+),", stdout, re.MULTILINE))
            require("reserve" in counters, "diagnostic reservation count unavailable")
            require(not args.require_reservations or counters["reserve"] > 0,
                    "contention run produced no reservations; reduce --nodes or increase offered load")
            case["diagnostics"][label] = {"untimed": True, "operation_calls": counters}
        case["medians_ms"] = {mode: statistics.median(values) for mode, values in case["samples_ms"].items()}
        case["cpu_medians_ms"] = {mode: statistics.median(values) for mode, values in case["cpu_samples_ms"].items()}
        case["change_percent_vs_capacity"] = {
            mode: 100 * (value / case["medians_ms"]["capacity"] - 1)
            for mode, value in case["medians_ms"].items()}
        case["change_percent_vs_placement_none"] = {
            mode: 100 * (value / case["medians_ms"]["none"] - 1)
            for mode, value in case["medians_ms"].items() if mode.startswith(FEATURES)}
        print(policy + ": " + ", ".join("{}={:.3f} ms".format(mode, value)
                                        for mode, value in case["medians_ms"].items()), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--trace", type=Path, help="Existing whole-second simple CSV; preserved without modification")
    source.add_argument("--generate-trace", action="store_true", help="Explicitly generate the seeded 20K input in the artifacts directory")
    parser.add_argument("--output", type=Path, required=True, help="JSON report; sibling artifacts directory retains inputs, outputs and logs")
    parser.add_argument("--capacity-baseline", type=Path, help="Optional pre-integration binary built with identical settings")
    parser.add_argument("--policy", choices=["all", "easy", "hybrid", "conservative"], default="all")
    parser.add_argument("--nodes", type=int, default=500)
    parser.add_argument("--jobs", type=int, default=20000)
    parser.add_argument("--seed", type=int, default=2718)
    parser.add_argument("--reservation-depth", type=int, default=32)
    parser.add_argument("--queue-depth", type=int, default=32)
    parser.add_argument("--runs", type=int, default=9)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--coarse-filter", action="store_true", help="Use coarse filtering in every placement variant")
    parser.add_argument("--compare-coarse-filter", action="store_true", help="Measure every variant with filtering OFF and ON")
    parser.add_argument("--require-reservations", action="store_true", help="Reject a contention run if untimed diagnostics find no reservations")
    parser.add_argument("--config-header", type=Path, help="Generated configuration header; otherwise discover beside binary or in installed include directory")
    parser.add_argument("--cmake-cache", type=Path, help="Defaults to CMakeCache.txt beside binary")
    parser.add_argument("--library", type=Path, action="append", default=[], help="Hash an explicitly selected shared library; repeatable, does not configure the loader")
    parser.add_argument("--build-settings", default="", help="Additional compiler/build context to retain with results")
    args = parser.parse_args()
    if min(args.runs, args.nodes, args.jobs, args.reservation_depth) < 1 or min(args.warmups, args.queue_depth) < 0:
        parser.error("runs, nodes, jobs and reservation depth must be positive; warmups and queue depth nonnegative")
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("timeout must be finite and positive")
    if args.coarse_filter and args.compare_coarse_filter:
        parser.error("--coarse-filter and --compare-coarse-filter are mutually exclusive")
    output = args.output.resolve()
    artifacts = output.parent / (output.stem + "-artifacts")
    if output.exists() or artifacts.exists():
        parser.error("report or artifacts already exist; choose a new --output path")
    output.parent.mkdir(parents=True, exist_ok=True)
    artifacts.mkdir()
    revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPO, capture_output=True, text=True) if shutil.which("git") else None
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=REPO, capture_output=True, text=True) if shutil.which("git") else None
    report = {"schema_version": 1, "status": "running", "scope": "complete process: parsing, scheduling, events, CSV output, startup/shutdown",
              "platform": platform.platform(), "machine": platform.machine(), "processor": platform.processor(),
              "python": sys.version, "revision": revision.stdout.strip() if revision else None,
              "working_tree_dirty": bool(dirty.stdout) if dirty and dirty.returncode == 0 else None,
              "build_settings": args.build_settings, "nodes": args.nodes, "warmups": args.warmups,
              "measured_runs": args.runs, "observers_in_timed_runs": False, "artifacts": str(artifacts),
              "loader_environment": {key: os.environ[key] for key in ("LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH") if key in os.environ},
              "cases": {}}
    result = 0
    try:
        run(args, report, artifacts)
        report["status"] = "passed"
    except (ValueError, KeyError, TypeError, RuntimeError, OSError, subprocess.SubprocessError) as error:
        report["status"], report["error"] = "failed", str(error)
        print(str(error), file=sys.stderr)
        result = 1
    except KeyboardInterrupt:
        report["status"] = "interrupted"
        result = 130
    finally:
        output.write_text(json.dumps(report, indent=2) + "\n")
        print("Report: {}".format(output), flush=True)
    return result


if __name__ == "__main__":
    sys.exit(main())
