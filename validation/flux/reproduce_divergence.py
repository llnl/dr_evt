"""Compare diverging and matching placements on a system with 4 nodes total.

Both four-job cases use high-ID-first matching, with optional deletion controls.
"""
import argparse
import hashlib
import json
from pathlib import Path

from fiction_runner import run_case
from test_paired import reference_schedule, simulator_case


JOBS = [(0, 2, 70, 70), (0, 2, 80, 80), (6, 3, 10, 10), (6, 1, 50, 50)]
MATCH_POLICY = 'hinodex'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('simulator', type=Path)
    parser.add_argument('--outdir', type=Path, default=Path('/results/minimal-divergence'))
    parser.add_argument('--check-deletions', action='store_true',
                        help='Also check every three-job trace made by deleting one job')
    parser.add_argument('--match-policy', choices=('lonodex', 'hinodex'), default=MATCH_POLICY,
                        help='Low- or high-ID-first native resource matching')
    args = parser.parse_args()
    args.outdir.mkdir(parents=True, exist_ok=True)
    report = {'capacity': 4, 'cores_per_node': 1, 'cases': [], 'queue_depth': 2048,
              'per_pass_accounting': True,
              'match_policy': args.match_policy,
              'simulator_sha256': hashlib.sha256(args.simulator.read_bytes()).hexdigest(),
              'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    # Equal-time input order fixes the first job's two nodes. Swapping A and B
    # changes which nodes are released at 70, without changing either job.
    traces = [('diverges', JOBS, ['A', 'B', 'C', 'D']),
              ('matches', [JOBS[1], JOBS[0], *JOBS[2:]], ['B', 'A', 'C', 'D'])]
    if args.check_deletions:
        traces += [(f'delete_{index}', JOBS[:index] + JOBS[index + 1:],
                    [label for i, label in enumerate(['A', 'B', 'C', 'D']) if i != index])
                   for index in range(4)]
    for name, jobs, labels in traces:
        for policy in ('easy', 'hybrid', 'conservative'):
            case = args.outdir / f'{name}_{policy}'
            case.mkdir(exist_ok=True)
            reservation = 32 if policy == 'hybrid' else None
            transitions = run_case(case, jobs, policy, 2048, reservation,
                                   match_policy=args.match_policy)
            native = reference_schedule(transitions, jobs)
            observed = simulator_case(case, jobs, args.simulator, policy, 2048, reservation)
            different = native != observed
            allocated_nodes = [sorted(map(int, row['NODELIST'].split(',')))
                               for row in transitions]
            if name in ('diverges', 'matches'):
                candidate_start = 80 if name == 'diverges' else 70
                assert [job['start'] for job in native] == [0, 0, 80, candidate_start], native
                assert [job['start'] for job in observed] == [0, 0, 80, 70], observed
                expected_nodes = ([[2, 3], [0, 1], [1, 2, 3], [0]] if
                                  args.match_policy == 'hinodex' else
                                  [[0, 1], [2, 3], [0, 1, 2], [3]])
                assert allocated_nodes == expected_nodes, allocated_nodes
                assert different == (name == 'diverges'), (name, native, observed)
            else:
                assert not different, ('deletion still diverges', name, policy, native, observed)
            report['cases'].append({'name': name, 'policy': policy, 'jobs': jobs,
                                    'labels': labels, 'reservation_depth': reservation,
                                    'fiction_allocated_nodes': allocated_nodes,
                                    'reference': native, 'dr_evt': observed,
                                    'different': different})
            (args.outdir / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
            print(name, policy, 'Fiction starts:', [job['start'] for job in native],
                  'dr_evt starts:', [job['start'] for job in observed],
                  'diverges' if different else 'matches', flush=True)
    (args.outdir / 'reproducer-script.py').write_bytes(Path(__file__).read_bytes())


if __name__ == '__main__':
    main()
