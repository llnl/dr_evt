#!/usr/bin/env python3
"""Fugaku EASY scheduling and power-output checks against hand cases and C++."""

import csv
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "python_reference_scheduler.py"
spec = importlib.util.spec_from_file_location("fugaku_reference", SCRIPT)
reference = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = reference
spec.loader.exec_module(reference)

# Rows are (submit, nodes, time_limit, actual_duration). Expected starts are
# independent of both schedulers; all cases use four nodes.
CASES = [
    ("half_open", [(0, 3, 10, 10), (1, 4, 5, 5), (1, 1, 9, 9)], [0, 10, 1]),
    ("slack", [(0, 3, 10, 10), (1, 3, 5, 5), (1, 1, 20, 20)], [0, 10, 1]),
    ("conflict", [(0, 3, 10, 10), (1, 4, 5, 5), (1, 1, 20, 20)], [0, 10, 15]),
    ("actual_runtime", [(0, 3, 100, 10), (1, 4, 5, 5), (1, 1, 20, 20)], [0, 21, 1]),
    ("simultaneous", [(0, 2, 10, 10), (0, 2, 10, 10), (10, 4, 5, 5),
                      (10, 1, 2, 2)], [0, 0, 10, 15]),
    ("fractional", [(0.125, 3, 10, 10), (1.125, 4, 5, 5),
                    (1.125, 1, 9, 9)], [0.125, 10.125, 1.125]),
]


def make_jobs(rows):
    return [reference.Job(i, submit, nodes, limit, actual,
                          100.0 * (i + 1), 80.0 * (i + 1), 120.0 * (i + 1))
            for i, (submit, nodes, limit, actual) in enumerate(rows)]


def write_trace(path, rows):
    with path.open("w", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["job_submit_time", "num_nodes", "time_limit", "duration",
                         "avgpcon", "minpcon", "maxpcon"])
        for job in make_jobs(rows):
            writer.writerow([job.submit_time, job.nodes, job.duration,
                             job.actual_run_time, job.avgpcon, job.minpcon, job.maxpcon])


def resource_states(path):
    # Compare settled state at a timestamp, rather than intermediate ordering
    # of several releases and starts at that same timestamp.
    with path.open(newline="") as stream:
        return {float(row["time"]): tuple(float(row[field]) for field in
                ["free_nodes", "allocated_nodes", "avgpcon", "minpcon", "maxpcon"])
                for row in csv.DictReader(stream)}


class FugakuReferenceTests(unittest.TestCase):
    def test_hand_derived_schedules(self):
        for name, rows, starts in CASES:
            with self.subTest(case=name):
                jobs = reference.EasyBackfillingScheduler(4).simulate(make_jobs(rows))
                self.assertEqual([job.start_time for job in jobs], starts)
                self.assertEqual([job.end_time for job in jobs],
                                 [start + row[3] for start, row in zip(starts, rows)])

    def test_fugaku_parser_retains_limits_actual_durations_and_power(self):
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / "jobs.csv"
            rows = CASES[3][1]
            write_trace(trace, rows)
            jobs = reference.load_trace(str(trace))
            self.assertEqual(jobs[0].duration, 100)
            self.assertEqual(jobs[0].actual_run_time, 10)
            self.assertEqual((jobs[0].avgpcon, jobs[0].minpcon, jobs[0].maxpcon),
                             (100, 80, 120))

    def test_power_output_tracks_current_allocations(self):
        jobs = reference.EasyBackfillingScheduler(4).simulate(make_jobs(CASES[0][1]))
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "resources.csv"
            reference.write_resource_trace(jobs, str(output), 4)
            states = resource_states(output)
            self.assertEqual(states[1], (0, 4, 400, 320, 480))
            self.assertEqual(states[10], (0, 4, 200, 160, 240))
            self.assertEqual(states[15], (4, 0, 0, 0, 0))

    @unittest.skipUnless(os.environ.get("DR_EVT_SIMULATOR"), "set DR_EVT_SIMULATOR for C++ comparison")
    def test_native_schedule_and_power_equivalence(self):
        simulator = os.environ["DR_EVT_SIMULATOR"]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, rows, starts in CASES:
                with self.subTest(case=name):
                    trace = root / (name + ".csv")
                    write_trace(trace, rows)
                    # Run the real Fugaku CLI, including isolated module loading.
                    result = subprocess.run([sys.executable, "-I", str(SCRIPT), str(trace),
                                             "--nodes", "4", "--outdir", str(root)],
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    schedule = root / (name + "_native.csv")
                    resources = root / (name + "_native_resources.csv")
                    result = subprocess.run([simulator, str(trace), "--total_nodes", "4",
                                             "--trace_type", "pcon", "--trace_format", "simple",
                                             "--run_time_mode", "actual", "--msec_output",
                                             "--outfile", str(schedule), "--resource_trace", str(resources)],
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    with schedule.open(newline="") as stream:
                        native = list(csv.DictReader(stream))
                    self.assertEqual([float(job["begin_time"]) for job in native], starts)
                    self.assertEqual([float(job["end_time"]) for job in native],
                                     [start + row[3] for start, row in zip(starts, rows)])
                    expected_states = resource_states(root / (name + "_reference_resources.csv"))
                    native_states = resource_states(resources)
                    for time, expected in expected_states.items():
                        self.assertIn(time, native_states)
                        for actual, wanted in zip(native_states[time], expected):
                            self.assertAlmostEqual(actual, wanted, places=6)


if __name__ == "__main__":
    unittest.main()
