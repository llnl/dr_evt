"""Checks for the workload conversion and independently reconstructed audits."""
import tempfile
from pathlib import Path
import unittest

from compare import audit, lassen
from diagnose import feasible


class ValidationAuditTests(unittest.TestCase):
    def test_forecast_fit_distinguishes_adjacent_releases(self):
        running = [[0, 10, 1], [0, 11, 1]]
        self.assertFalse(feasible(running + [[10, 12, 2]], 0, 1, 11, 3))
        self.assertTrue(feasible(running + [[11, 13, 2]], 0, 1, 11, 3))
        self.assertTrue(feasible(running + [[10, 12, 2]], 0, 1, 10, 3))

    def test_rejects_overlapping_capacity_even_with_correct_job_durations(self):
        jobs = [(0, 3, 10, 10), (0, 2, 10, 10)]
        with self.assertRaises(AssertionError):
            audit(jobs, [[0, 10], [0, 10]], 4)
        self.assertEqual(audit(jobs, [[0, 10], [10, 20]], 4),
                         [[0, 3], [10, 2], [20, 0]])

    def test_rejects_start_before_arrival(self):
        with self.assertRaises(AssertionError):
            audit([(5, 1, 10, 10)], [[0, 10]], 4)

    def test_rejects_incorrect_execution_duration(self):
        with self.assertRaises(AssertionError):
            audit([(0, 1, 10, 10)], [[0, 9]], 4)

    def test_lassen_quantization_preserves_stable_arrival_order(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.csv'
            path.write_text('job_submit_time,begin_time,end_time,num_nodes,time_limit\n'
                            '100.2,101,102.1,2,5.1\n'
                            '100,101,101.2,3,1\n'
                            '100.2,101,104.1,4,2\n')
            self.assertEqual(lassen(path, 0), [(0, 3, 1, 1), (1, 2, 6, 2), (1, 4, 4, 4)])
            self.assertEqual(lassen(path, 2), [(0, 3, 1, 1), (1, 2, 6, 2)])


if __name__ == '__main__':
    unittest.main()
