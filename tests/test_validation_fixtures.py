#!/usr/bin/env python3
"""Check compact native-scheduler validation fixtures."""

import csv
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1] / "validation" / "fixtures"


def rows(name):
    with (ROOT / name).open(newline="") as stream:
        return list(csv.DictReader(stream))


class ValidationFixtures(unittest.TestCase):
    def test_complete_workload_and_schedules(self):
        self.assertEqual(len(rows("jobs.csv")), 2000)
        for name in ("slurm-capacity.csv", "slurm-placement.csv",
                     "openpbs-capacity.csv", "openpbs-placement.csv"):
            records = rows(name)
            self.assertEqual(len(records), 2000, name)
            self.assertEqual([int(row["job_id"]) for row in records],
                             list(range(1, 2001)), name)

    def test_exact_matches_and_known_capacity_difference(self):
        jobs = rows("jobs.csv")
        self.assertEqual([int(row["job_id"]) for row in jobs],
                         list(range(1, 2001)))
        self.assertEqual(int(jobs[0]["submit_time"]), 1597836579)
        self.assertTrue(all(int(row["submit_time"]) >= 1597836579
                            for row in jobs))
        pbs = rows("openpbs-capacity.csv")
        self.assertTrue(all(row["start"] and row["end"] for row in pbs))
        self.assertTrue(all(not row["node_ids"] for row in pbs))

        for name in ("slurm-placement.csv", "openpbs-placement.csv"):
            records = rows(name)
            self.assertTrue(all(row["node_ids"] for row in records), name)
            intervals = [[] for _ in range(756)]
            for job, row in zip(jobs, records):
                node_ids = [int(node) for node in row["node_ids"].split(";")]
                self.assertEqual(len(node_ids), int(job["nodes"]), row["job_id"])
                self.assertEqual(len(node_ids), len(set(node_ids)), row["job_id"])
                self.assertTrue(all(0 <= node < 756 for node in node_ids),
                                row["job_id"])
                start, end = float(row["start"]), float(row["end"])
                self.assertGreaterEqual(start, float(job["submit_time"]), row["job_id"])
                self.assertEqual(end - start, float(job["actual_runtime"]),
                                 row["job_id"])
                for node in node_ids:
                    self.assertTrue(all(end <= other_start or start >= other_end
                                        for other_start, other_end in intervals[node]),
                                    f"{name} node {node} job {row['job_id']}")
                    intervals[node].append((start, end))

        slurm = rows("slurm-capacity.csv")
        differences = [int(row["job_id"]) for row in slurm
                       if float(row["slurm_start"]) != float(row["dr_evt_start"]) or
                       float(row["slurm_end"]) != float(row["dr_evt_end"])]
        self.assertEqual(differences, [560, 614])


if __name__ == "__main__":
    unittest.main()
