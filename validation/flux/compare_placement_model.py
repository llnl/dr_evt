"""Compare the C++ bitmap model's decisions with captured native Fluxion cases."""
import argparse
import json
from pathlib import Path
import subprocess

from placement_oracle import audit_case, require


def compare(driver, fixture):
    document = json.loads(Path(fixture).read_text())
    require(document['schema_version'] == 1 and document['reference']['backend'] == 'fluxion',
            'unsupported fixture backend or schema')
    require(bool(document['cases']), 'empty fixture')
    assumptions = document['assumptions']
    require(assumptions == {
        'whole_node_exclusive': True, 'cores_per_node': 1, 'gpus_per_node': 0,
        'equal_priorities': True, 'initially_empty': True,
        'release_delay': 0, 'system_latency': False,
        'batch_job_starts': True, 'intervals': 'half-open'},
        'fixture conditions do not match the validation driver')
    for case in document['cases']:
        audit_case(case)
        require(case['match_policy'] in ('lonodex', 'hinodex'), 'unsupported matcher')
        slots = {'fcfs': 0, 'easy': 1, 'hybrid': case['reservation_depth'],
                 'conservative': len(case['jobs'])}[case['policy']]
        order = 'low' if case['match_policy'] == 'lonodex' else 'high'
        header = f"{case['total_nodes']} {slots} {int(case['policy'] == 'fcfs')} {order} {len(case['jobs'])}"
        request = header + '\n' + '\n'.join(' '.join(map(str, job)) for job in case['jobs']) + '\n'
        for mode, arguments in (('coarse', []), ('exact-only', ['--exact-only'])):
            result = subprocess.run([str(driver), *arguments], input=request, text=True, capture_output=True, check=True)
            records = [json.loads(line) for line in result.stdout.splitlines()]
            actual = {**case,
                      'schedule': sorted((r['schedule'] for r in records if 'schedule' in r),
                                         key=lambda job: job['job']),
                      'snapshots': [r['snapshot'] for r in records if 'snapshot' in r]}
            audit_case(actual)
            label = f"{mode}/{case['match_policy']}/{case['policy']}/R{slots}/{case['name']}"
            require(actual['schedule'] == case['schedule'],
                    f"{label}: schedule mismatch\nexpected={case['schedule']}\nactual={actual['schedule']}")
            require(actual['snapshots'] == case['snapshots'],
                    f"{label}: reservation snapshot mismatch\nexpected={case['snapshots']}\nactual={actual['snapshots']}")
    print(f"Matched allocations, execution times, and reservations for {len(document['cases'])} native Fluxion cases with both filter specializations")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('driver', type=Path)
    parser.add_argument('--fixture', type=Path,
                        default=Path(__file__).parent / 'evidence/placement-oracle.json')
    args = parser.parse_args()
    compare(args.driver.resolve(), args.fixture)


if __name__ == '__main__':
    main()
