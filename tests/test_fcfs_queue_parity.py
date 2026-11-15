#!/usr/bin/env python3
"""The circular and deque wait queues must schedule identically in every mode.

The FCFS schedulers are templates over the queue storage, so a circular and a
deque run of the same configuration differ only in how waiting jobs are
stored. This runs one configuration per scheduling feature under both queues
and requires byte-identical output files. It also checks that the queues
without these features (multimap and block) are still rejected.

Usage: test_fcfs_queue_parity.py SIMULATOR
"""
import hashlib
import pathlib
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

SIMULATOR = str(pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/simulator").resolve())
ROOT = pathlib.Path(__file__).resolve().parents[1]
TRACES = {"xlarge": ("tests/test_traces/scale/xlarge_500jobs.csv", 400),
          "huge": ("tests/test_traces/scale/huge_2000jobs.csv", 500)}

FCFS = ["--priority_policy", "fcfs"]
CONSERVATIVE_ORDER = ["--priority_policy", "fcfs_conservative"]
EASY = ["--backfill_policy", "easy"]
NONE = ["--backfill_policy", "none"]
CONSERVATIVE = ["--backfill_policy", "conservative"]


def hybrid(depth=4):
    return ["--backfill_policy", "hybrid", "--reservation_depth", str(depth)]


def periodic(seconds=30):
    return ["--backfill_interval", str(seconds)]


def dispatch(seconds=30):
    return ["--dispatch_interval", str(seconds)]


def depth(limit):
    return ["--backfill_queue_depth", str(limit)]


PLACEMENT = ["--resource_model", "placement"]
GROUP = ["--priority_policy", "fcfs_1s_evt_group"]

# label: (options, traces)
CASES = {
    # Event-driven EASY, hybrid, and conservative backfill.
    "easy": (FCFS + EASY, ("xlarge", "huge")),
    "easy-depth": (FCFS + EASY + depth(8), ("xlarge",)),
    "easy-horizon": (FCFS + EASY + ["--planning_horizon", "3600"], ("xlarge",)),
    "none": (FCFS + NONE, ("xlarge",)),
    "hybrid": (FCFS + hybrid(), ("xlarge", "huge")),
    "hybrid-depth": (FCFS + hybrid() + depth(8), ("xlarge",)),
    "hybrid-horizon": (FCFS + hybrid() + ["--planning_horizon", "3600"], ("xlarge",)),
    "conservative": (FCFS + CONSERVATIVE, ("xlarge",)),
    "conservative-depth": (FCFS + CONSERVATIVE + depth(16), ("xlarge", "huge")),
    "fcfs_conservative-easy": (CONSERVATIVE_ORDER + EASY, ("xlarge",)),
    "fcfs_conservative-conservative": (CONSERVATIVE_ORDER + CONSERVATIVE, ("xlarge",)),
    # Separate periodic backfill.
    "periodic-easy": (FCFS + EASY + periodic(), ("xlarge",)),
    "periodic-easy-depth": (FCFS + EASY + periodic() + depth(4), ("xlarge",)),
    "periodic-hybrid": (FCFS + hybrid() + periodic(), ("xlarge", "huge")),
    "periodic-hybrid-depth": (FCFS + hybrid() + periodic() + depth(8), ("xlarge",)),
    "periodic-conservative": (FCFS + CONSERVATIVE + periodic(), ("xlarge",)),
    "periodic-conservative-horizon": (
        FCFS + CONSERVATIVE + periodic() + ["--planning_horizon", "3600"], ("xlarge",)),
    # Fixed periodic dispatch.
    "dispatch-none": (FCFS + NONE + dispatch(), ("xlarge",)),
    "dispatch-easy": (FCFS + EASY + dispatch(), ("xlarge",)),
    "dispatch-hybrid": (FCFS + hybrid() + dispatch(), ("xlarge", "huge")),
    "dispatch-conservative": (FCFS + CONSERVATIVE + dispatch(), ("xlarge",)),
    "dispatch-conservative-depth": (
        FCFS + CONSERVATIVE + dispatch() + ["--dispatch_queue_depth", "16"], ("xlarge",)),
    "dispatch-on-events": (
        FCFS + hybrid() + dispatch() + ["--dispatch_on_events"], ("xlarge",)),
    # Per-pass accounting and the one-second event group.
    "pass-none": (FCFS + NONE + ["--per_pass_accounting"], ("xlarge",)),
    "pass-easy": (FCFS + EASY + ["--per_pass_accounting"], ("xlarge",)),
    "pass-hybrid": (FCFS + hybrid() + ["--per_pass_accounting"], ("xlarge", "huge")),
    "pass-conservative-depth": (
        FCFS + CONSERVATIVE + ["--per_pass_accounting"] + depth(8), ("xlarge",)),
    "group-hybrid": (GROUP + hybrid(), ("xlarge",)),
    "group-easy": (GROUP + EASY, ("xlarge",)),
    "group-conservative": (GROUP + CONSERVATIVE, ("xlarge",)),
    # License-off diagnostic.
    "no-reservations-hybrid": (
        FCFS + hybrid() + periodic() + ["--diagnostic_no_reservations"], ("xlarge",)),
    "no-reservations-conservative": (
        FCFS + CONSERVATIVE + periodic() + ["--diagnostic_no_reservations"], ("xlarge",)),
    # Exact placement.
    "placement-easy": (FCFS + EASY + PLACEMENT, ("xlarge",)),
    "placement-hybrid": (FCFS + hybrid() + PLACEMENT, ("xlarge", "huge")),
    "placement-conservative": (FCFS + CONSERVATIVE + PLACEMENT, ("xlarge",)),
    "placement-high-order": (
        FCFS + hybrid() + PLACEMENT + ["--placement_order", "high"], ("xlarge",)),
    "placement-coarse-filter": (
        FCFS + hybrid() + PLACEMENT + ["--placement_coarse_filter"], ("xlarge",)),
    "placement-periodic": (FCFS + hybrid() + PLACEMENT + periodic(), ("xlarge",)),
    "placement-periodic-easy": (FCFS + EASY + PLACEMENT + periodic(), ("xlarge",)),
    "placement-group": (GROUP + hybrid() + PLACEMENT, ("xlarge",)),
}


def run(work, label, queue, trace_name, options):
    trace, nodes = TRACES[trace_name]
    output = work / f"{label}.{trace_name}.{queue}.csv"
    command = [SIMULATOR, trace, "--total_nodes", str(nodes), "--run_time_mode", "limit",
               "--max_jobs", "999999", "--trace_format", "simple", "--outfile", str(output),
               "--queue_impl", queue, *options]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=600)
    assert result.returncode == 0, f"{label} ({queue}): {result.stdout}{result.stderr}"
    return hashlib.sha256(output.read_bytes()).hexdigest()


