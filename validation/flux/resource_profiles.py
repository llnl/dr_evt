"""Audit resource-manager occupancy and node ownership on paired policy runs."""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path

from fiction_runner import CAPACITY, EPOCH, run_case
from test_paired import observed_profile, profile, reference_schedule, simulator_case


def ranks(response):
    """Decode allocated rank IDs from a native resource.find response."""
    resource = response['R']
    if resource is None:
        return set()
    nodes = set()
    for entry in resource['execution']['R_lite']:
        for part in entry['rank'].split(','):
            ends = [int(value) for value in part.split('-')]
            nodes.update(range(ends[0], ends[-1] + 1))
    return nodes


def native_profile(path, jobs, capacity=CAPACITY, time_precision=None):
    """Check native allocation maps, including each running job's node IDs."""
    samples = []
    by_time = {}
    snapshots = [json.loads(line) for line in path.read_text().splitlines()]
    for record in snapshots:
        time = record['time'] - EPOCH
        all_nodes = ranks(record['all'])
        allocated = ranks(record['allocated'])
        assert all_nodes == set(range(capacity)), ('native capacity changed', record)
        owners = {}
        for job in record['jobs']:
            assigned = ranks(job['allocation'])
            assert len(assigned) == jobs[job['job']][1], ('allocation size', record, job)
            for node in assigned:
                assert node not in owners, ('node claimed twice', time, node, owners, job)
                owners[node] = job['job']
        assert set(owners) == allocated, ('native ownership/union mismatch', record)
        assert allocated <= all_nodes, ('native capacity exceeded', record)
        if time_precision is not None:
            time = round(time, time_precision)
        by_time[time] = len(allocated)
    for time, allocated in sorted(by_time.items()):
        if not samples or samples[-1][1] != allocated:
            samples.append([time, allocated])
    assert samples and samples[0][0] == 0 and samples[-1][1] == 0
    return samples, len(snapshots)


def area(samples):
    """Integrate a step curve exactly, using rectangles rather than trapezoids."""
    return sum((end[0] - start[0]) * start[1]
               for start, end in zip(samples, samples[1:]))


