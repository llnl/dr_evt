"""Compare native OpenPBS with DR_EVT on an identical, whole-second workload."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import random
import subprocess
import time


def dump(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def curve(jobs, schedule):
    changes = {0: 0}
    for (_, units, _, duration), (start, end) in zip(jobs, schedule):
        assert end - start == duration
        changes[start] = changes.get(start, 0) + units
        changes[end] = changes.get(end, 0) - units
    used, result = 0, []
    for timestamp, delta in sorted(changes.items()):
        used += delta
        if not result or used != result[-1][1]:
            result.append([timestamp, used])
    return result


def audit(jobs, schedule, capacity):
    assert len(jobs) == len(schedule)
    for (submit, _, _, duration), (start, end) in zip(jobs, schedule):
        assert start >= submit
        assert end - start == duration
    profile = curve(jobs, schedule)
    assert all(0 <= used <= capacity for _, used in profile)
    area = sum((b[0] - a[0]) * a[1] for a, b in zip(profile, profile[1:]))
    assert area == sum(units * duration for _, units, _, duration in jobs)
    return profile


def metrics(jobs, schedule, capacity):
    waits = [start - job[0] for job, (start, _) in zip(jobs, schedule)]
    turns = [end - job[0] for job, (_, end) in zip(jobs, schedule)]
    makespan = max(end for _, end in schedule) - min(job[0] for job in jobs)
    return {'mean_wait_seconds': sum(waits) / len(waits),
            'mean_turnaround_seconds': sum(turns) / len(turns),
            'makespan_seconds': makespan,
            'utilization': sum(j[1] * j[3] for j in jobs) / (capacity * makespan)}


def experiment(args, name, jobs, capacity, reservations, expected=None):
    directory = args.outdir / name
    directory.mkdir(parents=True, exist_ok=True)
    dump(directory / 'workload.json', jobs)
    native_input = directory / 'native-input.csv'
    with native_input.open('w') as stream:
        writer = csv.writer(stream)
        for index, (submit, units, limit, duration) in enumerate(jobs, 1):
            writer.writerow([index, submit, units, duration, limit])
    (directory / 'sched_config').write_text(
        'strict_ordering: true ALL\n'
        'fair_share: false ALL\npreemptive_sched: false ALL\n'
        'dedicated_prefix: ded\nprimetime_prefix: p_\nnonprimetime_prefix: np_\n'
        'resources: "ncpus,mem,arch,host,vnode,capacity"\n'
        'max_job_check: ALL_JOBS\n')
    (directory / 'holidays').write_text('YEAR 2023\nweekday all none\nsaturday all none\nsunday all none\n')
    for filename in ('dedicated_time', 'resource_group'):
        (directory / filename).write_text('')
    native_command = [str(args.native), str(native_input), str(capacity), str(reservations),
                      str(directory / 'native.csv'), str(directory / 'native-events.csv')]
    started = time.monotonic()
    with (directory / 'native-stdout.log').open('w') as log:
        subprocess.run(native_command, cwd=directory, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=1800)
    native_elapsed = time.monotonic() - started
    with (directory / 'native.csv').open() as stream:
        rows = list(csv.DictReader(stream))
    assert [int(r['job_id']) for r in rows] == list(range(1, len(jobs) + 1))
    native = [[int(r['start']), int(r['end'])] for r in rows]
    dr_input = directory / 'jobs.csv'
    with dr_input.open('w') as stream:
        writer = csv.writer(stream)
        writer.writerow(['job_submit_time', 'num_nodes', 'time_limit', 'duration'])
        writer.writerows(jobs)
    dr_command = [str(args.simulator), str(dr_input), '--total_nodes', str(capacity),
                  '--trace_format', 'simple', '--timestamp_format', 'epoch',
                  '--run_time_mode', 'actual', '--priority_policy', args.priority_policy,
                  '--queue_impl', 'circular', '--backfill_policy', 'hybrid',
                  '--reservation_depth', str(reservations), '--per_pass_accounting',
                  '--backfill_queue_depth', str(len(jobs)), '--backfill_interval', '0',
                  '--dispatch_interval', '0', '--msec_output',
                  '--outfile', str(directory / 'dr_evt.csv'),
                  '--resource_trace', str(directory / 'dr_evt-resources.csv')]
    dump(directory / 'commands.json', {'openpbs': native_command, 'dr_evt': dr_command})
    started = time.monotonic()
    with (directory / 'dr_evt-stdout.log').open('w') as log:
        subprocess.run(dr_command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900)
    dr_elapsed = time.monotonic() - started
    with (directory / 'dr_evt.csv').open() as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == len(jobs)
    for row, job in zip(rows, jobs):
        assert [float(row['job_submit_time']), int(row['num_nodes']), float(row['time_limit'])] == list(job[:3])
    dr_evt = [[float(r['begin_time']), float(r['end_time'])] for r in rows]
    native_profile = audit(jobs, native, capacity)
    dr_profile = audit(jobs, dr_evt, capacity)
    with (directory / 'dr_evt-resources.csv').open() as stream:
        dr_snapshots = {}
        for row in csv.DictReader(stream):
            timestamp, used = float(row['time']), int(row['allocated_nodes'])
            assert used + int(row['free_nodes']) == capacity
            dr_snapshots[timestamp] = used
    dr_observed = []
    for timestamp, used in sorted(dr_snapshots.items()):
        if not dr_observed or used != dr_observed[-1][1]:
            dr_observed.append([timestamp, used])
    assert dr_observed == dr_profile
    # Verify adapter observations independently of the reconstructed schedule.
    active, snapshots = {}, {}
    with (directory / 'native-events.csv').open() as stream:
        for kind, timestamp, identity, value in csv.reader(stream):
            timestamp, value = int(timestamp), int(value)
            if kind in ('start', 'end'):
                index = int(identity) - 1
                if kind == 'start':
                    assert index not in active
                    active[index] = jobs[index][1]
                else:
                    assert index in active
                    del active[index]
                assert sum(active.values()) == value
                snapshots[timestamp] = value
            elif kind == 'snapshot':
                assert sum(active.values()) == value
                snapshots[timestamp] = value
    assert not active
    observed_profile = []
    for timestamp, value in sorted(snapshots.items()):
        if not observed_profile or value != observed_profile[-1][1]:
            observed_profile.append([timestamp, value])
    assert observed_profile == native_profile
    if expected is not None:
        assert [s[0] for s in native] == expected, (name, native, expected)
    differences = [{'job_id': i + 1, 'openpbs': n, 'dr_evt': d}
                   for i, (n, d) in enumerate(zip(native, dr_evt)) if n != d]
    report = {'name': name, 'jobs': len(jobs), 'capacity': capacity,
              'priority_policy': args.priority_policy,
              'reservation_depth': reservations, 'exact_match': not differences,
              'different_jobs': len(differences), 'differences': differences,
              'openpbs_schedule': native, 'dr_evt_schedule': dr_evt,
              'openpbs_profile': native_profile, 'dr_evt_profile': dr_profile,
              'openpbs_metrics': metrics(jobs, native, capacity),
              'dr_evt_metrics': metrics(jobs, dr_evt, capacity),
              'openpbs_wall_seconds': native_elapsed, 'dr_evt_wall_seconds': dr_elapsed,
              'capacity_and_ownership_audit_passed': True}
    dump(directory / 'report.json', report)
    print(f'{name}: {len(jobs)} jobs, {len(differences)} differ', flush=True)
    return report


def lassen(path, count):
    with path.open() as stream:
        rows = list(csv.DictReader(stream))
    rows = sorted(enumerate(rows), key=lambda pair: (float(pair[1]['job_submit_time']), pair[0]))
    if count: rows = rows[:count]
    origin = float(rows[0][1]['job_submit_time'])
    jobs = []
    for _, row in rows:
        submit = math.ceil(float(row['job_submit_time']) - origin)
        duration = max(1, math.ceil(float(row['end_time']) - float(row['begin_time'])))
        limit = max(duration, math.ceil(float(row['time_limit'])))
        jobs.append((submit, int(row['num_nodes']), limit, duration))
    return jobs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--simulator', type=Path, required=True)
    parser.add_argument('--trace', type=Path)
    parser.add_argument('--max-jobs', type=int, default=2000)
    parser.add_argument('--priority_policy', choices=('fcfs', 'fcfs_1s_evt_group'), default='fcfs_1s_evt_group',
                        help='Original FCFS or the opt-in OpenPBS forecast policy')
    parser.add_argument('--outdir', type=Path, required=True)
    args = parser.parse_args()
    if args.max_jobs < 0:
        parser.error('--max-jobs must be nonnegative')
    args.outdir = args.outdir.resolve()
    args.outdir.mkdir(parents=True, exist_ok=True)
    cases = [
        ('backfill-allowed', [(0, 3, 120, 120), (0, 4, 20, 20), (0, 1, 20, 20)], 4, 1, [0, 120, 0]),
        ('backfill-blocked', [(0, 3, 120, 120), (0, 4, 20, 20), (0, 1, 200, 200)], 4, 1, [0, 120, 140]),
        ('early-completion', [(0, 3, 120, 60), (0, 4, 20, 20), (0, 1, 200, 200)], 4, 1, [0, 60, 80]),
        ('simultaneous-events', [(0, 2, 30, 30), (0, 2, 30, 30), (5, 4, 20, 20), (30, 1, 10, 10)], 4, 2, [0, 0, 30, 50]),
        ('endpoint', [(0, 3, 120, 120), (0, 4, 20, 20), (0, 1, 120, 120)], 4, 1, [0, 120, 0]),
        ('depth-one', [(0, 3, 120, 120), (0, 2, 20, 20), (0, 4, 100, 100), (0, 1, 200, 200)], 4, 1, [0, 120, 200, 0]),
        ('depth-two', [(0, 3, 120, 120), (0, 2, 20, 20), (0, 4, 100, 100), (0, 1, 200, 200)], 4, 2, [0, 120, 140, 240]),
    ]
    reports = [experiment(args, *case) for case in cases]
    rng = random.Random(20261108)
    for index in range(12):
        jobs = sorted([(rng.randrange(0, 100), rng.randrange(1, 9), rng.randrange(10, 100))
                       for _ in range(30)])
        jobs = [(a, n, limit, rng.randrange(1, limit + 1)) for a, n, limit in jobs]
        reports.append(experiment(args, f'random-{index:02d}', jobs, 8, (1, 2, 32)[index % 3]))
    assert all(r['exact_match'] for r in reports), 'A matching control changed'
    adjacent = [(0, 1, 10, 10), (0, 1, 11, 11), (0, 2, 2, 2), (0, 1, 11, 11)]
    diagnostic = experiment(args, 'adjacent-release-calendar', adjacent, 3, 1, [0, 0, 11, 0])
    mimic = args.priority_policy == 'fcfs_1s_evt_group'
    assert [s[0] for s in diagnostic['dr_evt_schedule']] == (
        [0, 0, 11, 0] if mimic else [0, 0, 10, 11])
    diagnostic['expected_difference'] = not mimic
    dump(args.outdir / diagnostic['name'] / 'report.json', diagnostic)
    reports.append(diagnostic)
    separated = [(0, 1, 10, 10), (0, 1, 12, 12), (0, 2, 2, 2), (0, 1, 12, 12)]
    spacing = experiment(args, 'separated-release-calendar', separated, 3, 1, [0, 0, 10, 12])
    assert spacing['exact_match']
    reports.append(spacing)
    if args.trace is not None:
        jobs = lassen(args.trace, args.max_jobs)
        dump(args.outdir / 'trace-conversion.json', {
            'source_sha256': hashlib.sha256(args.trace.read_bytes()).hexdigest(),
            'selected_jobs': len(jobs), 'selection': 'earliest jobs in stable arrival order',
            'conversion': 'Relative arrivals, actual runtimes and walltime limits rounded up to integer seconds; runtime >= 1; limit >= runtime.',
            'initial_state': 'empty', 'capacity': 756})
        name = f'lassen-prefix{len(jobs)}-r32'
        reports.append(experiment(args, name, jobs, 756, 32))
        if mimic:
            assert reports[-1]['exact_match'], 'OpenPBS forecast policy differs from native'
        from plot import plot
        plot(args.outdir / name / 'report.json')
        from diagnose import diagnose
        if not reports[-1]['exact_match']:
            diagnose(args.outdir / name / 'report.json')
    summary = [{k: r[k] for k in ('name', 'jobs', 'exact_match', 'different_jobs',
                                'capacity_and_ownership_audit_passed', 'openpbs_metrics', 'dr_evt_metrics')}
               for r in reports]
    dump(args.outdir / 'summary.json', summary)
    # The primary trace records measured differences. Matching controls and the
    # adjacent-event divergence are assertions, as are all capacity audits.


if __name__ == '__main__':
    main()
