"""Unit checks for workload and node parsing in the native Slurm harness."""
import tempfile
from pathlib import Path
import unittest

from compare import load_lassen
from licenses.event_trace import expand_nodes


class EventTraceTests(unittest.TestCase):
    def test_expand_nodes_accepts_single_ranges_and_fragments(self):
        self.assertEqual(expand_nodes('n0007'), [7])
        self.assertEqual(expand_nodes('n[0001-0003,0007,0010-0011]'),
                         [1, 2, 3, 7, 10, 11])

    def test_expand_nodes_rejects_non_harness_names(self):
        for expression in ('node0001', 'n[0001-]', 'n[0002-0001]'):
            with self.subTest(expression=expression):
                with self.assertRaises((ValueError, TypeError)):
                    expand_nodes(expression)

    def test_absolute_fixture_schema_is_used_without_rebasing(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'jobs.csv'
            path.write_text('job_id,submit_time,nodes,time_limit,actual_runtime\n'
                            '1,1597836579,3,60,11\n'
                            '2,1597836580,2,120,12\n')
            jobs, provenance = load_lassen(path, 0, 4)
            self.assertEqual(jobs, [(1597836579, 3, 60, 11),
                                    (1597836580, 2, 120, 12)])
            self.assertEqual(provenance['source_time_origin'], '0')
            self.assertEqual(provenance['rounded_durations'], 0)


if __name__ == '__main__':
    unittest.main()
