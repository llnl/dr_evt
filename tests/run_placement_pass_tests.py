#!/usr/bin/env python3
"""End-to-end checks for guarded per-pass FCFS placement accounting."""

import argparse
import csv
import random
import re
import subprocess
import tempfile
from pathlib import Path
from read_build_config import reservation_barrier


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("simulator", type=Path)
    parser.add_argument("--config-header", type=Path)
    args = parser.parse_args()
    simulator = args.simulator.resolve()
    legacy_barrier, header = reservation_barrier(simulator, args.config_header)
    has_protobuf = re.search(r"^\s*#\s*define\s+DR_EVT_HAS_PROTOBUF\s+1\s*$",
                             header.read_text(), re.MULTILINE) is not None
    with tempfile.TemporaryDirectory(prefix="dr-evt-placement-pass-") as tmp:
        root = Path(tmp)

        def run(name, jobs, policy="hybrid", q=1, enabled=True, extra=()):
            trace = root / (name + ".csv")
            output = root / (name + "-out.csv")
            with trace.open("w") as stream:
                stream.write("job_submit_time,num_nodes,time_limit\n")
                for row in jobs:
                    stream.write(",".join(map(str, row)) + "\n")
            command = [str(simulator), "--infile", str(trace), "--outfile", str(output),
                       "--total_nodes", "4", "--trace_format", "simple",
                       "--timestamp_format", "epoch", "--priority_policy", "fcfs",
                       "--queue_impl", "circular", "--backfill_policy", policy,
                       "--reservation_depth", "32", "--backfill_queue_depth", str(q)]
            if enabled:
                command.append("--per_pass_accounting")
            result = subprocess.run(command[:1] + list(extra) + command[1:], capture_output=True,
                                    text=True, timeout=60)
            if result.returncode:
                raise AssertionError(name + ": " + result.stdout + result.stderr)
            with output.open() as stream:
                return sorted((float(row["job_submit_time"]), int(row["num_nodes"]),
                               float(row["time_limit"]), float(row["begin_time"]),
                               float(row["end_time"])) for row in csv.DictReader(stream))

        # A full-machine runner followed by two waiting jobs triggers the
        # reservation-only guard at Q=1. Completion still starts both jobs.
        for policy in ("easy", "hybrid", "conservative", "none"):
            rows = run("retry-" + policy, [(0, 4, 10), (0, 4, 5), (0, 4, 6)], policy)
            assert rows == [(0, 4, 5, 10, 15), (0, 4, 6, 15, 21), (0, 4, 10, 0, 10)], rows

        # Unlimited Q preserves the aggregate resource-time baseline, while
        # bounded Q=32/64 must complete a substantial mixed-size workload.
        rng = random.Random(7351)
        jobs = [(i // 24, rng.randint(1, 4), rng.randint(1, 30)) for i in range(2000)]
        unlimited = run("unlimited-pass", jobs, q=0)
        if not legacy_barrier:
            baseline = run("baseline", jobs, q=0, enabled=False)
            assert unlimited == baseline
        for q in (32, 64):
            rows = run("depth-" + str(q), jobs, q=q)
            assert len(rows) == 2000
            events = {}
            for submit, nodes, duration, start, end in rows:
                assert start >= submit and end - start == duration
                events[start] = events.get(start, 0) + nodes
                events[end] = events.get(end, 0) - nodes
            occupied = 0
            for time in sorted(events):
                occupied += events[time]
                assert 0 <= occupied <= 4, (q, time, occupied)
            assert occupied == 0

        # Protobuf-enabled builds also accept the option in configuration.
        if has_protobuf:
            config = root / "pass.prototext"
            config.write_text("per_pass_accounting: true\n")
            assert run("config", [(0, 4, 10), (0, 4, 5)], enabled=False,
                       extra=("--config", str(config))) == [
                           (0, 4, 5, 10, 15), (0, 4, 10, 0, 10)]
    print("placement-pass integration checks passed (2,000-job Q=32/64, R=32)")


if __name__ == "__main__":
    main()