def export_curves(case, observed, reference, small, capacity=CAPACITY):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt

    for name, samples in [('dr_evt', observed), ('fiction', reference)]:
        with (case / f'{name}-resource-profile.csv').open('w', newline='') as stream:
            writer = csv.writer(stream)
            writer.writerow(['time', 'allocated_nodes'])
            writer.writerows(samples)
    end = max(observed[-1][0], reference[-1][0])
    figure, axis = plt.subplots(figsize=(9, 3.7))
    for label, samples in [('dr_evt', observed), ('fiction', reference)]:
        points = samples + [[end, 0]]
        axis.step(*zip(*points), where='post', label=f'{label}: {area(samples):g} node-s')
    axis.axhline(capacity, color='gray', linestyle=':', label=f'Capacity: {capacity} nodes')
    axis.set(xlabel='Simulation time (s)', ylabel='Allocated nodes', ylim=(-0.1, capacity * 1.1),
             title=case.name)
    if small:
        axis.set_xticks([0, 6, 70, 80, 90, 120, 130])
    axis.legend(loc='upper right')
    figure.tight_layout()
    figure.savefig(case / 'resource-profile.svg')
    figure.savefig(case / 'resource-profile.png', dpi=150)
    plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('simulator', type=Path)
    parser.add_argument('--paired-report', type=Path,
                        help='Also replay cases from an existing paired report')
    parser.add_argument('--paired-only', action='store_true',
                        help='Capture only the supplied paired cases')
    parser.add_argument('--outdir', type=Path, default=Path('/results/resource-manager-profiles'))
    parser.add_argument('--reuse-completed', action='store_true',
                        help='Audit existing completed captures before running missing cases')
    args = parser.parse_args()
    if args.paired_only and not args.paired_report:
        parser.error('--paired-only requires --paired-report')
    root = args.outdir
    root.mkdir(parents=True, exist_ok=True)
    observer = Path(__file__).with_name('resource_observer') / 'sitecustomize.py'
    os.environ['DR_EVT_OBSERVE_RESOURCES'] = '1'
    os.environ['PYTHONPATH'] = str(observer.parent) + ':' + os.environ.get('PYTHONPATH', '')
    jobs = [(0, 1, 80, 80), (0, 1, 70, 70), (0, 1, 70, 70),
            (0, 1, 80, 80), (6, 3, 10, 10), (6, 1, 50, 50)]
    configurations = [] if args.paired_only else [
        ('six_job_hybrid', jobs, 'hybrid', 32, 2048, 'first', None)]
    if args.paired_report:
        paired = json.loads(args.paired_report.read_text())
        if not paired['assumptions'].get('per_pass_accounting'):
            parser.error('Paired input must have per_pass_accounting enabled')
        for entry in paired['cases']:
            policy = entry['backfill_policy']
            reservation = entry['reservation_depth']
            label = f'{policy}_r{reservation}_q{entry["reference_queue_depth"]}_{entry["case"]}'
            configurations.append((label, entry['jobs'], policy, reservation,
                                   entry['reference_queue_depth'], 'lonodex', entry))
    report = {'capacity': CAPACITY, 'per_pass_accounting': True, 'cases': [],
              'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'observer_sha256': hashlib.sha256(observer.read_bytes()).hexdigest()}
    for label, jobs, policy, reservations, depth, matcher, prior in configurations:
        case = root / label
        case.mkdir(exist_ok=True)
        observed_jobs = simulator_case(case, jobs, args.simulator, policy,
                                       depth, reservations)
        marker = case / 'run-directory.txt'
        run = case / Path(marker.read_text().strip()).name if marker.exists() else None
        if (args.reuse_completed and run is not None
                and (run / 'output/job_transitions.csv').exists()
                and (run / 'output/resource-manager-snapshots.jsonl').exists()):
            with (run / 'output/job_transitions.csv').open() as stream:
                transitions = sorted(csv.DictReader(stream), key=lambda row: int(row['trace_idx']))
            assert len(transitions) == len(jobs), 'Incomplete cached run'
        else:
            transitions = run_case(
                case, jobs, 'fcfs' if policy == 'none' else policy, depth,
                reservations if policy == 'hybrid' else None, timeout=300, match_policy=matcher)
        reference_jobs = reference_schedule(transitions, jobs)
        run = case / Path((case / 'run-directory.txt').read_text().strip()).name
        snapshot_path = run / 'output/resource-manager-snapshots.jsonl'
        native, count = native_profile(snapshot_path, jobs)
        observed = observed_profile(case / 'dr_evt-resources.csv')
        # Validate each independent manager curve against job records, without
        # using those records to construct the curve being integrated.
        assert observed == profile(observed_jobs), 'dr_evt manager/job discrepancy'
        assert native == profile(reference_jobs), 'Fluxion manager/job discrepancy'
        replay_differences = []
        if prior:
            assert observed_jobs == prior['dr_evt_schedule'], 'dr_evt replay changed'
            replay_differences = [index for index, (old, new) in enumerate(
                zip(prior['reference_schedule'], reference_jobs)) if old != new]
        expected = sum(job[1] * job[3] for job in jobs)
        assert math.isclose(area(observed), expected, abs_tol=1e-6)
        assert math.isclose(area(native), expected, abs_tol=1e-6)
        export_curves(case, observed, native, prior is None)
        report['cases'].append({
            'case': label, 'jobs': len(jobs), 'backfill_policy': policy,
            'match_policy': matcher, 'reservation_depth': reservations,
            'queue_depth': depth, 'native_snapshots': count,
            'dr_evt_area_node_seconds': area(observed),
            'fiction_area_node_seconds': area(native), 'expected_node_seconds': expected,
            'native_duplicate_owners': 0, 'manager_job_profiles_agree': True,
            'dr_evt_max_allocated': max(value for _, value in observed),
            'fiction_max_allocated': max(value for _, value in native),
            'reference_replay_differing_jobs': replay_differences,
            'dr_evt_schedule': observed_jobs, 'reference_schedule': reference_jobs,
            'native_snapshot_file': str(snapshot_path.relative_to(root))})
        (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(f'{label}: area={area(native):g}/{area(observed):g}, '
              f'{count} native snapshots, no duplicate node owners, '
              f'{len(replay_differences)} reference replay differences', flush=True)
    (root / 'profile-script.py').write_bytes(Path(__file__).read_bytes())
    (root / 'observer-script.py').write_bytes(observer.read_bytes())


if __name__ == '__main__':
    main()
