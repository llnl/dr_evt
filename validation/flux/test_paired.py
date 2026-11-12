"""Compare event-driven per-pass accounting on identical homogeneous workloads."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import random
import subprocess
import time

from fiction_runner import CAPACITY, EPOCH, run_case


def expected_starts(name, policy, reservations):
    """Independent starts for the small cases that distinguish policy choices."""
    fixed = {'single': [0], 'simultaneous_capacity': [0, 0, 0, 0, 23],
             'limit_admission': [0, 60, 80]}
    if name in fixed:
        return fixed[name]
    if name == 'head_guard':
        return [0, 120, 140] if policy == 'none' else [0, 120, 6]
    if name == 'safe_overlap':
        return [0, 120, 120] if policy == 'none' else [0, 120, 6]
    if name == 'early_completion':
        return [0, 60, 80] if policy == 'none' else [0, 60, 6]
    if name == 'two_reservations':
        return ([0, 120, 206, 6] if policy == 'easy' or
                (policy == 'hybrid' and reservations == 1) else [0, 120, 140, 240])
    if name == 'all_reservations':
        return ([0, 120, 140, 240, 260] if policy in ('none', 'conservative')
                else [0, 120, 140, 306, 6])
    return None


def workloads():
    cases = {
        'single': [(0, 1, 20)],
        'simultaneous_capacity': [(0, 1, 20), (0, 1, 21), (0, 1, 22),
                                  (0, 1, 23), (0, 4, 10)],
        'head_guard': [(0, 3, 120), (6, 4, 20), (6, 1, 20)],
        'safe_overlap': [(0, 3, 120), (6, 2, 20), (6, 1, 200)],
        'two_reservations': [(0, 3, 120), (6, 2, 20), (6, 4, 100), (6, 1, 200)],
        'all_reservations': [(0, 3, 120), (6, 2, 20), (6, 3, 100),
                             (6, 4, 20), (6, 1, 300)],
        'early_completion': [(0, 3, 120, 60), (6, 4, 20, 20), (6, 1, 80, 10)],
        'limit_admission': [(0, 3, 120, 60), (6, 4, 20, 20), (6, 1, 130, 10)],
    }
    for seed in (17, 43):
        rng = random.Random(seed)
        cases[f'mixed_seed_{seed}'] = sorted(
            (rng.randrange(0, 20) * 6, rng.randrange(1, 5), rng.randrange(1, 9) * 10)
            for _ in range(12))
    return {name: [job if len(job) == 4 else (*job, job[2]) for job in jobs]
            for name, jobs in cases.items()}


def large_workloads():
    """Two reproducible 2,000-job traces with different peak queue demands.

    Bursts arrive together; the long gap lets each burst drain. The default
    examination cap covers the full trace to avoid depth-triggered restarts.
    Explicit smaller caps remain available for accounting diagnostics.
    Limits equal runtime so release-delay/estimation differences are excluded.
    """
    cases = {}
    for burst_size in (24, 48):
        rng = random.Random(2000 + burst_size)
        jobs = []
        for index in range(2000):
            arrival = (index // burst_size) * 4000
            nodes = rng.randrange(1, CAPACITY + 1)
            duration = rng.randrange(1, 9) * 10
            jobs.append((arrival, nodes, duration, duration))
        cases[f'burst_{burst_size}_jobs_2000'] = jobs
    return cases


def profile(schedule, capacity=CAPACITY):
    """Right-continuous aggregate occupancy after all events at a timestamp."""
    events = {0.0: 0}
    for job in schedule:
        events[job['start']] = events.get(job['start'], 0) + job['nodes']
        events[job['finish']] = events.get(job['finish'], 0) - job['nodes']
    allocated = 0
    samples = []
    for time, change in sorted(events.items()):
        allocated += change
        if not 0 <= allocated <= capacity:
            raise AssertionError(('capacity violation', time, allocated))
        if not samples or samples[-1][1] != allocated:
            samples.append([time, allocated])
    return samples


def observed_profile(path, capacity=CAPACITY):
    with path.open() as stream:
        rows = list(csv.DictReader(stream))
    by_time = {}
    for row in rows:
        allocated, free = int(row['allocated_nodes']), int(row['free_nodes'])
        if free + allocated != capacity:
            raise AssertionError(('resource pool changed', row))
        by_time[float(row['time'])] = allocated
    samples = []
    for time, allocated in sorted(by_time.items()):
        if not samples or samples[-1][1] != allocated:
            samples.append([time, allocated])
    return samples


def simulator_case(case, jobs, binary, policy, depth, reservations, total_nodes=CAPACITY,
                   resource_model='capacity', placement_optimizations='all',
                   placement_order='low', coarse_filter=False, timeout=30):
    trace, output, resources = (case / filename for filename in
                               ('dr_evt-input.csv', 'dr_evt-output.csv', 'dr_evt-resources.csv'))
    with trace.open('w', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(['job_submit_time', 'num_nodes', 'time_limit', 'actual_run_time'])
        writer.writerows(jobs)
    priority_option = 'fcfs_conservative' if policy == 'conservative' else 'fcfs'
    command = [str(binary), str(trace), '--total_nodes', str(total_nodes),
               '--priority_policy', priority_option, '--queue_impl', 'circular',
               '--per_pass_accounting',
               '--backfill_policy', policy, '--backfill_queue_depth', str(depth),
               '--backfill_interval', '0', '--dispatch_interval', '0',
               '--planning_time_resolution', '0', '--node_release_delay', '0',
               '--trace_format', 'simple', '--timestamp_format', 'epoch',
               '--run_time_mode', 'actual', '--msec_output', '--max_jobs', str(len(jobs)),
               '--outfile', str(output), '--resource_trace', str(resources)]
    if resource_model == 'placement':
        command.remove('--per_pass_accounting')
        command.extend(['--resource_model', 'placement', '--placement_optimizations',
                        placement_optimizations, '--placement_order', placement_order])
        if coarse_filter:
            command.append('--placement_coarse_filter')
    elif resource_model != 'capacity':
        raise ValueError('unknown resource model: ' + resource_model)
    if policy == 'hybrid':
        command.extend(['--reservation_depth', str(reservations)])
    (case / 'dr_evt-command.json').write_text(json.dumps(command, indent=2) + '\n')
    with (case / 'dr_evt.log').open('w') as log:
        started = time.perf_counter()
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
        (case / 'dr_evt-process-timing.json').write_text(json.dumps({
            'elapsed_seconds': time.perf_counter() - started,
            'scope': 'simulator process launch through exit',
        }, indent=2) + '\n')
    with output.open() as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != len(jobs):
        raise AssertionError(('missing simulator jobs', rows))
    result = []
    for index, (row, (arrival, nodes, limit, actual)) in enumerate(zip(rows, jobs)):
        # The simulator flushes contiguous input IDs in order. Confirm input
        # metadata before relying on implicit row IDs, including duplicate jobs.
        if (float(row['job_submit_time']), int(row['num_nodes']), float(row['time_limit'])) != (
                arrival, nodes, limit):
            raise AssertionError(('simulator row order/input changed', index, row))
        result.append({'job': index, 'submit': arrival, 'nodes': int(row['num_nodes']),
                       'start': float(row['begin_time']), 'finish': float(row['end_time'])})
    if profile(result, total_nodes) != observed_profile(resources, total_nodes):
        raise AssertionError(('simulator resource trace disagrees with its jobs',
                              profile(result, total_nodes), observed_profile(resources, total_nodes)))
    return result


def reference_schedule(transitions, jobs, total_nodes=CAPACITY, time_precision=None):
    result = []
    intervals = {node: [] for node in range(total_nodes)}
    for index, (row, (arrival, nodes, limit, actual)) in enumerate(zip(transitions, jobs)):
        start, finish, submitted = (float(row[key]) - EPOCH
                                    for key in ('START', 'FINISH', 'SUBMIT'))
        allocated = [int(node) for node in row['NODELIST'].split(',')]
        if (int(row['nnodes']) != nodes or len(set(allocated)) != nodes
                or any(node not in intervals for node in allocated)
                or submitted != arrival or not math.isclose(finish - start, actual, abs_tol=1e-6)):
            raise AssertionError(('reference allocation/runtime changed', index, row))
        for node in allocated:
            for previous_start, previous_finish in intervals[node]:
                if start < previous_finish and previous_start < finish:
                    raise AssertionError(('exclusive node allocated twice', node, row))
            intervals[node].append((start, finish))
        if time_precision is not None:
            start, finish, submitted = (round(t, time_precision) for t in (start, finish, submitted))
        result.append({'job': index, 'submit': submitted, 'nodes': len(allocated),
                       'start': start, 'finish': finish})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('simulator', type=Path)
    parser.add_argument('--outdir', type=Path, default=Path('/results/paired'))
    parser.add_argument('--policy', choices=('none', 'easy', 'hybrid', 'conservative'))
    parser.add_argument('--suite', choices=('small', 'large', 'all'), default='all')
    parser.add_argument('--queue-depth', type=int, choices=(32, 64, 2048), default=2048,
                        help='Large-suite examination cap (default: 2048 covers every job); '
                             '32 and 64 are finite-depth diagnostics')
    parser.add_argument('--reference-timeout', type=int, default=300,
                        help='Real-time timeout per large reference run, in seconds')
    args = parser.parse_args()
    root = args.outdir
    root.mkdir(parents=True, exist_ok=True)
    configurations = []
    if args.suite in ('small', 'all'):
        configurations.extend((policy, reference_policy, reservations, 100, workloads(), 120)
                              for policy, reference_policy, reservations in
                              [('none', 'fcfs', 0), ('easy', 'easy', 1),
                               ('hybrid', 'hybrid', 1), ('hybrid', 'hybrid', 2),
                               ('conservative', 'conservative', None)])
    if args.suite in ('large', 'all'):
        # Keep the default large checks at FCFS/no-backfill. Other policies
        # require an explicit selection so exploratory runs do not enter CI.
        policy = args.policy or 'none'
        reservations = {'none': 0, 'easy': 1, 'hybrid': 32, 'conservative': None}[policy]
        configurations.append((policy, 'fcfs' if policy == 'none' else policy,
                               reservations, args.queue_depth,
                               large_workloads(), args.reference_timeout))
    report = {'scope': 'Selected event-driven policy sets on one homogeneous four-node pool',
              'assumptions': {'equal_priority': True, 'whole_nodes': True,
                              'system_latency': False, 'planning_resolution': 'exact integer times',
                              'horizon': 'unlimited', 'reservation_admission': 'resource_time',
                              'per_pass_accounting': True,
                              'queue_caps': 'Small suite uses 100; large suite uses the selected cap',
                              'large_queue_depth': args.queue_depth,
                              'large_queue_covers_trace': args.queue_depth >= 2000},
              'cases': []}
    report['provenance'] = {
        'reference_sources': json.loads(Path('/results/sources.json').read_text()),
        'simulator_sources': json.loads(Path('/results/dr_evt-sources.json').read_text()),
        'harness_sha256': {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                           for name in ('test_paired.py', 'fiction_runner.py', 'build_dr_evt.py')},
    }
    failures = []
    for policy, reference_policy, reservations, depth, traces, timeout in configurations:
        if args.policy and policy != args.policy:
            continue
        for name, jobs in traces.items():
            label = f'{policy}_r{reservations if reservations is not None else "all"}_q{depth}_{name}'
            case = root / label
            case.mkdir(exist_ok=True)
            entry = {'case': name, 'priority_policy': 'fcfs', 'backfill_policy': policy,
                     'reservation_depth': reservations if reservations is not None else 'all_examined',
                     'per_pass_accounting': True,
                     'reference_queue_depth': depth, 'dr_evt_backfill_queue_depth': depth,
                     'job_count': len(jobs), 'reference_timeout_seconds': timeout,
                     'jobs': jobs, 'status': 'failed'}
            try:
                observed = simulator_case(case, jobs, args.simulator, policy,
                                          entry['dr_evt_backfill_queue_depth'], reservations)
                entry.update({'dr_evt_schedule': observed, 'dr_evt_occupancy': profile(observed)})
                reference = reference_schedule(
                    run_case(case, jobs, reference_policy, depth,
                             reservations if policy == 'hybrid' else None, timeout=timeout), jobs)
                entry.update({'reference_schedule': reference, 'dr_evt_schedule': observed,
                              'reference_occupancy': profile(reference), 'dr_evt_occupancy': profile(observed)})
                if reference != observed or profile(reference) != profile(observed):
                    differences = [{'job': index, 'reference': actual, 'dr_evt': expected}
                                   for index, (actual, expected) in enumerate(zip(reference, observed))
                                   if actual != expected]
                    entry['differing_jobs'] = len(differences)
                    entry['first_differences'] = differences[:10]
                    raise AssertionError(f'{len(differences)} jobs differ in start, completion, or allocated nodes')
                golden = expected_starts(name, policy, reservations)
                if golden is not None:
                    entry['hand_computed_starts'] = golden
                    if [job['start'] for job in observed] != golden:
                        raise AssertionError(('hand-computed starts differ', golden))
                entry['status'] = 'passed'
                print(f'PASS {label}: starts/completions/allocations agree', flush=True)
            except (AssertionError, subprocess.SubprocessError, OSError, ValueError) as error:
                entry['error'] = str(error)
                failures.append(label)
                print(f'FAIL {label}: {error}; artifacts: {case}', flush=True)
            report['cases'].append(entry)
            (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    report['checks'] = len(report['cases'])
    report['failures'] = failures
    (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    if failures:
        raise SystemExit(f'{len(failures)} of {report["checks"]} paired cases failed: {failures}')
    print(f'{report["checks"]} paired policy cases passed', flush=True)


if __name__ == '__main__':
    main()
