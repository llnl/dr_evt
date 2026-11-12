"""Check that placement audits reject errors hidden by aggregate counts."""
import json
from pathlib import Path
import tempfile
import unittest

from fiction_runner import run_case
from placement_oracle import audit_case


def valid_case():
    return {
        'name': 'release_and_reserve', 'total_nodes': 2, 'match_policy': 'lonodex',
        'policy': 'hybrid', 'reservation_depth': 2, 'queue_depth': 100,
        'jobs': [[0, 1, 10, 10], [0, 1, 10, 10], [1, 1, 5, 5]],
        'schedule': [
            {'job': 0, 'start': 0, 'finish': 10, 'node_ids': [0]},
            {'job': 1, 'start': 0, 'finish': 10, 'node_ids': [1]},
            {'job': 2, 'start': 10, 'finish': 15, 'node_ids': [0]}],
        'snapshots': [
            {'time': 0, 'allocations': [{'job': 0, 'node_ids': [0]},
                                      {'job': 1, 'node_ids': [1]}], 'reservations': []},
            {'time': 1, 'allocations': [{'job': 0, 'node_ids': [0]},
                                      {'job': 1, 'node_ids': [1]}],
             'reservations': [{'job': 2, 'start': 10, 'finish': 15, 'node_ids': [0]}]},
            {'time': 10, 'allocations': [{'job': 2, 'node_ids': [0]}], 'reservations': []},
            {'time': 15, 'allocations': [], 'reservations': []}],
    }


class PlacementOracleTests(unittest.TestCase):
    def test_release_at_reservation_boundary(self):
        audit_case(valid_case())

    def test_duplicate_identity_below_capacity(self):
        case = valid_case()
        case['schedule'][1]['node_ids'] = [0]
        with self.assertRaisesRegex(ValueError, 'overlapping jobs'):
            audit_case(case)

    def test_wrong_identity_released_with_same_count(self):
        case = valid_case()
        case['snapshots'][2]['allocations'][0]['node_ids'] = [1]
        with self.assertRaisesRegex(ValueError, 'exact job ownership/release'):
            audit_case(case)

    def test_stale_allocation_after_completion(self):
        case = valid_case()
        case['snapshots'][-1]['allocations'] = [{'job': 2, 'node_ids': [0]}]
        with self.assertRaisesRegex(ValueError, 'exact job ownership/release'):
            audit_case(case)

    def test_reservation_overlaps_live_planned_interval(self):
        case = valid_case()
        case['snapshots'][1]['reservations'][0].update(start=9, finish=14)
        with self.assertRaisesRegex(ValueError, 'live allocation'):
            audit_case(case)

    def test_reservation_overlap(self):
        case = valid_case()
        case['jobs'].append([1, 1, 5, 5])
        case['schedule'].append({'job': 3, 'start': 15, 'finish': 20, 'node_ids': [0]})
        case['snapshots'][1]['reservations'].append(
            {'job': 3, 'start': 12, 'finish': 17, 'node_ids': [0]})
        case['snapshots'].insert(-1, {'time': 15, 'allocations': [
            {'job': 3, 'node_ids': [0]}], 'reservations': []})
        case['snapshots'][-1]['time'] = 20
        with self.assertRaisesRegex(ValueError, 'overlapping reservations'):
            audit_case(case)

    def test_limit_is_used_for_reservation(self):
        case = valid_case()
        case['jobs'][2][2] = 8
        case['snapshots'][1]['reservations'][0]['finish'] = 18
        audit_case(case)
        case['snapshots'][1]['reservations'][0]['finish'] = 15
        with self.assertRaisesRegex(ValueError, 'reserved interval'):
            audit_case(case)

    def test_reservation_owner_already_running(self):
        case = valid_case()
        case['snapshots'][1]['reservations'][0]['job'] = 0
        with self.assertRaisesRegex(ValueError, 'not pending'):
            audit_case(case)

    def test_missing_final_release_snapshot(self):
        case = valid_case()
        case['snapshots'].pop()
        with self.assertRaisesRegex(ValueError, 'all resources were released'):
            audit_case(case)

    def test_invalid_node_ids_and_intervals(self):
        mutations = [lambda c: c['schedule'][0].update(node_ids=[2]),
                     lambda c: c['schedule'][0].update(node_ids=[0, 0]),
                     lambda c: c['schedule'][0].update(start=-1, finish=9),
                     lambda c: c['schedule'][0].update(finish=float('nan')),
                     lambda c: c['jobs'][0].__setitem__(3, 11),
                     lambda c: c.update(reservation_depth=0),
                     lambda c: c.update(queue_depth=1)]
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                case = valid_case()
                mutation(case)
                with self.assertRaises(ValueError):
                    audit_case(case)

    def test_snapshots_require_fluxion(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, 'require Fluxion'):
                run_case(directory, [(0, 1, 10, 10)], 'fcfs',
                         scheduler_backend='sched-simple', observe_reservations=True)

    def test_checked_native_evidence(self):
        path = Path(__file__).parent / 'evidence' / 'placement-oracle.json'
        self.assertTrue(path.is_file(), 'missing checked native placement fixture')
        document = json.loads(path.read_text())
        self.assertEqual(document['schema_version'], 1)
        self.assertEqual(document['reference']['backend'], 'fluxion')
        self.assertEqual(len(document['cases']), 60)
        for case in document['cases']:
            with self.subTest(name=case['name'], matcher=case['match_policy'], policy=case['policy']):
                audit_case(case)
                if (case['name'] in ('identity_reservation', 'swapped_order')
                        and case['match_policy'] in ('lonodex', 'hinodex')):
                    starts = [job['start'] for job in case['schedule']]
                    expected = [0, 0, 80, 70 if case['name'] == 'swapped_order'
                                and case['policy'] != 'fcfs' else 80]
                    self.assertEqual(starts, expected)
                    expected_nodes = ([[0, 1], [2, 3], [0, 1, 2], [3]]
                                      if case['match_policy'] == 'lonodex' else
                                      [[2, 3], [0, 1], [1, 2, 3], [0]])
                    self.assertEqual([job['node_ids'] for job in case['schedule']], expected_nodes)
                if case['name'] == 'two_reservations' and case['policy'] == 'hybrid':
                    expected = 6 if case['reservation_depth'] == 1 else 240
                    self.assertEqual(case['schedule'][3]['start'], expected)
                if case['name'] == 'exact_release_boundary' and case['policy'] != 'fcfs':
                    self.assertEqual(case['schedule'][2]['finish'], case['schedule'][1]['start'])
                if case['name'] == 'early_completion' and case['policy'] != 'fcfs':
                    self.assertEqual(case['schedule'][1]['start'], 60)
                    self.assertTrue(any(reservation['job'] == 1 and reservation['start'] == 120
                                        for snapshot in case['snapshots']
                                        for reservation in snapshot['reservations']))


if __name__ == '__main__':
    unittest.main()
