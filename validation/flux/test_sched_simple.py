"""Compare native FIFO scheduling and Fiction synchronization with dr_evt."""
import argparse
import json
import os
from pathlib import Path

from fiction_runner import run_case
from resource_profiles import native_profile
from test_paired import expected_starts, profile, reference_schedule, simulator_case, workloads


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('simulator', type=Path)
    parser.add_argument('--outdir', type=Path, default=Path('/results/sched-simple-small'))
    args = parser.parse_args()
    args.outdir.mkdir(parents=True, exist_ok=True)
    os.environ['DR_EVT_OBSERVE_RESOURCES'] = '1'
    results = []
    for depth in (1, 2, 64):
        for name, jobs in workloads().items():
            origin = min(job[0] for job in jobs)
            jobs = [(arrival - origin, nodes, limit, duration)
                    for arrival, nodes, limit, duration in jobs]
            case = args.outdir / f'q{depth}_{name}'
            case.mkdir(exist_ok=True)
            observed = simulator_case(case, jobs, args.simulator, 'none', depth, 0)
            transitions = run_case(case, jobs, 'fcfs', queue_depth=depth, timeout=45,
                                   scheduler_backend='sched-simple')
            reference = reference_schedule(transitions, jobs, time_precision=3)
            assert observed == reference, (depth, name, observed, reference)
            expected = expected_starts(name, 'none', 0)
            if expected is not None:
                assert [job['start'] for job in reference] == expected
            run = Path((case / 'run-directory.txt').read_text().strip())
            curve, count = native_profile(run / 'output/resource-manager-snapshots.jsonl',
                                          jobs, time_precision=3)
            assert curve == profile(reference)
            results.append({'case': name, 'queue_depth': depth, 'jobs': len(jobs),
                            'resource_snapshots': count, 'match': True})
            (args.outdir / 'report.json').write_text(json.dumps({
                'per_pass_accounting': True, 'fiction_scheduler': 'sched-simple',
                'cases': results}, indent=2) + '\n')
            print(f'Matched Q={depth}: {name}', flush=True)
    print(f'{len(results)} sched-simple comparisons passed', flush=True)


if __name__ == '__main__':
    main()
