"""Time repeated DR_EVT processes using a saved validation command."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import resource
import shutil
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('result', type=Path)
    parser.add_argument('--runs', type=int, default=5)
    args = parser.parse_args()
    if args.runs < 1:
        parser.error('--runs must be positive')
    result = args.result.resolve()
    original = json.loads((result / 'dr_evt/command.json').read_text())
    output = result / 'performance'
    output.mkdir(exist_ok=False)
    expected = {name: hashlib.sha256((result / 'dr_evt' / name).read_bytes()).hexdigest()
                for name in ('schedule.csv', 'resources.csv')}
    measurements = []
    for number in range(1, args.runs + 1):
        directory = output / f'run-{number:03d}'
        directory.mkdir()
        command = original.copy()
        for option, filename in (('--outfile', 'schedule.csv'), ('--resource_trace', 'resources.csv')):
            command[command.index(option) + 1] = str(directory / filename)
        (directory / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        with (directory / 'stdout.log').open('w') as log:
            started = time.perf_counter()
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
            elapsed = time.perf_counter() - started
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
        for filename, digest in expected.items():
            assert hashlib.sha256((directory / filename).read_bytes()).hexdigest() == digest, filename
        measurements.append({'run': number, 'wall_seconds': elapsed,
                             'user_seconds': after.ru_utime - before.ru_utime,
                             'system_seconds': after.ru_stime - before.ru_stime,
                             'schedule_and_resources_identical': True})
    times = [row['wall_seconds'] for row in measurements]
    performance = {
        'scheduler': 'DR_EVT event-driven FCFS dispatch with periodic hybrid backfill',
        'jobs': len(json.loads((result / 'report.json').read_text())['jobs']),
        'bf_licenses_reference': True,
        'timing_scope': 'Process launch, trace input, simulation, schedule/resource output and process exit; excludes Python output parsing, audits and plotting',
        'warmup': 'The preceding validation run populated filesystem caches',
        'interventions': [], 'platform': platform.platform(),
        'machine': platform.machine(), 'provenance': json.loads((result / 'provenance.json').read_text()),
        'runs': measurements, 'median_wall_seconds': statistics.median(times),
        'min_wall_seconds': min(times), 'max_wall_seconds': max(times),
        'mean_wall_seconds': statistics.mean(times),
    }
    (result / 'performance.json').write_text(json.dumps(performance, indent=2) + '\n')
    shutil.copy2(Path(__file__), result / Path(__file__).name)
    print(json.dumps({key: performance[key] for key in (
        'jobs', 'median_wall_seconds', 'min_wall_seconds', 'max_wall_seconds', 'timing_scope')}, indent=2))


if __name__ == '__main__':
    main()
