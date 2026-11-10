"""Alternate isolated Release binaries; audit outputs and save per-run data."""
import argparse
import csv
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, folder):
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'command.json').write_text(json.dumps(command, indent=2))
    with (folder / 'stdout.log').open('w') as output:
        begin = time.perf_counter()
        process = subprocess.Popen(command, cwd=folder, stdout=output,
                                   stderr=subprocess.STDOUT)
        _, status, usage = os.wait4(process.pid, 0)
        elapsed = time.perf_counter() - begin
        process.returncode = os.waitstatus_to_exitcode(status)
        if process.returncode:
            raise RuntimeError((command, (folder / 'stdout.log').read_text()))
    return dict(wall_seconds=elapsed, user_seconds=usage.ru_utime,
                system_seconds=usage.ru_stime,
                peak_rss_bytes=usage.ru_maxrss if platform.system() == 'Darwin'
                else usage.ru_maxrss * 1024)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--before-revision', required=True,
                        help='Git revision used for the before build')
    parser.add_argument('--after-revision', required=True,
                        help='Git revision used for the after build')
    parser.add_argument('--runs', type=int, default=9)
    args = parser.parse_args()
    root, repo = args.root.resolve(), args.repo.resolve()
    before_revision, after_revision = [
        subprocess.check_output(
            ['git', 'rev-parse', '--verify', '--end-of-options', revision+'^{commit}'],
            cwd=repo, text=True).strip()
        for revision in (args.before_revision, args.after_revision)]
    source_diff = subprocess.check_output(
        ['git', 'diff', '--binary', '--no-ext-diff', '--no-textconv', '--no-color',
         before_revision, after_revision, '--'], cwd=repo)
    output = root / 'measurements'
    output.mkdir(exist_ok=True)
    path = repo / 'docs/_static/scheduler-validation/performance/measure_dr_evt.py'
    spec = importlib.util.spec_from_file_location('published_audit', path)
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    cases = json.loads(path.with_name('cases.json').read_text())
    for case in cases:
        folder = output / case['name']
        folder.mkdir(exist_ok=True)
        with (folder / 'jobs.csv').open('w', newline='') as stream:
            writer = csv.writer(stream, lineterminator='\n')
            writer.writerow(['job_submit_time', 'num_nodes', 'time_limit', 'actual_run_time'])
            writer.writerows(case['jobs'])
        case['input'] = str(folder / 'jobs.csv')
        case['audit_published'] = True
    for policy, queue in [('easy', 'circular'), ('easy', 'deque'),
                          ('hybrid', 'circular'), ('conservative', 'circular')]:
        count = 2000 if policy == 'conservative' else 10000
        cases.append(dict(name=f'scale-{policy}-{queue}',
                          input=str(repo / f'tests/test_traces/scale/huge_{count}jobs.csv'),
                          command_options=['--total_nodes', '795', '--queue_impl', queue,
                              '--priority_policy', 'fcfs_conservative' if policy == 'conservative' else 'fcfs',
                              '--backfill_policy', policy, '--reservation_depth', '32',
                              '--backfill_queue_depth', '64', '--run_time_mode', 'limit',
                              '--trace_format', 'simple', '--timestamp_format', 'epoch'],
                          audit_published=False))
    report = dict(platform=platform.platform(), machine=platform.machine(),
                  base_revision=before_revision, after_revision=after_revision,
                  source_diff_sha256=hashlib.sha256(source_diff).hexdigest(),
                  compiler=subprocess.check_output(['/usr/bin/c++', '--version'], text=True),
                  configure_options=json.loads((root / 'configure-options.json').read_text()),
                  execution='One warmup, then alternating AB/BA paired runs; sequential, warm filesystem caches',
                  scope='Process launch, trace parsing, scheduling, schedule/resource output, and exit; no API queries',
                  binaries={tag: digest(root / (tag+'-build') / 'simulator') for tag in ('before','after')},
                  libraries={tag: digest(root / (tag+'-build') / 'libdr_evt.dylib') for tag in ('before','after')},
                  cases=[])
    for case in cases:
        data = dict(name=case['name'], input_sha256=digest(Path(case['input'])),
                    options=case['command_options'], runs=[], warmup=[])
        reference = None
        for number in range(args.runs + 1):
            tags = ('before', 'after') if number % 2 == 0 else ('after', 'before')
            for tag in tags:
                folder = output / case['name'] / f'{number:02d}-{tag}'
                command = [str(root / (tag+'-build') / 'simulator'), case['input']]
                command += case['command_options'] + ['--outfile', str(folder / 'schedule.csv'),
                                                      '--resource_trace', str(folder / 'resources.csv')]
                row = dict(version=tag, repetition=number, **run(command, folder))
                row.update(schedule_sha256=digest(folder / 'schedule.csv'),
                           resources_sha256=digest(folder / 'resources.csv'))
                hashes = (row['schedule_sha256'], row['resources_sha256'])
                if reference is None:
                    reference = hashes
                assert hashes == reference, (case['name'], tag, 'schedule or resources changed')
                if case['audit_published']:
                    audit.audit(case, folder)
                data['warmup' if number == 0 else 'runs'].append(row)
            (output / 'performance.json').write_text(json.dumps(report | {'in_progress': data}, indent=2))
        data['summary'] = {}
        for tag in ('before', 'after'):
            rows = [r for r in data['runs'] if r['version'] == tag]
            data['summary'][tag] = {
                key: statistics.median(r[key] for r in rows)
                for key in ('wall_seconds', 'user_seconds', 'system_seconds', 'peak_rss_bytes')}
            data['summary'][tag]['wall_range'] = [min(r['wall_seconds'] for r in rows), max(r['wall_seconds'] for r in rows)]
        data['wall_change_percent'] = (data['summary']['after']['wall_seconds'] /
                                      data['summary']['before']['wall_seconds'] - 1) * 100
        data['outputs_identical'] = True
        data['published_audits_passed'] = case['audit_published']
        report['cases'].append(data)
        (output / 'performance.json').write_text(json.dumps(report, indent=2))
        print(case['name'], data['summary'], 'change', data['wall_change_percent'], flush=True)


if __name__ == '__main__':
    main()
