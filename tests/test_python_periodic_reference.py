#!/usr/bin/env python3
"""Exercise the standalone reference's input, policy, and output contract."""

import csv
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REFERENCE = Path(__file__).resolve().parents[1] / "scripts/python_periodic_scheduler.py"


class PeriodicReferenceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)
        self.trace = self.work / "jobs.csv"
        self.output = self.work / "schedule.csv"

    def run_reference(self, jobs, *options, success=True):
        with self.trace.open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["job_submit_time", "num_nodes", "time_limit", "actual_run_time"])
            writer.writerows(jobs)
        result = subprocess.run(
            [sys.executable, str(REFERENCE), str(self.trace), "--outfile", str(self.output),
             *options], text=True, capture_output=True, timeout=5)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
            with self.output.open() as stream:
                return list(csv.DictReader(stream))
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.output.exists())
        return result

    def test_sorted_fcfs_with_input_row_ids_and_finite_prefix(self):
        rows = self.run_reference([(7, 4, 1), (2.3, 4, 1), (2.3, 4, 1)],
                                  "--dispatch-queue-depth", "1")
        self.assertEqual([int(row["job_idx"]) for row in rows], [0, 1, 2])
        self.assertEqual([float(row["begin_time"]) for row in rows], [15, 5, 10])

    def test_arrival_passes_preserve_phase(self):
        rows = self.run_reference([(0, 4, 1)] * 3 + [(7, 4, 1)],
                                  "--dispatch-queue-depth", "1", "--dispatch-on-events")
        self.assertEqual([float(row["begin_time"]) for row in rows], [0, 5, 7, 10])

    def test_actual_runtime_and_limit_clamping(self):
        rows = self.run_reference([(0, 4, 20, 2), (7, 4, 2, 8)], "--run-time-mode", "actual")
        self.assertEqual([(float(row["begin_time"]), float(row["end_time"]))
                          for row in rows], [(5, 7), (10, 12)])

    def test_strict_easy_rejects_equal_boundary(self):
        rows = self.run_reference([(0, 3, 10), (1, 4, 2), (1, 1, 10)],
                                  "--backfill-policy", "easy", "--dispatch-interval", "1")
        self.assertEqual([float(row["begin_time"]) for row in rows], [1, 11, 13])

    def test_epoch_times_and_empty_input(self):
        rows = self.run_reference([(1000000002.3, 4, 1)])
        self.assertEqual(float(rows[0]["begin_time"]), 1000000005)
        rows = self.run_reference([])
        self.assertEqual(rows, [])

    def test_unsupported_policy_and_impossible_job_are_rejected(self):
        for options in [("--backfill-policy", "hybrid"),
                        ("--backfill-policy", "easy", "--dispatch-on-events"),
                        ("--dispatch-interval", "0")]:
            self.run_reference([(0, 4, 1)], *options, success=False)
        self.run_reference([(0, 5, 1)], success=False)


if __name__ == "__main__":
    unittest.main()
