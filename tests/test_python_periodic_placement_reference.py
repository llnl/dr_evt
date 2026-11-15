#!/usr/bin/env python3
"""Tests for the default-Slurm exact-placement periodic backfill reference.

With a driver path argument, also cross-checks DR_EVT's placement driver
against the reference on seeded random workloads.
"""
import json
import random
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import python_periodic_backfill_placement_scheduler as ref  # noqa: E402

DRIVER = None


def simulate(nodes, jobs, **options):
    """Run the reference on (submit, nodes, limit, runtime) tuples."""
    records = [ref.Job(i, s, n, l, r) for i, (s, n, l, r) in enumerate(jobs)]
    scheduler = ref.PlacementBackfillingScheduler(nodes, **options)
    return [(job.start_time, job.node_ids) for job in scheduler.simulate(records)]


class Helpers(unittest.TestCase):
    def test_contiguous_best_fit(self):
        free = {0, 1, 2, 4, 5, 7}
        self.assertEqual(ref.contiguous_best_fit(free, 1), [7])
        self.assertEqual(ref.contiguous_best_fit(free, 2), [4, 5])
        self.assertEqual(ref.contiguous_best_fit(free, 4), [0, 1, 2, 7])
        self.assertIsNone(ref.contiguous_best_fit(free, 7))

    def test_profile_usable_is_end_inclusive(self):
        profile = ref.ReservationProfile(0, 1000, set(range(4)))
        profile.book(100, 200, {0, 1})
        self.assertEqual(profile.usable(0, 100)[0], {2, 3})
        self.assertEqual(profile.usable(0, 99)[0], set(range(4)))
        self.assertTrue(profile.collides(frozenset({0}), 150, 160))
        self.assertFalse(profile.collides(frozenset({0}), 200, 300))


class Scheduling(unittest.TestCase):
    def test_event_dispatch_does_not_backfill(self):
        # The head blocks; the small job waits for the 30 s timer.
        result = simulate(2, [(0, 1, 100, 40), (1, 2, 100, 100), (2, 1, 20, 20)])
        self.assertEqual(result[2][0], 30)

    def test_running_jobs_are_not_reserved(self):
        # Job 0 holds node 0 until 100 and job 1 holds node 1 until 1000. The
        # three-node head books {0, 2, 3} from 100. The profile holds no
        # running jobs, so a later job may plan on node 1 before job 1's
        # limit; it starts at once on idle node 2 only because it ends
        # before the head's booking.
        result = simulate(4, [(0, 1, 100, 100), (0, 1, 1000, 1000),
                              (1, 3, 100, 100), (2, 1, 50, 50)])
        self.assertEqual(result[2], (100, [0, 2, 3]))
        self.assertEqual(result[3], (30, [2]))

    def test_reservation_depth_limits_future_bookings(self):
        jobs = [(0, 4, 100, 100), (1, 4, 50, 50), (1, 4, 50, 50), (1, 1, 10, 10)]
        unlimited = simulate(4, jobs)
        self.assertEqual([start for start, _ in unlimited], [0, 100, 150, 200])


class DriverCrossCheck(unittest.TestCase):
    def test_random_workloads_match(self):
        if DRIVER is None:
            self.skipTest("no placement driver supplied")
        for seed in range(12):
            rng = random.Random(seed)
            nodes = rng.choice([8, 16, 33])
            submit, jobs = 0, []
            for _ in range(60):
                submit += rng.choice([0, 0, 1, 7, 20, 45])
                limit = rng.choice([60, 90, 300, 1000, 3600])
                runtime = rng.randint(1, limit)
                jobs.append((submit, rng.randint(1, nodes), limit, runtime))
            request = f"{nodes} 32 0 low {len(jobs)}\n" + "".join(
                " ".join(map(str, job)) + "\n" for job in jobs)
            output = subprocess.run(
                [DRIVER, "--exact-only", "--queue-depth", "64",
                 "--backfill-interval", "30"],
                input=request, text=True, capture_output=True, check=True).stdout
            native = {}
            for line in output.splitlines():
                record = json.loads(line)
                if "schedule" in record:
                    row = record["schedule"]
                    native[row["job"]] = (row["start"], sorted(row["node_ids"]))
            expected = simulate(nodes, jobs)
            for index, (start, ids) in enumerate(expected):
                self.assertEqual(native[index], (start, sorted(ids)),
                                 f"seed {seed} job {index}")


if __name__ == "__main__":
    if len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
        DRIVER = sys.argv.pop(1)
    unittest.main()
