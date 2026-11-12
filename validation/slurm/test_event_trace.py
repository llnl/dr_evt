"""Unit checks for exact-node parsing in the native Slurm event harness."""
import unittest

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


if __name__ == '__main__':
    unittest.main()
