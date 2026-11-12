"""Capture and audit whole-node placement fixtures from native Fluxion."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path

from fiction_runner import CAPACITY, EPOCH, run_case
from resource_profiles import ranks


MATCHERS = ('lonodex', 'hinodex', 'first')
POLICIES = ('fcfs', 'easy', 'hybrid', 'conservative')
QUEUE_DEPTH = 2048


def require(condition, message):
    if not condition:
        raise ValueError(message)


def node_ids(values, quantity, capacity):
    require(isinstance(values, list) and all(type(node) is int for node in values),
            'node IDs must be an integer list')
    require(values == sorted(set(values)) and len(values) == quantity,
            'allocation has duplicate, unordered, or missing node IDs')
    require(all(0 <= node < capacity for node in values), 'node ID outside resource pool')
    return set(values)


def overlaps(left, right):
    return (left['start'] < right['finish'] and right['start'] < left['finish']
            and bool(set(left['node_ids']) & set(right['node_ids'])))


def audit_case(case):
    """Check identities and intervals without constructing a capacity schedule."""
    jobs, schedule, snapshots = case['jobs'], case['schedule'], case['snapshots']
    capacity = case['total_nodes']
    require(type(capacity) is int and capacity > 0, 'invalid resource pool size')
    require(case['match_policy'] in MATCHERS and case['policy'] in POLICIES,
            'unknown matching or queue policy')
    require(type(case['queue_depth']) is int and case['queue_depth'] >= len(jobs),
            'oracle cases must not reach the queue examination cap')
    depth = case['reservation_depth']
    require((case['policy'] == 'hybrid' and type(depth) is int and depth > 0)
            or (case['policy'] != 'hybrid' and depth is None), 'invalid reservation depth')
    for index, job in enumerate(jobs):
        require(len(job) == 4 and all(math.isfinite(value) for value in job), 'invalid job')
        submit, quantity, limit, runtime = job
        require(submit >= 0 and type(quantity) is int and 0 < quantity <= capacity
                and 0 < runtime <= limit, 'invalid request, estimate, or runtime')
        require(index == 0 or jobs[index - 1][0] <= submit, 'jobs must be submission ordered')
    require([job['job'] for job in schedule] == list(range(len(jobs))),
            'missing, duplicate, or reordered schedule jobs')
    for entry, (submit, quantity, limit, runtime) in zip(schedule, jobs):
        require(all(math.isfinite(entry[key]) for key in ('start', 'finish')),
                'nonfinite execution interval')
        require(entry['start'] >= submit and math.isclose(
            entry['finish'] - entry['start'], runtime, rel_tol=0, abs_tol=1e-6),
            'execution precedes submission or has wrong runtime')
        node_ids(entry['node_ids'], quantity, capacity)
    for index, left in enumerate(schedule):
        for right in schedule[index + 1:]:
            require(not overlaps(left, right), 'node allocated to overlapping jobs')
    require(bool(snapshots), 'missing native snapshots')
    previous_time = -1
    for snapshot in snapshots:
        time = snapshot['time']
        require(math.isfinite(time) and time >= previous_time, 'invalid snapshot time order')
        previous_time = time
        running = {job['job']: job['node_ids'] for job in schedule
                   if job['start'] <= time < job['finish']}
        observed = {}
        for allocation in snapshot['allocations']:
            index = allocation['job']
            require(type(index) is int and 0 <= index < len(jobs) and index not in observed,
                    'unknown or duplicate snapshot owner')
            node_ids(allocation['node_ids'], jobs[index][1], capacity)
            observed[index] = allocation['node_ids']
        require(observed == running, 'native snapshot disagrees with exact job ownership/release')
        reservations = snapshot['reservations']
        require(len({entry['job'] for entry in reservations}) == len(reservations),
                'duplicate reserved job')
        for reservation in reservations:
            index = reservation['job']
            require(type(index) is int and 0 <= index < len(jobs), 'unknown reserved job')
            submit, quantity, limit, runtime = jobs[index]
            require(submit <= time < schedule[index]['start'], 'reservation owner is not pending')
            require(all(math.isfinite(reservation[key]) for key in ('start', 'finish'))
                    and reservation['start'] >= time
                    and reservation['finish'] - reservation['start'] == limit,
                    'invalid reserved interval')
            node_ids(reservation['node_ids'], quantity, capacity)
            for owner in running:
                planned = {**schedule[owner], 'finish': schedule[owner]['start'] + jobs[owner][2]}
                require(not overlaps(reservation, planned), 'reservation overlaps a live allocation')
        for index, left in enumerate(reservations):
            for right in reservations[index + 1:]:
                require(not overlaps(left, right), 'overlapping reservations on the same nodes')
        maximum = {'fcfs': 0, 'easy': 1, 'hybrid': depth, 'conservative': len(jobs)}[case['policy']]
        require(len(reservations) <= maximum, 'reservation depth exceeded')
    require(snapshots[-1]['time'] >= max((job['finish'] for job in schedule), default=0)
            and not snapshots[-1]['allocations'] and not snapshots[-1]['reservations'],
            'final snapshot does not prove all resources were released')


def capture_case(directory, name, jobs, policy, matcher, depth=None):
    """Run Fluxion and retain normalized decisions plus hashes of raw evidence."""
    directory = Path(directory).resolve()
    transitions = run_case(directory, jobs, policy, QUEUE_DEPTH, depth,
                           match_policy=matcher, observe_reservations=True)
    run = Path((directory / 'run-directory.txt').read_text().strip())
    raw = run / 'output/resource-manager-snapshots.jsonl'
    case = {'name': name, 'total_nodes': CAPACITY, 'match_policy': matcher,
            'policy': policy, 'reservation_depth': depth, 'queue_depth': QUEUE_DEPTH,
            'jobs': [list(job) for job in jobs], 'schedule': [], 'snapshots': []}
    for index, (row, job) in enumerate(zip(transitions, jobs)):
        require(float(row['SUBMIT']) - EPOCH == job[0] and int(row['nnodes']) == job[1],
                'native transition input metadata changed')
        case['schedule'].append({'job': index, 'start': float(row['START']) - EPOCH,
                                 'finish': float(row['FINISH']) - EPOCH,
                                 'node_ids': sorted(int(node) for node in row['NODELIST'].split(','))})
    for line in raw.read_text().splitlines():
        record = json.loads(line)
        require(ranks(record['all']) == set(range(CAPACITY)), 'native pool changed')
        allocations = [{'job': job['job'], 'node_ids': sorted(ranks(job['allocation']))}
                       for job in record['jobs']]
        require(set().union(*(set(job['node_ids']) for job in allocations))
                == ranks(record['allocated']), 'native allocation union mismatch')
        reservations = []
        for entry in record['reservations']:
            require(entry['info']['status'] == 'RESERVED', 'native reservation status changed')
            start = entry['info']['at'] - EPOCH
            reservations.append({'job': entry['job'], 'start': start,
                                 'finish': start + jobs[entry['job']][2],
                                 'node_ids': sorted(ranks(entry['allocation']))})
        require(set().union(*(set(job['node_ids']) for job in reservations))
                == ranks(record['reserved']), 'native reservation union mismatch')
        case['snapshots'].append({'time': record['time'] - EPOCH,
                                  'allocations': sorted(allocations, key=lambda job: job['job']),
                                  'reservations': sorted(reservations, key=lambda job: job['job'])})
    audit_case(case)
    case['raw_sha256'] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in (raw, run / 'output/job_transitions.csv',
                                       directory / 'scheduler.json', directory / 'config.toml')}
    return case


def workloads():
    minimal = [(0, 2, 70, 70), (0, 2, 80, 80), (6, 3, 10, 10), (6, 1, 50, 50)]
    return {
        'identity_reservation': minimal,
        'swapped_order': [minimal[1], minimal[0], *minimal[2:]],
        'backfill_before_reservation': [(0, 3, 120, 120), (6, 4, 20, 20), (6, 1, 100, 100)],
        'exact_release_boundary': [(0, 3, 120, 120), (6, 4, 20, 20), (6, 1, 114, 114)],
        'two_reservations': [(0, 3, 120, 120), (6, 2, 20, 20), (6, 4, 100, 100), (6, 1, 200, 200)],
        'early_completion': [(0, 3, 120, 60), (6, 4, 20, 20), (6, 1, 80, 10)],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--outdir', type=Path, default=Path('/results/placement-oracle'))
    parser.add_argument('--verify', type=Path, help='Audit a captured fixture without running Flux')
    parser.add_argument('--native-manifest', type=Path, default=Path('/workspace/sources.json'))
    parser.add_argument('--match-policy', choices=MATCHERS, action='append')
    parser.add_argument('--policy', choices=POLICIES, action='append')
    parser.add_argument('--case', choices=tuple(workloads()), action='append')
    args = parser.parse_args()
    if args.verify:
        document = json.loads(args.verify.read_text())
        require(document['schema_version'] == 1 and document['reference']['backend'] == 'fluxion',
                'unsupported oracle format or backend')
        require(bool(document['cases']), 'empty oracle fixture')
        for case in document['cases']:
            audit_case(case)
        print(f"Audited {len(document['cases'])} Fluxion placement cases")
        return
    sources = json.loads(args.native_manifest.read_text())
    here = Path(__file__).parent
    binaries = [Path('/workspace/install/lib/flux/modules') / name for name in
                ('sched-fluxion-resource.so', 'sched-fluxion-qmanager.so')]
    binaries.append(Path(os.environ['FLUX_FICTION_JOBTAP_SO']))
    document = {
        'schema_version': 1,
        'reference': {'backend': 'fluxion', 'native_sources': sources,
                      'binary_sha256': {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                                        for path in binaries},
                      'script_sha256': {name: hashlib.sha256((here / name).read_bytes()).hexdigest()
                                        for name in ('placement_oracle.py', 'fiction_runner.py',
                                                     'resource_observer/sitecustomize.py')}},
        'assumptions': {'whole_node_exclusive': True, 'cores_per_node': 1, 'gpus_per_node': 0,
                        'equal_priorities': True, 'initially_empty': True,
                        'release_delay': 0, 'system_latency': False,
                        'batch_job_starts': True, 'intervals': 'half-open'},
        'cases': [],
    }
    for matcher in args.match_policy or ('lonodex', 'hinodex'):
        for policy in args.policy or POLICIES:
            for depth in ((1, 2) if policy == 'hybrid' else (None,)):
                for name in args.case or tuple(workloads()):
                    label = f'{matcher}-{policy}-r{depth or 0}-{name}'
                    case = capture_case(args.outdir / label, name, workloads()[name], policy, matcher, depth)
                    document['cases'].append(case)
                    print(f"{label}: starts={[job['start'] for job in case['schedule']]}, "
                          f"reservations={sum(len(s['reservations']) for s in case['snapshots'])}",
                          flush=True)
    args.outdir.mkdir(parents=True, exist_ok=True)
    (args.outdir / 'oracle.json').write_text(json.dumps(document, indent=2) + '\n')
    print(f"Captured {len(document['cases'])} placement cases in {args.outdir / 'oracle.json'}")


if __name__ == '__main__':
    main()
