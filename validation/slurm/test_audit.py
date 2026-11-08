"""Verify that the evidence audit rejects invalid resource observations."""
import copy
import json
from pathlib import Path
import tempfile
import unittest

from compare import audit, EPOCH


class AuditTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'slurm').mkdir()
        (self.root / 'dr_evt').mkdir()
        self.jobs = [(0, 1, 60, 10), (0, 1, 60, 10)]
        self.native = [{'start': 0, 'end': 10, 'nodes': 1, 'assignment': f'n{i:04d}'} for i in range(2)]
        self.observed = copy.deepcopy(self.native)
        self.snapshots = [
            {'time': EPOCH, 'allocated': [0, 1], 'jobs': [{'id': 1, 'nodes': [0]}, {'id': 2, 'nodes': [1]}]},
            {'time': EPOCH + 10, 'allocated': [], 'jobs': []},
        ]
        (self.root / 'dr_evt/resources.csv').write_text('time,allocated_nodes,free_nodes\n0,2,0\n10,0,2\n')

    def check(self):
        (self.root / 'slurm/snapshots.jsonl').write_text(''.join(json.dumps(row) + '\n' for row in self.snapshots))
        return audit(self.root, self.jobs, self.native, self.observed, 2)

    def test_valid(self):
        self.assertEqual(self.check()['status'], 'passed')

    def test_duplicate_node_owner(self):
        self.native[1]['assignment'] = 'n0000'
        self.snapshots[0]['jobs'][1]['nodes'] = [0]
        with self.assertRaisesRegex(AssertionError, 'Duplicate node owner'):
            self.check()

    def test_stale_native_allocation(self):
        self.snapshots[1]['allocated'] = [0]
        with self.assertRaisesRegex(AssertionError, 'Native allocation mismatch'):
            self.check()

    def test_incorrect_dr_evt_curve(self):
        (self.root / 'dr_evt/resources.csv').write_text('time,allocated_nodes,free_nodes\n0,1,1\n10,0,2\n')
        with self.assertRaisesRegex(AssertionError, 'DR_EVT resource curve mismatch'):
            self.check()

    def test_changed_duration(self):
        self.native[0]['end'] = 11
        with self.assertRaises(AssertionError):
            self.check()


if __name__ == '__main__':
    unittest.main()
