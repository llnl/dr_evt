"""Compare a rebuilt DR_EVT with an audited, saved native Slurm run."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compare import compress, metrics, profile, run_dr_evt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('native_run', type=Path)
    parser.add_argument('binary', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--skip-plots', action='store_true',
                        help='Generate plots separately in an environment with matplotlib')
    args = parser.parse_args()
    native_run = args.native_run.resolve()
    binary = args.binary.resolve()
    output = args.output.resolve()
    report_path = native_run / 'report.json'
    report = json.loads(report_path.read_text())
    audit_path = native_run / 'audit.json'
    native_audit = json.loads(audit_path.read_text()) if audit_path.exists() else report['audits']
    if audit_path.exists():
        assert native_audit['status'] == 'passed'
    else:
        assert native_audit['runtime_and_capacity'] == 'passed'
        assert native_audit['private_node_assignments'] == 'passed'
    old_schedule = report['dr_evt_schedule']
    previous_command = json.loads((native_run / 'dr_evt/command.json').read_text())
    resolution = int(previous_command[previous_command.index('--planning_time_resolution') + 1])
    horizon = int(previous_command[previous_command.index('--planning_horizon') + 1])
    output.mkdir(parents=True, exist_ok=False)
    observed, elapsed = run_dr_evt(
        output / 'dr_evt', report['jobs'], binary, report['capacity'],
        report['backfill_interval_seconds'], report['reservation_depth'],
        report['backfill_queue_depth'], resolution, horizon)
    for row, job in zip(observed, report['jobs']):
        assert row['start'] >= job[0]
        assert row['end'] - row['start'] == job[3]
        assert row['nodes'] == job[1]
    curve = profile(observed)
    assert all(0 <= nodes <= report['capacity'] for _, nodes in curve)
    with (output / 'dr_evt/resources.csv').open() as stream:
        resources = list(csv.DictReader(stream))
    assert all(int(row['allocated_nodes']) + int(row['free_nodes']) == report['capacity']
               for row in resources)
    assert compress([(float(row['time']), int(row['allocated_nodes']))
                     for row in resources]) == curve
    report['dr_evt_schedule'] = observed
    report['dr_evt_metrics'] = metrics(report['jobs'], observed, report['capacity'], elapsed)
    report['resource_curves']['dr_evt'] = curve
    native = report['slurm_schedule']
    differing = [i for i, (a, b) in enumerate(zip(native, observed)) if a['start'] != b['start']]
    first = min(differing, key=lambda i: (min(native[i]['start'], observed[i]['start']), i),
                default=None)
    report['different_start_times'] = len(differing)
    report['max_start_difference_seconds'] = max(
        abs(a['start'] - b['start']) for a, b in zip(native, observed))
    times = sorted({t for c in report['resource_curves'].values() for t, _ in c})
    positions = {name: 0 for name in report['resource_curves']}
    counts = dict.fromkeys(positions, 0)
    area = 0
    for begin, end in zip(times, times[1:]):
        for name, c in report['resource_curves'].items():
            while positions[name] < len(c) and c[positions[name]][0] <= begin:
                counts[name] = c[positions[name]][1]
                positions[name] += 1
        area += (end - begin) * abs(counts['slurm'] - counts['dr_evt'])
    report['resource_curve_difference_node_seconds'] = area
    report['resource_curve_difference_percent'] = 100 * area / (report['capacity'] * times[-1])
    report['audits'] = {
        'status': 'passed', 'native': 'reused saved audit', 'native_run': str(native_run),
        'dr_evt_checks': ['arrival constraints', 'exact runtimes', 'capacity',
                          'resource trace equals job schedule'],
    }
    report['retry_verification'] = {
        'first_chronological_divergence': None if first is None else {
            'job': first + 1, 'slurm_start': native[first]['start'],
            'dr_evt_start': observed[first]['start']},
        'job243': {'slurm_start': native[242]['start'],
                   'previous_dr_evt_start': old_schedule[242]['start'],
                   'fixed_dr_evt_start': observed[242]['start']} if len(native) >= 243 else None,
    }
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    (output / 'audit.json').write_text(json.dumps(report['audits'], indent=2) + '\n')
    repo = Path(__file__).resolve().parents[3]
    provenance = {
        'native_run': str(native_run), 'native_report_sha256': hashlib.sha256(report_path.read_bytes()).hexdigest(),
        'native_audit': native_audit, 'binary': str(binary),
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'simulation_source_sha256': hashlib.sha256((repo / 'src/sim/sim.cpp').read_bytes()).hexdigest(),
        'source_revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
        'timing_scope': 'DR_EVT subprocess and schedule parsing; native Slurm was not rerun',
    }
    for library in ('libdr_evt.dylib', 'libdr_evt.so'):
        path = binary.parent / library
        if path.exists():
            provenance[library + '_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    provenance['scheduler_source_sha256'] = {
        name: hashlib.sha256((repo / name).read_bytes()).hexdigest()
        for name in ('src/sim/scheduler_base.hpp',
                     'src/sim/scheduler_fcfs_periodic_hybrid_circular.hpp',
                     'src/sim/scheduler_fcfs_periodic_hybrid_circular.cpp',
                     'src/sim/scheduler_fcfs_hybrid_circular.tpp')}
    (output / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    (output / 'source.diff').write_text(subprocess.check_output(
        ['git', 'diff', 'HEAD', '--', 'src/sim'], cwd=repo, text=True))
    for name, c in report['resource_curves'].items():
        with (output / f'{name}-resource-curve.csv').open('w') as stream:
            writer = csv.writer(stream)
            writer.writerow(['time', 'allocated_nodes'])
            writer.writerows(c)
    shutil.copy2(Path(__file__), output / Path(__file__).name)
    shutil.copy2(Path(__file__).resolve().parents[1] / 'plot.py', output / 'plot.py')
    if not args.skip_plots:
        subprocess.run([sys.executable, str(output / 'plot.py'), str(output / 'report.json')], check=True)
    print(json.dumps({key: report[key] for key in (
        'different_start_times', 'max_start_difference_seconds',
        'resource_curve_difference_percent', 'retry_verification', 'dr_evt_metrics')}, indent=2))


if __name__ == '__main__':
    main()
