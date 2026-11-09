"""Time all published DR_EVT comparisons with one binary and audit each run."""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import platform
import resource
import statistics
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit(case, directory):
    """Check job timing, capacity, and agreement with the published schedule."""
    with (directory / 'schedule.csv').open() as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == len(case['jobs'])
    schedule = []
    changes = {0.0: 0}
    for job, row in zip(case['jobs'], rows):
        arrival, nodes, limit, duration = job
        assert (float(row['job_submit_time']), int(row['num_nodes']),
                float(row['time_limit'])) == (arrival, nodes, limit)
        start, end = float(row['begin_time']), float(row['end_time'])
        assert start >= arrival
        assert abs(end - start - duration) <= 0.00101
        schedule.append([start, end])
        changes[start] = changes.get(start, 0) + nodes
        changes[end] = changes.get(end, 0) - nodes
    assert schedule == case['expected_schedule'], case['name']
    allocated, expected_profile = 0, []
    for timestamp, change in sorted(changes.items()):
        allocated += change
        assert 0 <= allocated <= case['capacity']
        if not expected_profile or allocated != expected_profile[-1][1]:
            expected_profile.append([timestamp, allocated])
    assert allocated == 0
    with (directory / 'resources.csv').open() as stream:
        snapshots = {}
        for row in csv.DictReader(stream):
            used, free = int(row['allocated_nodes']), int(row['free_nodes'])
            assert used + free == case['capacity']
            snapshots[float(row['time'])] = used
    observed = []
    for timestamp, used in sorted(snapshots.items()):
        if not observed or used != observed[-1][1]:
            observed.append([timestamp, used])
    assert observed == expected_profile, case['name']
    area = sum((right[0] - left[0]) * left[1]
               for left, right in zip(observed, observed[1:]))
    requested_area = sum(job[1] * job[3] for job in case['jobs'])
    assert abs(area - requested_area) <= 0.001 * sum(job[1] for job in case['jobs'])


def measure(case, binary, directory):
    directory.mkdir()
    command = [str(binary), str(directory.parent / 'jobs.csv')]
    command += case['command_options']
    command += ['--outfile', str(directory / 'schedule.csv'),
                '--resource_trace', str(directory / 'resources.csv')]
    (directory / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    with (directory / 'stdout.log').open('w') as stream:
        started = time.perf_counter()
        subprocess.run(command, check=True, stdout=stream, stderr=subprocess.STDOUT)
        elapsed = time.perf_counter() - started
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    audit(case, directory)
    return {'wall_seconds': elapsed, 'user_seconds': after.ru_utime - before.ru_utime,
            'system_seconds': after.ru_stime - before.ru_stime,
            'schedule_sha256': digest(directory / 'schedule.csv'),
            'resources_sha256': digest(directory / 'resources.csv'),
            'published_schedule_matches': True, 'capacity_and_duration_audits_passed': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('simulator', type=Path)
    parser.add_argument('--cases', type=Path, default=Path(__file__).with_name('cases.json'))
    parser.add_argument('--outdir', type=Path, required=True)
    parser.add_argument('--runs', type=int, default=5)
    args = parser.parse_args()
    if args.runs < 1:
        parser.error('--runs must be positive')
    output = args.outdir.resolve()
    output.mkdir(exist_ok=False)
    binary = args.simulator.resolve()
    results = []
    for case in json.loads(args.cases.read_text()):
        folder = output / case['name']
        folder.mkdir()
        with (folder / 'jobs.csv').open('w', newline='') as stream:
            writer = csv.writer(stream, lineterminator='\n')
            writer.writerow(['job_submit_time', 'num_nodes', 'time_limit', 'actual_run_time'])
            writer.writerows(case['jobs'])
        warmup = measure(case, binary, folder / 'warmup')
        runs = []
        for number in range(1, args.runs + 1):
            row = measure(case, binary, folder / f'run-{number:03d}')
            for key in ('schedule_sha256', 'resources_sha256'):
                assert row[key] == warmup[key], (case['name'], key)
            runs.append({'run': number, **row})
        times = [row['wall_seconds'] for row in runs]
        result = {'case': case['name'], 'jobs': len(case['jobs']),
                  'command_options': case['command_options'],
                  'input_sha256': digest(folder / 'jobs.csv'),
                  'warmup': warmup, 'runs': runs,
                  'median_wall_seconds': statistics.median(times),
                  'min_wall_seconds': min(times), 'max_wall_seconds': max(times)}
        results.append(result)
        (folder / 'performance.json').write_text(json.dumps(result, indent=2) + '\n')
        print(case['name'], 'median:', result['median_wall_seconds'], flush=True)
    report = {'platform': platform.platform(), 'machine': platform.machine(),
              'simulator_sha256': digest(binary), 'cases_sha256': digest(args.cases),
              'scope': 'Process launch, input, simulation, schedule/resource output, and exit; '
                       'excludes builds, Python parsing, audits, and plotting',
              'warmup': 'One untimed run per case; filesystem caches remain warm',
              'execution': 'All cases run sequentially in one container with the same binary',
              'results': results}
    (output / 'performance.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
