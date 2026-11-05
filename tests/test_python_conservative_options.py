#!/usr/bin/env python3
"""Regression coverage for the Python conservative scheduler CLI options."""

import csv
import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path


def run_reference(source_dir, trace, outdir, *options, nodes="10"):
    # A bounded run time turns a scheduler that never terminates into a
    # failure instead of a hang.
    subprocess.run(
        [sys.executable, source_dir / "scripts" / "python_conservative_scheduler.py",
         source_dir / trace, "--nodes", nodes, "--outdir", outdir, *options],
        check=True,
        capture_output=True,
        text=True,
        timeout=60,
    )
    output = outdir / (Path(trace).stem + "_conservative_reference.csv")
    with output.open(newline="") as stream:
        return list(csv.DictReader(stream))


def main():
    source_dir = Path(__file__).resolve().parents[1]
    scheduler_spec = importlib.util.spec_from_file_location(
        "python_conservative_scheduler",
        source_dir / "scripts" / "python_conservative_scheduler.py",
    )
    scheduler_module = importlib.util.module_from_spec(scheduler_spec)
    assert scheduler_spec.loader is not None
    scheduler_spec.loader.exec_module(scheduler_module)
    scheduler = scheduler_module.ConservativeBackfillingScheduler(100)
    scheduler.current_time = 0.0
    # Same-time releases must occupy one timeline boundary.  Duplicate
    # zero-width segments would receive a protected reservation twice.
    assert scheduler.build_resource_timeline_from_running({
        1: (0.0, 10.0, 10.0, 20),
        2: (0.0, 10.0, 10.0, 30),
    }) == [(0.0, 50), (10.0, 100)]

    with tempfile.TemporaryDirectory(prefix="dr-evt-python-options-") as temp:
        outdir = Path(temp)
        horizon = run_reference(
            source_dir, "tests/test_traces/feature/planning_horizon.csv", outdir,
            "--backfill-interval", "5", "--planning-horizon", "10",
        )
        assert [(row["start_time"], row["end_time"]) for row in horizon] == [
            ("0.0", "100.0"), ("105.0", "115.0"), ("5.0", "105.0")]

        # Event-driven (no interval): the head's reservation at t=100 lies
        # beyond a 10 s horizon, so job 2 backfills at its arrival, t=1.
        event_driven = run_reference(
            source_dir, "tests/test_traces/feature/planning_horizon.csv", outdir,
            "--planning-horizon", "10",
        )
        assert [(row["start_time"], row["end_time"]) for row in event_driven] == [
            ("0.0", "100.0"), ("101.0", "111.0"), ("1.0", "101.0")]

        resolution = run_reference(
            source_dir,
            "tests/test_traces/feature/planning_time_resolution.csv",
            outdir,
            "--planning-time-resolution",
            "60",
        )
        assert resolution[2]["duration"] == "59.0"
        # It can now overlap the head's future reservation while preserving
        # the head's ten nodes at t=120.
        assert resolution[2]["start_time"] == "2.0"
        assert float(resolution[2]["end_time"]) - float(
            resolution[2]["start_time"]
        ) == 59.0

        # A job larger than the machine is rejected at submission, as in the
        # simulator, so it neither blocks the jobs behind it nor keeps the
        # periodic tick loop running forever.
        rejected = run_reference(
            source_dir, "tests/test_traces/feature/rejected_job.csv", outdir,
            "--backfill-interval", "5",
        )
        assert [(row["start_time"], row["end_time"]) for row in rejected] == [
            ("0.0", "100.0"), ("", ""), ("100.0", "200.0"), ("200.0", "300.0")]

        chained = run_reference(
            source_dir, "tests/test_traces/feature/chained_reservations.csv",
            outdir, nodes="100",
        )
        # H occupies 100 nodes from 100--150 in the temporary plan, so J is
        # reserved at 150 and C can backfill from zero through 70.
        assert [row["start_time"] for row in chained] == [
            "0.0", "0.0", "100.0", "150.0", "0.0"]

        flexible = run_reference(
            source_dir, "tests/test_traces/feature/flexible_reservation.csv",
            outdir, nodes="100",
        )
        # The 20-node candidate overlaps the 80-node head reservation at
        # t=10 while leaving the full 80 nodes available to the head.
        assert [row["start_time"] for row in flexible] == [
            "0.0", "10.0", "0.0", "30.0"]

        # H, C, and D consume the three slots at t=0. D starts in the first
        # pass; E must wait for the next timestamp instead of using a reset
        # queue-depth budget in the repeated t=0 pass.
        depth_trace = outdir / "queue_depth_per_timestamp.csv"
        depth_trace.write_text(
            "job_submit_time,num_nodes,time_limit\n"
            "0,1,10\n"
            "0,3,10\n"
            "0,2,20\n"
            "0,1,1\n"
            "0,1,1\n"
        )
        per_timestamp = run_reference(
            source_dir, depth_trace, outdir, "--backfill-queue-depth", "3",
            nodes="3",
        )
        assert per_timestamp[4]["start_time"] == "1.0"

        # A planning horizon makes the blocked head and the second job
        # unplannable (their reservations are at t=1000, beyond 50 s). With a
        # depth of 2, event-driven scheduling follows Fluxion and does not
        # charge them, so the small job behind them starts at once. Periodic
        # scheduling follows Slurm, which counts both jobs as tested, so the
        # depth is spent before the small job and it waits for the large job's
        # nodes to come back.
        horizon_trace = outdir / "horizon_counts_toward_depth.csv"
        horizon_trace.write_text(
            "job_submit_time,num_nodes,time_limit\n"
            "0,6,1000\n"
            "0,10,10\n"
            "0,8,5\n"
            "0,2,5\n"
        )
        horizon_options = ("--reservation-depth", "1",
                           "--backfill-queue-depth", "2",
                           "--planning-horizon", "50")
        event_driven = run_reference(
            source_dir, horizon_trace, outdir, *horizon_options)
        assert event_driven[3]["start_time"] == "0.0"
        periodic = run_reference(
            source_dir, horizon_trace, outdir, *horizon_options,
            "--backfill-interval", "10")
        assert float(periodic[3]["start_time"]) >= 1000.0


if __name__ == "__main__":
    main()
