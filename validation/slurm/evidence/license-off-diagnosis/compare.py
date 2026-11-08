"""Compare native Slurm simulator and DR_EVT periodic backfill schedules."""
import argparse
import csv
from decimal import Decimal
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import time

EPOCH = 1700000040
PREFIX = Path('/workspace/slurm-install')


def write_trace(path, jobs):
    """Slurm simulator 17.11.7 binary ABI: little-endian int32/int64."""
    with path.open('wb') as stream:
        def number(fmt, value):
            stream.write(struct.pack('<' + fmt, value))

        def string(value):
            encoded = value.encode()
            number('i', len(encoded))
            stream.write(encoded)

        for index, (arrival, nodes, limit, duration) in enumerate(jobs, 1):
            if any(int(value) != value for value in (arrival, nodes, limit, duration)):
                raise ValueError('Native simulator requires integral arrivals and durations')
            if limit % 60:
                raise ValueError('Time limits must be whole minutes; normalize both inputs explicitly')
            number('i', index)
            string('root')
            number('q', EPOCH + int(arrival))
            number('i', int(duration))
            number('i', int(limit // 60))
            number('i', int(nodes))
            for value in ('', 'normal', ''):
                string(value)
            number('i', 1)
            number('i', 1)
            string('')
            string('')
            number('Q', 0)
            string('')
            string('')
            number('i', 0)
            number('q', 0)


def run_slurm(directory, jobs, capacity, interval, reservations, depth, resolution, horizon):
    directory.mkdir(parents=True, exist_ok=False)
    (directory / 'state').mkdir(exist_ok=True)
    write_trace(directory / 'jobs.trace', jobs)
    parameters = (f'sched_min_interval=0,batch_sched_delay=0,sched_interval=60,'
                  f'default_queue_depth=100000,bf_interval={interval},bf_interval_sleep=0,'
                  f'bf_max_job_test=4096,bf_max_job_part={depth},bf_job_part_count_reserve={reservations},'
                  f'bf_resolution={resolution},bf_window={horizon // 60},'
                  'max_rpc_cnt=0,max_sched_time=30,bf_max_time=1000')
    config = f'''ClusterName=validation
ControlMachine=localhost
ControlAddr=localhost
SlurmUser=root
SlurmdUser=root
AuthType=auth/none
CryptoType=crypto/munge
PluginDir={PREFIX}/lib/slurm
StateSaveLocation={directory}/state
SlurmctldPidFile={directory}/slurmctld.pid
SlurmctldLogFile={directory}/controller.log
SlurmdSpoolDir={directory}/spool
SlurmctldPort=16817
SlurmdPort=16818
SlurmctldDebug=3
ReturnToService=2
FastSchedule=1
SchedulerType=sched/backfill
SchedulerParameters={parameters}
SelectType=select/linear
PriorityType=priority/basic
ProctrackType=proctrack/linuxproc
TaskPlugin=task/none
SwitchType=switch/none
AccountingStorageType=accounting_storage/none
JobAcctGatherType=jobacct_gather/none
JobCompType=jobcomp/filetxt
JobCompLoc={directory}/jobcomp.log
JobRequeue=0
MinJobAge=300
SlurmdTimeout=0
SlurmctldTimeout=0
MessageTimeout=60
MailProg=/bin/true
FrontendName=localhost FrontendAddr=127.0.0.1 State=UNKNOWN
NodeName=n[0000-{capacity-1:04d}] CPUs=1 Sockets=1 CoresPerSocket=1 ThreadsPerCore=1 RealMemory=1024 State=UNKNOWN
PartitionName=normal Nodes=ALL Default=YES MaxTime=INFINITE State=UP Shared=EXCLUSIVE
'''
    (directory / 'slurm.conf').write_text(config)
    (directory / 'users.sim').write_text('')
    (directory / 'sim.conf').write_text(f'''JobsTraceFile={directory}/jobs.trace
TimeStart={EPOCH-30}
StartSecondsBeforeFirstJob=30
TimeStop=1
TimeStep=1
AfterJobLaunchTimeIncreament=0
BFBetweenJobsChecksTimeIncreament=0
scalingFactor=0
SharedMemoryName=/validation_slurm_{os.getpid()}
SimStats={directory}/sim-stats.txt
''')
    env = dict(os.environ, SLURM_CONF=str(directory / 'slurm.conf'), TZ='UTC',
               LD_LIBRARY_PATH=str(PREFIX / 'lib'),
               SLURM_VALIDATION_CONTROLLED_CLOCK='1',
               SLURM_VALIDATION_SNAPSHOTS=str(directory / 'snapshots.jsonl'))
    command = [str(PREFIX / 'sbin/slurmctld'), '-D', '-f', str(directory / 'slurm.conf')]
    (directory / 'command.json').write_text(json.dumps(command) + '\n')
    (directory / 'environment.json').write_text(json.dumps(
        {key: env[key] for key in ('SLURM_CONF', 'TZ', 'LD_LIBRARY_PATH',
                                  'SLURM_VALIDATION_CONTROLLED_CLOCK',
                                  'SLURM_VALIDATION_SNAPSHOTS')}, indent=2) + '\n')
    started = time.monotonic()
    with (directory / 'stdout.log').open('w') as log:
        result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=3600)
    if result.returncode:
        raise RuntimeError(f'Slurm exited {result.returncode}: {(directory / "stdout.log").read_text()[-4000:]}')
    log_text = (directory / 'stdout.log').read_text()
    settings = f'VALIDATION_BACKFILL interval={interval} table_limit=4096 partition_depth={depth} reservations={reservations} resolution={resolution} horizon={horizon}'
    assert settings in log_text, ('Native backfill settings not loaded', settings)
    assert 'sched_min_interval=0' in log_text
    assert 'Failed submission' not in log_text
    placement_counts = [int(value) for value in re.findall(
        r'Last depth cycle \(try sched\): (\d+)', (directory / 'sim-stats.txt').read_text())]
    assert all(count <= depth for count in placement_counts), ('Native candidate cap exceeded', placement_counts)
    (directory / 'cycle-audit.json').write_text(json.dumps(
        {'backfill_cycles': len(placement_counts), 'maximum_placement_tests': max(placement_counts, default=0),
         'partition_candidate_cap': depth}, indent=2) + '\n')
    schedule = {}
    for line in (directory / 'jobcomp.log').read_text().splitlines():
        fields = dict(re.findall(r'(\w+)=(\S+)', line))
        if fields['JobState'] != 'COMPLETED':
            raise AssertionError(fields)
        index = int(fields['JobId']) - 1
        if index in schedule:
            raise AssertionError('Duplicate completion record')
        def timestamp(name):
            return datetime.fromisoformat(fields[name]).replace(tzinfo=timezone.utc).timestamp() - EPOCH
        schedule[index] = {'start': timestamp('StartTime'), 'end': timestamp('EndTime'),
                           'nodes': int(fields['NodeCnt']), 'assignment': fields['NodeList']}
    if set(schedule) != set(range(len(jobs))):
        raise AssertionError(f'Expected {len(jobs)} jobs, got {len(schedule)}')
    return [schedule[i] for i in range(len(jobs))], time.monotonic() - started


def run_dr_evt(directory, jobs, binary, capacity, interval, reservations, depth, resolution, horizon):
    directory.mkdir(parents=True, exist_ok=False)
    with (directory / 'jobs.csv').open('w') as stream:
        writer = csv.writer(stream)
        writer.writerow(['job_submit_time', 'num_nodes', 'time_limit', 'duration'])
        writer.writerows(jobs)
    command = [str(binary), str(directory / 'jobs.csv'), '--total_nodes', str(capacity),
               '--trace_format', 'simple', '--timestamp_format', 'epoch', '--run_time_mode', 'actual',
               '--priority_policy', 'fcfs', '--queue_impl', 'circular', '--backfill_policy', 'hybrid',
               '--dispatch_interval', '0',
               '--reservation_depth', str(reservations), '--backfill_interval', str(interval),
               '--backfill_queue_depth', str(depth), '--planning_time_resolution', str(resolution),
               '--planning_horizon', str(horizon), '--msec_output',
               '--max_jobs', str(len(jobs)), '--outfile', str(directory / 'schedule.csv'),
               '--resource_trace', str(directory / 'resources.csv')]
    (directory / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    started = time.monotonic()
    with (directory / 'stdout.log').open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900)
    with (directory / 'schedule.csv').open() as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != len(jobs):
        raise AssertionError('DR_EVT job count differs from input')
    for row, (arrival, nodes, limit, duration) in zip(rows, jobs):
        if (float(row['job_submit_time']), int(row['num_nodes']), float(row['time_limit'])) != (arrival, nodes, limit):
            raise AssertionError('DR_EVT output order or input metadata changed')
    schedule = [{'start': float(row['begin_time']), 'end': float(row['end_time']),
                 'nodes': int(row['num_nodes'])} for row in rows]
    return schedule, time.monotonic() - started


def profile(schedule):
    changes = {0: 0}
    for job in schedule:
        changes[job['start']] = changes.get(job['start'], 0) + job['nodes']
        changes[job['end']] = changes.get(job['end'], 0) - job['nodes']
    allocated = 0
    curve = []
    for timestamp, delta in sorted(changes.items()):
        allocated += delta
        if not curve or allocated != curve[-1][1]:
            curve.append([timestamp, allocated])
    return curve


def compress(samples):
    curve = []
    for timestamp, allocated in sorted(dict(samples).items()):
        if not curve or allocated != curve[-1][1]:
            curve.append([timestamp, allocated])
    return curve


def assignment_nodes(value):
    nodes = []
    for group, single in re.findall(r'n(?:\[([^]]+)\]|(\d+))', value):
        for token in (group or single).split(','):
            if '-' in token:
                first, last = map(int, token.split('-'))
                nodes.extend(range(first, last + 1))
            else:
                nodes.append(int(token))
    return nodes


def audit(directory, jobs, native, observed, capacity):
    expected_area = sum(nodes * duration for _, nodes, _, duration in jobs)
    for schedule in (native, observed):
        for (arrival, nodes, limit, duration), row in zip(jobs, schedule):
            assert row['start'] >= arrival
            assert row['nodes'] == nodes
            assert row['end'] - row['start'] == duration, (row, duration)
        curve = profile(schedule)
        assert all(0 <= count <= capacity for _, count in curve)
        actual_area = sum((b[0] - a[0]) * a[1] for a, b in zip(curve, curve[1:]))
        assert actual_area == expected_area
    snapshots = [json.loads(line) for line in (directory / 'slurm/snapshots.jsonl').read_text().splitlines()]
    for snapshot in snapshots:
        timestamp = snapshot['time'] - EPOCH
        owners = {}
        running = {}
        for row in snapshot['jobs']:
            index = row['id'] - 1
            assert index not in running
            assert len(row['nodes']) == jobs[index][1]
            assert sorted(row['nodes']) == sorted(assignment_nodes(native[index]['assignment']))
            running[index] = row['nodes']
            for node in row['nodes']:
                assert 0 <= node < capacity
                assert node not in owners, ('Duplicate node owner', timestamp, node)
                owners[node] = index
        assert set(owners) == set(snapshot['allocated']), ('Native allocation mismatch', snapshot)
        expected = {index for index, row in enumerate(native) if row['start'] <= timestamp < row['end']}
        assert set(running) == expected, ('Snapshot/job schedule mismatch', timestamp, running, expected)
    native_curve = compress([(max(0, row['time'] - EPOCH), len(row['allocated'])) for row in snapshots])
    assert native_curve == profile(native), ('Native curve mismatch', native_curve, profile(native))
    with (directory / 'dr_evt/resources.csv').open() as stream:
        resources = list(csv.DictReader(stream))
    for row in resources:
        assert int(row['allocated_nodes']) + int(row['free_nodes']) == capacity
    dr_curve = compress([(float(row['time']), int(row['allocated_nodes'])) for row in resources])
    assert dr_curve == profile(observed), ('DR_EVT resource curve mismatch', dr_curve, profile(observed))
    return {'status': 'passed', 'native_snapshots': len(snapshots),
            'capacity_and_node_ownership': 'passed', 'resource_area_node_seconds': expected_area,
            'resource_curves_agree_with_job_records': True}


def metrics(jobs, schedule, capacity, elapsed):
    makespan = max(row['end'] for row in schedule)
    area = sum(row['nodes'] * (row['end'] - row['start']) for row in schedule)
    return {'mean_wait_seconds': sum(row['start'] - job[0] for row, job in zip(schedule, jobs)) / len(jobs),
            'makespan_seconds': makespan, 'resource_area_node_seconds': area,
            'utilization_percent': 100 * area / (capacity * makespan),
            'process_elapsed_seconds': elapsed}


def compare_case(directory, jobs, binary, capacity, interval, reservations, depth, resolution, horizon):
    native, elapsed = run_slurm(directory / 'slurm', jobs, capacity, interval, reservations, depth, resolution, horizon)
    observed, dr_elapsed = run_dr_evt(directory / 'dr_evt', jobs, binary, capacity, interval, reservations, depth, resolution, horizon)
    audits = audit(directory, jobs, native, observed, capacity)
    curves = {'slurm': profile(native), 'dr_evt': profile(observed)}
    times = sorted({t for curve in curves.values() for t, _ in curve})
    positions = {'slurm': 0, 'dr_evt': 0}
    counts = {'slurm': 0, 'dr_evt': 0}
    difference_area = 0
    for start, end in zip(times, times[1:]):
        for name, curve in curves.items():
            while positions[name] < len(curve) and curve[positions[name]][0] <= start:
                counts[name] = curve[positions[name]][1]
                positions[name] += 1
        difference_area += (end - start) * abs(counts['slurm'] - counts['dr_evt'])
    report = {'jobs': jobs, 'capacity': capacity, 'backfill_interval_seconds': interval,
              'reservation_depth': reservations, 'backfill_queue_depth': depth,
              'planning_resolution_seconds': resolution, 'planning_horizon_seconds': horizon,
              'throttling': {'sched_min_interval': 0, 'batch_sched_delay': 0, 'defer': False},
              'controlled_clock': 'one-second steps; host execution does not advance simulation time',
              'slurm_schedule': native, 'dr_evt_schedule': observed, 'resource_curves': curves,
              'slurm_metrics': metrics(jobs, native, capacity, elapsed),
              'dr_evt_metrics': metrics(jobs, observed, capacity, dr_elapsed),
              'different_start_times': sum(a['start'] != b['start'] for a, b in zip(native, observed)),
              'max_start_difference_seconds': max(abs(a['start'] - b['start']) for a, b in zip(native, observed)),
              'resource_curve_difference_node_seconds': difference_area, 'audits': audits}
    report['resource_curve_difference_percent'] = 100 * difference_area / (capacity * times[-1])
    (directory / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    for name, curve in curves.items():
        with (directory / f'{name}-resource-curve.csv').open('w') as stream:
            writer = csv.writer(stream)
            writer.writerow(['time', 'allocated_nodes'])
            writer.writerows(curve)
    print(directory.name, {key: report[key] for key in ('different_start_times', 'max_start_difference_seconds', 'audits')}, flush=True)
    return report


def load_lassen(path, limit, capacity):
    with path.open() as stream:
        indexed = list(enumerate(csv.DictReader(stream)))
    indexed.sort(key=lambda item: (Decimal(item[1]['job_submit_time']), item[0]))
    if limit:
        indexed = indexed[:limit]
    origin = Decimal(indexed[0][1]['job_submit_time'])
    jobs, rounded = [], 0
    for _, row in indexed:
        arrival = Decimal(row['job_submit_time']) - origin
        duration = Decimal(row['end_time']) - Decimal(row['begin_time'])
        actual = math.ceil(duration)
        rounded += actual != duration
        nodes, walltime = int(row['num_nodes']), Decimal(row['time_limit'])
        if not (arrival % 1 == 0 and walltime % 60 == 0 and 0 < nodes <= capacity and 0 < actual <= walltime):
            raise ValueError(f'Unsupported workload row: {row}')
        jobs.append((int(arrival), nodes, int(walltime), actual))
    provenance = {'source_file': path.name, 'source_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                  'source_time_origin': str(origin), 'source_row_indices': [index for index, _ in indexed],
                  'selection': 'earliest arrivals, stable input order for ties', 'jobs': len(jobs),
                  'duration_conversion': 'ceil to whole seconds in both scheduler inputs',
                  'rounded_durations': rounded, 'arrivals_and_time_limits': 'unchanged'}
    return jobs, provenance


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--outdir', type=Path, default=Path('/results/paired'))
    parser.add_argument('--trace', type=Path)
    parser.add_argument('--max-jobs', type=int, default=2000)
    args = parser.parse_args()
    binary = Path('/workspace/dr_evt-simulator').read_text().strip()
    cases = {
        'all-fit': ([(0, 1, 60, 5)] * 4, [0, 0, 0, 0]),
        'completion-dispatch': ([(0, 4, 60, 7), (1, 4, 60, 3)], [0, 7]),
        'periodic-backfill': ([(0, 3, 120, 120), (1, 4, 60, 60), (1, 1, 60, 60)], [0, 120, 30]),
        'reservation-protection': ([(0, 3, 120, 120), (1, 4, 60, 60), (1, 1, 180, 180)], [0, 120, 180]),
        'four-job': ([(0, 2, 120, 120), (0, 2, 180, 180), (0, 3, 60, 60), (0, 1, 120, 120)], None),
    }
    for name, (jobs, expected) in cases.items():
        report = compare_case(args.outdir / name, jobs, binary, 4, 30, 1, 64, 1, 259200)
        if expected is not None:
            for key in ('slurm_schedule', 'dr_evt_schedule'):
                assert [row['start'] for row in report[key]] == expected, (name, key, report[key])
        elif name == 'four-job':
            assert [row['start'] for row in report['slurm_schedule']] == [0, 0, 180, 180]
            assert [row['start'] for row in report['dr_evt_schedule']] == [0, 0, 180, 120]
    jobs = [(0, 3, 120, 120), (1, 2, 60, 60), (1, 4, 60, 60), (1, 1, 180, 180)]
    for reservations, expected in ((1, [0, 120, 210, 30]), (2, [0, 120, 180, 240])):
        report = compare_case(args.outdir / f'reservation-depth-{reservations}', jobs, binary,
                              4, 30, reservations, 64, 1, 259200)
        for key in ('slurm_schedule', 'dr_evt_schedule'):
            assert [row['start'] for row in report[key]] == expected, (key, report[key])
    jobs = [(0, 3, 120, 120), (1, 4, 60, 60), (1, 4, 60, 60), (1, 1, 60, 60)]
    for depth, expected in ((2, [0, 120, 180, 240]), (3, [0, 120, 180, 30])):
        report = compare_case(args.outdir / f'queue-depth-{depth}', jobs, binary,
                              4, 30, 1, depth, 1, 259200)
        for key in ('slurm_schedule', 'dr_evt_schedule'):
            assert [row['start'] for row in report[key]] == expected, (key, report[key])
    if args.trace:
        jobs, provenance = load_lassen(args.trace, args.max_jobs, 756)
        directory = args.outdir / f'lassen-prefix{len(jobs)}-hybrid-r32-q64'
        directory.mkdir(parents=True, exist_ok=True)
        (directory / 'workload.json').write_text(json.dumps(provenance, indent=2) + '\n')
        compare_case(directory, jobs, binary, 756, 30, 32, 64, 1, 259200)


if __name__ == '__main__':
    main()