with tempfile.TemporaryDirectory(prefix="dr_evt_queue_parity_") as directory:
    work = pathlib.Path(directory)
    jobs = [(label, trace_name, options)
            for label, (options, trace_names) in CASES.items() for trace_name in trace_names]

    def compare(job):
        label, trace_name, options = job
        circular = run(work, label, "circular", trace_name, options)
        deque = run(work, label, "deque", trace_name, options)
        return label, trace_name, circular == deque

    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(compare, jobs))
    mismatched = [(label, trace) for label, trace, same in results if not same]
    assert not mismatched, f"circular and deque schedules differ: {mismatched}"

    # The multimap and block queues implement EASY only.
    rejected = 0
    for queue in ("multimap", "block"):
        for options in (hybrid() + periodic(), FCFS + hybrid() + dispatch(),
                        FCFS + EASY + ["--per_pass_accounting"], GROUP + EASY,
                        FCFS + EASY + PLACEMENT):
            trace, nodes = TRACES["xlarge"]
            result = subprocess.run(
                [SIMULATOR, trace, "--total_nodes", str(nodes), "--trace_format", "simple",
                 "--queue_impl", queue, *options], cwd=ROOT, capture_output=True, text=True,
                timeout=60)
            assert result.returncode != 0, f"{queue} accepted {options}"
            assert "circular or deque" in result.stdout + result.stderr, (
                queue, options, result.stdout + result.stderr)
            rejected += 1

print(f"FCFS queue parity: {len(results)} circular/deque runs identical, "
      f"{rejected} unsupported combinations rejected")
