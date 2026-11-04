#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

"""Regression test for the multi-system spatial-backfill scheduler.

Compares the C++ driver (``DR_EVT_MSBF_SIM``) against the Python reference
in this directory on generated traces for every spatial-backfill and
placement policy, checks resource invariants on the reference output, and
verifies that one system reproduces the stock simulator
(``DR_EVT_SIMULATOR``) and the stock Python oracle.

Run directly or through CTest.  Temporary files go to a fresh directory.
"""

import csv
import itertools
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
REFERENCE = HERE.parent / "python_multi_system_reference.py"
STOCK_ORACLE = REPO / "scripts" / "python_reference_scheduler.py"

SPATIAL = ("target_only", "every_system", "global_shadow")
PLACEMENT = ("best_fit", "first_fit", "worst_fit")


def require_env(name):
    value = os.environ.get(name)
    if not value or not Path(value).exists():
        print(f"SKIP: {name} is not set to an existing binary", file=sys.stderr)
        sys.exit(77)
    return value


def write_trace(path, jobs):
    with open(path, "w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["job_submit_time", "num_nodes", "time_limit",
                         "actual_run_time"])
        for submit, nodes, limit, actual in jobs:
            writer.writerow([submit, nodes, limit, actual])


def generate_trace(seed, count, max_nodes, mean_gap, burst_every=0):
    """Integer-valued trace spanning small to largest-system-only jobs."""
    rng = random.Random(seed)
    jobs = []
    submit = 0
    for index in range(count):
        if burst_every and index % burst_every == 0 and index:
            submit += int(rng.expovariate(1.0 / mean_gap)) * burst_every
        elif not burst_every:
            submit += int(rng.expovariate(1.0 / mean_gap))
        draw = rng.random()
        if draw < 0.6:
            nodes = rng.randint(1, max(1, max_nodes // 8))
        elif draw < 0.9:
            nodes = rng.randint(1, max(1, max_nodes // 2))
        else:
            nodes = rng.randint(max(1, max_nodes // 2), max_nodes)
        limit = rng.choice([60, 300, 900, 1800, 3600, 7200])
        actual = max(1, int(limit * rng.uniform(0.2, 1.0)))
        jobs.append((submit, nodes, limit, actual))
    return jobs


def run(command, cwd=None, expect_failure=False):
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True,
                            check=False)
    if expect_failure:
        assert result.returncode != 0, (command, result.stdout, result.stderr)
    else:
        assert result.returncode == 0, (command, result.stdout, result.stderr)
    return result


def read_reference(path):
    """Scheduled jobs only; rejected jobs have empty start times."""
    rows = {}
    with open(path, newline="") as stream:
        for row in csv.DictReader(stream):
            if not row["start_time"]:
                continue
            rows[int(row["job_idx"])] = (float(row["start_time"]),
                                        float(row["end_time"]),
                                        int(row["nodes"]), row["system"])
    return rows


def read_cpp(schedule_path, placement_path):
    placements = {}
    with open(placement_path, newline="") as stream:
        for row in csv.DictReader(stream):
            placements[int(row["job_id"])] = row["system"]
    rows = {}
    with open(schedule_path, newline="") as stream:
        for index, row in enumerate(csv.DictReader(stream)):
            rows[index] = (float(row["begin_time"]), float(row["end_time"]),
                           int(row["num_nodes"]), placements[index])
    return rows


def compare(reference, cpp, label):
    assert set(reference) == set(cpp), f"{label}: job sets differ"
    mismatches = []
    for job in sorted(reference):
        ref_start, ref_end, ref_nodes, ref_system = reference[job]
        cpp_start, cpp_end, cpp_nodes, cpp_system = cpp[job]
        if (abs(ref_start - cpp_start) > 1e-3 or abs(ref_end - cpp_end) > 1e-3
                or ref_nodes != cpp_nodes or ref_system != cpp_system):
            mismatches.append((job, reference[job], cpp[job]))
    assert not mismatches, f"{label}: {len(mismatches)} mismatches, first " \
                           f"{mismatches[:5]}"


def check_invariants(reference, resources_path, systems):
    capacity = dict(systems)
    for job, (start, end, nodes, system) in reference.items():
        assert nodes <= capacity[system], (job, nodes, system)
        assert end > start >= 0
    with open(resources_path, newline="") as stream:
        for row in csv.DictReader(stream):
            for name, cap in systems:
                allocated = int(row[f"allocated_{name}"])
                free = int(row[f"free_{name}"])
                assert 0 <= allocated <= cap, (row["time"], name, allocated)
                assert allocated + free == cap


def reference_outputs(trace, outdir):
    base = trace.stem
    return (outdir / f"{base}_msbf_reference.csv",
            outdir / f"{base}_msbf_reference_resources.csv")


# Hand-derived cases where the three window rules diverge. Each job is
# (submit, nodes, time_limit, actual_run_time); expected schedules are
# {policy: {job: (start, end, system)}} with best_fit placement, worked out
# by hand from the rules in the README, independent of both implementations.
#
# Shared setup of the first two cases: A:10 holds job 0 (10 nodes, ends
# 100); B:20 holds job 1 (15 nodes, ends 200), leaving 5 free. Head job 2
# (10 nodes) is blocked everywhere, so S_A = 100, S_B = 200, T* = 100 on A.
# Candidate job 3 (5 nodes) fits B now.
HAND_CASES = [
    ("long_candidate_on_non_target",
     # Limit 1000: only target_only (window inf on B) lets it start now.
     [("A", 10), ("B", 20)],
     [(0, 10, 100, 100), (0, 15, 200, 200), (1, 10, 50, 50),
      (1, 5, 1000, 1000)],
     {"target_only": {0: (0, 100, "A"), 1: (0, 200, "B"),
                      2: (100, 150, "A"), 3: (1, 1001, "B")},
      "every_system": {0: (0, 100, "A"), 1: (0, 200, "B"),
                       2: (100, 150, "A"), 3: (1, 1001, "B")},
      "global_shadow": {0: (0, 100, "A"), 1: (0, 200, "B"),
                        2: (100, 150, "A"), 3: (1, 1001, "B")}}),
    ("medium_candidate_on_non_target",
     # Limit 150: ends at 151, before S_B = 200 but after T* = 100, so
     # every_system admits it and global_shadow does not.
     [("A", 10), ("B", 20)],
     [(0, 10, 100, 100), (0, 15, 200, 200), (1, 10, 50, 50),
      (1, 5, 150, 150)],
     {"target_only": {0: (0, 100, "A"), 1: (0, 200, "B"),
                      2: (100, 150, "A"), 3: (1, 151, "B")},
      "every_system": {0: (0, 100, "A"), 1: (0, 200, "B"),
                       2: (100, 150, "A"), 3: (1, 151, "B")},
      "global_shadow": {0: (0, 100, "A"), 1: (0, 200, "B"),
                        2: (100, 150, "A"), 3: (1, 151, "B")}}),
    ("inadmissible_system_is_free",
     # B:4 can never hold the 10-node head, so S_B = inf and every rule,
     # including global_shadow, lets a long 4-node candidate use B now.
     [("A", 10), ("B", 4)],
     [(0, 10, 100, 100), (1, 10, 50, 50), (1, 4, 1000, 1000)],
     {"target_only": {0: (0, 100, "A"), 1: (100, 150, "A"),
                      2: (1, 1001, "B")},
      "every_system": {0: (0, 100, "A"), 1: (100, 150, "A"),
                       2: (1, 1001, "B")},
      "global_shadow": {0: (0, 100, "A"), 1: (100, 150, "A"),
                        2: (1, 1001, "B")}}),
    ("early_drain_on_non_target",
     # A:20 holds job 0 (20 nodes, ends 100). B:20 holds job 1 (12 nodes,
     # limit 200 but actually ends at 50), leaving 8 free. Head job 2
     # (15 nodes) is blocked: S_A = 100, S_B = 200, T* = 100 on A.
     # Candidate job 3 (8 nodes, limit 1000) fits B now. target_only
     # backfills it on B; when job 1 drains early at 50, B has only 12
     # free and the head waits for A at 100. every_system keeps B's
     # projection and the head starts on B at 50; job 3 follows at 70.
     [("A", 20), ("B", 20)],
     [(0, 20, 100, 100), (0, 12, 200, 50), (1, 15, 20, 20),
      (1, 8, 1000, 1000)],
     {"target_only": {0: (0, 100, "A"), 1: (0, 50, "B"),
                      2: (100, 120, "A"), 3: (1, 1001, "B")},
      "every_system": {0: (0, 100, "A"), 1: (0, 50, "B"),
                       2: (50, 70, "B"), 3: (70, 1070, "B")},
      "global_shadow": {0: (0, 100, "A"), 1: (0, 50, "B"),
                        2: (100, 120, "A"), 3: (1, 1001, "B")}}),
]


def check_against_expected(actual, expected, label):
    assert set(actual) == set(expected), f"{label}: job sets differ"
    for job, (start, end, system) in expected.items():
        got_start, got_end, _, got_system = actual[job]
        assert (abs(got_start - start) < 1e-6 and abs(got_end - end) < 1e-6
                and got_system == system), \
            f"{label}: job {job} expected {(start, end, system)}, " \
            f"got {(got_start, got_end, got_system)}"


def run_hand_cases(tmp, msbf, python):
    checks = 0
    for name, systems, jobs, expected in HAND_CASES:
        trace = tmp / f"hand_{name}.csv"
        write_trace(trace, jobs)
        systems_arg = ",".join(f"{n}:{c}" for n, c in systems)
        for spatial in SPATIAL:
            outdir = tmp / f"hand_{name}_{spatial}"
            outdir.mkdir()
            run([python, "-I", str(REFERENCE), str(trace), "--systems",
                 systems_arg, "--spatial_backfill", spatial,
                 "--placement_policy", "best_fit", "--outdir", str(outdir)])
            run([msbf, "-i", str(trace), "-o", str(outdir / "cpp.csv"),
                 "--trace_format", "simple", "--systems", systems_arg,
                 "--spatial_backfill", spatial,
                 "--placement_policy", "best_fit"])
            schedule, _ = reference_outputs(trace, outdir)
            reference = read_reference(schedule)
            cpp = read_cpp(outdir / "cpp.csv", outdir / "cpp_placements.csv")
            label = f"hand/{name}/{spatial}"
            check_against_expected(reference, expected[spatial],
                                   label + "/reference")
            check_against_expected(cpp, expected[spatial], label + "/cpp")
            checks += 1
    return checks


def main():
    msbf = require_env("DR_EVT_MSBF_SIM")
    stock = os.environ.get("DR_EVT_SIMULATOR")
    python = sys.executable

    cases = [
        ("three_systems", [("big", 128), ("mid", 64), ("small", 32)],
         generate_trace(seed=11, count=400, max_nodes=128, mean_gap=120)),
        ("two_systems", [("a", 100), ("b", 40)],
         generate_trace(seed=23, count=300, max_nodes=100, mean_gap=60)),
        ("bursty", [("x", 96), ("y", 48), ("z", 48)],
         generate_trace(seed=37, count=240, max_nodes=96, mean_gap=600,
                        burst_every=8)),
    ]

    comparisons = 0
    with tempfile.TemporaryDirectory(prefix="msbf_test_") as tmp:
        tmp = Path(tmp)
        hand_checks = run_hand_cases(tmp, msbf, python)
        for name, systems, jobs in cases:
            trace = tmp / f"{name}.csv"
            write_trace(trace, jobs)
            systems_arg = ",".join(f"{n}:{c}" for n, c in systems)
            policies = list(itertools.product(SPATIAL, PLACEMENT))
            policies.append(("none", "best_fit"))
            for spatial, placement in policies:
                outdir = tmp / f"{name}_{spatial}_{placement}"
                outdir.mkdir()
                ref_cmd = [python, "-I", str(REFERENCE), str(trace),
                           "--systems", systems_arg,
                           "--placement_policy", placement,
                           "--outdir", str(outdir)]
                cpp_cmd = [msbf, "-i", str(trace), "-o",
                           str(outdir / "cpp.csv"), "--trace_format", "simple",
                           "--systems", systems_arg,
                           "--placement_policy", placement]
                if spatial == "none":
                    ref_cmd.append("--no_backfill")
                    cpp_cmd += ["--backfill_policy", "none"]
                else:
                    ref_cmd += ["--spatial_backfill", spatial]
                    cpp_cmd += ["--spatial_backfill", spatial]
                run(ref_cmd)
                run(cpp_cmd)
                schedule, resources = reference_outputs(trace, outdir)
                reference = read_reference(schedule)
                assert len(reference) == len(jobs)
                check_invariants(reference, resources, systems)
                cpp = read_cpp(outdir / "cpp.csv",
                               outdir / "cpp_placements.csv")
                compare(reference, cpp, f"{name}/{spatial}/{placement}")
                comparisons += 1

        # One system: the reference must match the stock oracle, and the
        # driver must match the stock simulator byte for byte.
        trace = tmp / "three_systems.csv"
        one = tmp / "one_system"
        one.mkdir()
        run([python, "-I", str(REFERENCE), str(trace), "--systems", "one:128",
             "--outdir", str(one)])
        run([python, "-I", str(STOCK_ORACLE), str(trace), "--nodes", "128",
             "--outdir", str(one)])
        reference = read_reference(one / "three_systems_msbf_reference.csv")
        with open(one / "three_systems_reference.csv", newline="") as stream:
            for row in csv.DictReader(stream):
                job = int(row["job_idx"])
                assert abs(reference[job][0] - float(row["start_time"])) < 1e-3
                assert abs(reference[job][1] - float(row["end_time"])) < 1e-3
        run([msbf, "-i", str(trace), "-o", str(one / "msbf.csv"),
             "--trace_format", "simple", "--systems", "one:128"])
        if stock and Path(stock).exists():
            run([stock, "-i", str(trace), "-o", str(one / "stock.csv"),
                 "--trace_format", "simple", "--total_nodes", "128"])
            assert (one / "msbf.csv").read_bytes() == \
                (one / "stock.csv").read_bytes(), "one-system output differs"
        else:
            print("note: DR_EVT_SIMULATOR not set; skipped the stock-binary "
                  "comparison", file=sys.stderr)

        # A job larger than the largest system but within the summed
        # capacity (192) is rejected at submission by both implementations
        # and the run continues without it.
        bad = tmp / "oversized.csv"
        write_trace(bad, [(0, 10, 100, 100), (5, 150, 100, 100),
                          (6, 20, 100, 100)])
        result = run([python, "-I", str(REFERENCE), str(bad), "--systems",
                      "a:128,b:64", "--outdir", str(tmp)])
        assert "Job 1 rejected" in result.stderr and "can never run" in \
            result.stderr, result.stderr
        reference = read_reference(tmp / "oversized_msbf_reference.csv")
        assert set(reference) == {0, 2}, reference
        result = run([msbf, "-i", str(bad), "-o", str(tmp / "bad.csv"),
                      "--trace_format", "simple", "--systems", "a:128,b:64"])
        assert "Job 1 rejected" in result.stderr and \
            "declined by the scheduler" in result.stderr, result.stderr
        with open(tmp / "bad.csv", newline="") as stream:
            rows = list(csv.DictReader(stream))
        assert [int(r["num_nodes"]) for r in rows] == [10, 20], rows
        with open(tmp / "bad_placements.csv", newline="") as stream:
            placed = {int(r["job_id"]): r["system"]
                      for r in csv.DictReader(stream)}
        assert placed == {0: reference[0][3], 2: reference[2][3]}, placed

        # Unsupported settings are refused up front.
        result = run([msbf, "-i", str(trace), "-o", str(tmp / "x.csv"),
                      "--trace_format", "simple", "--systems", "a:128",
                      "--backfill_policy", "conservative"], expect_failure=True)
        assert "not supported" in result.stderr
        result = run([msbf, "-i", str(trace), "-o", str(tmp / "x.csv"),
                      "--trace_format", "simple", "--systems", "a:128",
                      "--total_nodes", "64"], expect_failure=True)
        assert "must equal the sum" in result.stderr

    print(f"PASS: {hand_checks} hand-derived policy checks, {comparisons} "
          f"policy comparisons, one-system equivalence, rejections")
    return 0


if __name__ == "__main__":
    sys.exit(main())
