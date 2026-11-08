"""Ensure manager audits detect conflicts that aggregate area can miss."""
import json
from pathlib import Path
import tempfile
import unittest

from resource_profiles import area, native_profile


def resource(rank):
    return {'R': {'execution': {'R_lite': [{'rank': rank}]}}} if rank else {'R': None}


def snapshot(time, allocated, assignments):
    return {'time': 1700000000 + time, 'all': resource('0-3'),
            'allocated': resource(allocated),
            'jobs': [{'job': index, 'allocation': resource(nodes)}
                     for index, nodes in enumerate(assignments)]}


class ResourceProfileTests(unittest.TestCase):
    def audit(self, records):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'snapshots.jsonl'
            path.write_text(''.join(json.dumps(row) + '\n' for row in records))
            return native_profile(path, [(0, 1, 10, 10), (0, 1, 10, 10)])

    def test_step_area(self):
        self.assertEqual(area([[0, 1], [3, 2], [7, 0]]), 11)

    def test_distinct_nodes_and_release(self):
        curve, count = self.audit([snapshot(0, '0-1', ['0', '1']),
                                   snapshot(10, '', [])])
        self.assertEqual(area(curve), 20)
        self.assertEqual(count, 2)

    def test_duplicate_owner_below_capacity(self):
        with self.assertRaisesRegex(AssertionError, 'node claimed twice'):
            self.audit([snapshot(0, '0', ['0', '0'])])

    def test_allocation_left_after_release(self):
        with self.assertRaisesRegex(AssertionError, 'ownership/union mismatch'):
            self.audit([snapshot(0, '0', ['0']), snapshot(10, '0', [])])


if __name__ == '__main__':
    unittest.main()
