"""Archive audited gated runs under validation before copying documentation assets."""
import argparse
import copy
import csv
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compare import profile


def protected_off_report(off):
    """Compare protected DR_EVT with the current audited license-off schedule."""
    report = copy.deepcopy(off)
    control = report.pop('protected_dr_evt_control')
    report.update(dr_evt_schedule=control['schedule'], dr_evt_metrics=control['metrics'],
                  dr_evt_label='DR_EVT (protected control)', diagnostic_no_reservations=False,
                  plot_note='Current acknowledged adapter; future license reservations disabled')
    report['resource_curves']['dr_evt'] = profile(control['schedule'])
    deltas = [a['start'] - b['start'] for a, b in
              zip(report['slurm_schedule'], report['dr_evt_schedule'])]
    report['different_start_times'] = sum(value != 0 for value in deltas)
    report['max_start_difference_seconds'] = max(map(abs, deltas))
    curves = list(report['resource_curves'].values())
    times = sorted({time for curve in curves for time, _ in curve})
    pointers = [0, 0]
    area = 0
    for begin, end in zip(times, times[1:]):
        for i, curve in enumerate(curves):
            while pointers[i] + 1 < len(curve) and curve[pointers[i] + 1][0] <= begin:
                pointers[i] += 1
        area += (end - begin) * abs(curves[0][pointers[0]][1] - curves[1][pointers[1]][1])
    report['resource_curve_difference_node_seconds'] = area
    report['resource_curve_difference_percent'] = 100 * area / (report['capacity'] * times[-1])
    return report


def copy_file(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.name == 'snapshots.jsonl':
        # Preserve every frame and field, storing only changed job records.
        # The auditor reconstructs complete snapshots; raw-byte hashes identify
        # the original stream without committing gigabytes of repeated rows.
        jobs = {}
        digest = hashlib.sha256()
        destination = destination.with_name(destination.name + '.gz')
        with source.open('rb') as incoming, gzip.GzipFile(str(destination), 'wb', mtime=0) as outgoing:
            for line in incoming:
                digest.update(line)
                frame = json.loads(line)
                native = frame['native']
                rows = native.pop('jobs')
                present = {row['id'] for row in rows}
                updates = [row for row in rows if jobs.get(row['id']) != row]
                deleted = sorted(set(jobs) - present)
                for row in updates:
                    jobs[row['id']] = row
                for job_id in deleted:
                    jobs.pop(job_id)
                native.update(snapshot_encoding='job-deltas-v1', job_updates=updates)
                if deleted:
                    native['deleted_job_ids'] = deleted
                outgoing.write(json.dumps(frame, separators=(',', ':')).encode() + b'\n')
        destination.with_name('snapshots-original-sha256.txt').write_text(digest.hexdigest() + '\n')
    elif source.suffix == '.csv':
        destination.write_text(source.read_text())
    elif source.suffix == '.svg':
        destination.write_text('\n'.join(line.rstrip() for line in source.read_text().splitlines()) + '\n')
    elif source.suffix in ('.log', '.jsonl', '.diff'):
        destination = destination.with_name(destination.name + '.gz')
        with source.open('rb') as incoming, gzip.GzipFile(str(destination), 'wb', mtime=0) as outgoing:
            shutil.copyfileobj(incoming, outgoing)
    else:
        shutil.copyfile(source, destination)


def archive_run(source, destination):
    report = json.loads((source / 'report.json').read_text())
    assert json.loads((source / 'audit.json').read_text())['status'] == 'passed'
    assert report['batch_main_dispatch']
    for item in source.iterdir():
        if item.is_file() and item.name not in ('clock.bin', 'slurmctld.pid'):
            copy_file(item, destination / item.name)
        elif item.is_dir() and item.name in ('scripts', 'dr_evt', 'dr_evt_protected'):
            for child in item.iterdir():
                if child.is_file():
                    copy_file(child, destination / item.name / child.name)
    subprocess.run([sys.executable, str(Path(__file__).with_name('audit_event.py')), str(destination)],
                   check=True, stdout=subprocess.DEVNULL)
    return report


def copy_doc_assets(source, destination):
    """Keep full native logs and snapshots in validation, outside docs assets."""
    for item in source.iterdir():
        if item.is_file() and (item.suffix not in ('.gz', '.log', '.bin') or item.name.endswith('.diff.gz')):
            copy_file(item, destination / item.name)
        elif item.is_dir() and item.name != 'scripts':
            copy_doc_assets(item, destination / item.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results', type=Path)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--docs', type=Path, required=True)
    parser.add_argument('--local-performance', type=Path, required=True)
    parser.add_argument('--previous-results', type=Path,
                        help='Archive the earlier gated run that missed dispatch at time zero')
    args = parser.parse_args()
    evidence = args.evidence
    evidence.mkdir(parents=True, exist_ok=False)
    on = archive_run(args.results / 'paired/lassen-prefix2000-hybrid-r32-q64', evidence / 'on')
    off = archive_run(args.results / 'paired-license-off/lassen-prefix2000-hybrid-r32-q64', evidence / 'off')
    assert on['jobs'] == off['jobs']
    for item in (args.results / 'controls').iterdir():
        if item.is_dir():
            archive_run(item, evidence / 'controls' / item.name)
        elif item.is_file():
            copy_file(item, evidence / 'controls' / item.name)
    timeline = evidence / 'controls/plot_timeline.py'
    shutil.copyfile(Path(__file__).with_name('plot_timeline.py'), timeline)
    for case in ('pooled', 'protection'):
        subprocess.run([sys.executable, str(timeline), str(evidence / 'controls'),
                        '--case', case], check=True)
    for item in args.results.iterdir():
        if item.is_file():
            copy_file(item, evidence / item.name)
    for item in (args.results / 'ordering-diagnosis').glob('*'):
        if item.is_file():
            copy_file(item, evidence / 'ordering-diagnosis' / item.name)
    if args.previous_results:
        archive_run(args.previous_results / 'paired-license-off/lassen-prefix2000-hybrid-r32-q64',
                    evidence / 'missed-startup-dispatch')
        for item in (args.results / 'startup-diagnosis').glob('*'):
            if item.is_file():
                copy_file(item, evidence / 'startup-diagnosis' / item.name)
    for mode, target in (('protected', 'on'), ('diagnostic', 'off')):
        timing = args.local_performance / ('timing-' + mode)
        copy_file(timing / 'performance.json', evidence / target / 'performance.json')
        for item in timing.rglob('*'):
            if item.is_file():
                copy_file(item, evidence / 'local-performance' / mode / item.relative_to(timing))
    native_performance = {
        'scope': 'Controller startup, virtual node registration, scheduling, completion RPCs, observation and snapshots; excludes DR_EVT and plotting',
        'platform': 'Linux validation container', 'batch_main_dispatch': True,
        'interventions': [],
        'on_seconds': on['slurm_metrics']['process_elapsed_seconds'],
        'off_seconds': off['slurm_metrics']['process_elapsed_seconds']}
    (evidence / 'native-performance.json').write_text(json.dumps(native_performance, indent=2) + '\n')
    original = json.loads((Path(__file__).resolve().parents[1] / 'evidence/license-off-diagnosis/corrected-report.json').read_text())
    historical = evidence / 'original-protected-comparison'
    historical.mkdir()
    original['dr_evt_label'] = 'DR_EVT (protected)'
    original['plot_note'] = 'Historical ungated adapter run; protected DR_EVT control'
    for name in ('report.json', 'plot-input.json'):
        (historical / name).write_text(json.dumps(original, indent=2) + '\n')
    shutil.copyfile(Path(__file__).resolve().parents[1] / 'plot.py', historical / 'plot.py')
    subprocess.run([sys.executable, str(historical / 'plot.py'), str(historical / 'plot-input.json')], check=True)
    protected = evidence / 'protected-off-comparison'
    protected.mkdir()
    protected_report = protected_off_report(off)
    for name in ('report.json', 'plot-input.json'):
        (protected / name).write_text(json.dumps(protected_report, indent=2) + '\n')
    for name, curve in protected_report['resource_curves'].items():
        with (protected / f'{name}-resource-curve.csv').open('w') as stream:
            writer = csv.writer(stream)
            writer.writerow(['time', 'allocated_node_equivalents'])
            writer.writerows(curve)
    for name in ('provenance.json', 'dr_evt-sources.json', 'slurm.conf', 'jobs.csv',
                 'workload.json', 'snapshots.jsonl.gz'):
        copy_file(evidence / 'off' / name, protected / name)
    for source in (evidence / 'off/dr_evt_protected').iterdir():
        if source.is_file():
            copy_file(source, protected / 'dr_evt' / source.name)
    subprocess.run([sys.executable, str(Path(__file__).with_name('audit_event.py')), str(protected)],
                   check=True, stdout=subprocess.DEVNULL)
    shutil.copyfile(Path(__file__).resolve().parents[1] / 'plot.py', protected / 'plot.py')
    subprocess.run([sys.executable, str(protected / 'plot.py'), str(protected / 'plot-input.json')], check=True)
    data = {'jobs': off['jobs'], 'capacity': off['capacity'],
            'schedules': {'protected': off['protected_dr_evt_control']['schedule'],
                          'diagnostic': off['dr_evt_schedule'], 'native': off['slurm_schedule'],
                          'original_native': original['slurm_schedule']},
            'scope': 'Original ungated native run compared with fresh gated runs; reservation policy and event batching are separate controls'}
    (evidence / 'gated-diagnosis-data.json').write_text(json.dumps(data, indent=2) + '\n')
    scripts = Path(__file__).parent
    shutil.copyfile(scripts / 'plot_gated_diagnosis.py', evidence / 'plot_gated_diagnosis.py')
    shutil.copyfile(__file__, evidence / Path(__file__).name)
    subprocess.run([sys.executable, str(evidence / 'plot_gated_diagnosis.py'),
                    str(evidence / 'gated-diagnosis-data.json')], check=True)
    # Record the archive hashes after normalizing CSV newlines and SVG whitespace.
    for item in evidence.rglob('*.svg'):
        item.write_text('\n'.join(line.rstrip() for line in item.read_text().splitlines()) + '\n')
    hashes = {str(item.relative_to(evidence)): hashlib.sha256(item.read_bytes()).hexdigest()
              for item in sorted(evidence.rglob('*')) if item.is_file()}
    (evidence / 'archive-sha256.json').write_text(json.dumps(hashes, indent=2) + '\n')
    # Documentation is a copy of the archived, audited evidence.
    # The parent page always shows the current license-on run. Earlier runs
    # remain in validation evidence, not in the active comparison folders.
    for old in (args.docs / 'gated-license-on',
                args.docs / 'bf-licenses-off/original-protected-comparison'):
        if old.exists():
            shutil.rmtree(old)
    for source, destination in ((evidence / 'on', args.docs),
                                (evidence / 'off', args.docs / 'bf-licenses-off'),
                                (evidence / 'controls', args.docs / 'controls'),
                                (protected, args.docs / 'bf-licenses-off/protected-comparison')):
        copy_doc_assets(source, destination)
    for name in ('license-planning-comparison.json', 'license-planning-metrics.csv',
                 'summarize_licenses.py', 'dr_evt-sources.json', 'source.diff.gz'):
        source = evidence / name
        if source.exists():
            shutil.copyfile(source, args.docs / name)
        elif name == 'source.diff.gz':
            (args.docs / name).unlink(missing_ok=True)
    shutil.copyfile(evidence / 'native-performance.json', args.docs / 'native-performance.json')
    if (evidence / 'ordering-diagnosis').exists():
        copy_doc_assets(evidence / 'ordering-diagnosis', args.docs / 'divergence/gated-ordering')
    if (evidence / 'startup-diagnosis').exists():
        copy_doc_assets(evidence / 'startup-diagnosis', args.docs / 'divergence/startup-dispatch')
    if (evidence / 'source.diff.gz').exists():
        shutil.copyfile(evidence / 'source.diff.gz', args.docs / 'bf-licenses-off/source.diff.gz')
    else:
        (args.docs / 'bf-licenses-off/source.diff.gz').unlink(missing_ok=True)
    shutil.copyfile(evidence / 'on/performance.json', args.docs / 'performance.json')
    shutil.copyfile(Path(__file__).with_name('measure_dr_evt.py'), args.docs / 'measure_dr_evt.py')
    for item in evidence.glob('*diagnosis*'):
        if item.is_file():
            copy_file(item, args.docs / 'divergence' / item.name)
    print(json.dumps({'on_differing_starts': on['different_start_times'],
                      'off_diagnostic_differing_starts': off['different_start_times'],
                      'off_protected_differing_starts': off['protected_dr_evt_control']['different_start_times'],
                      'evidence': str(evidence)}, indent=2))


if __name__ == '__main__':
    main()
