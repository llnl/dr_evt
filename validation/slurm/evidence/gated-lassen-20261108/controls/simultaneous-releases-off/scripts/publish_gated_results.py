"""Archive audited gated runs under validation before copying documentation assets."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


def copy_file(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.suffix == '.csv':
        destination.write_text(source.read_text())
    elif source.suffix == '.svg':
        destination.write_text('\n'.join(line.rstrip() for line in source.read_text().splitlines()) + '\n')
    elif source.suffix in ('.log', '.jsonl'):
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
    return report


def copy_doc_assets(source, destination):
    """Keep full native logs and snapshots in validation, outside docs assets."""
    for item in source.iterdir():
        if item.is_file() and item.suffix not in ('.gz', '.log', '.bin'):
            copy_file(item, destination / item.name)
        elif item.is_dir() and item.name != 'scripts':
            copy_doc_assets(item, destination / item.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results', type=Path)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--docs', type=Path, required=True)
    parser.add_argument('--local-performance', type=Path, required=True)
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
    for item in (args.results / 'ordering-diagnosis').iterdir():
        if item.is_file():
            copy_file(item, evidence / 'ordering-diagnosis' / item.name)
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
    # Retain the original exact license-on comparison on the parent page.
    # Gated license-on is a separate ordering control, with its own provenance.
    for source, destination in ((evidence / 'on', args.docs / 'gated-license-on'),
                                (evidence / 'off', args.docs / 'bf-licenses-off'),
                                (evidence / 'controls', args.docs / 'controls')):
        copy_doc_assets(source, destination)
    for name in ('license-planning-comparison.json', 'license-planning-metrics.csv',
                 'summarize_licenses.py', 'dr_evt-sources.json', 'source.diff'):
        shutil.copyfile(evidence / name, args.docs / name)
    shutil.copyfile(evidence / 'native-performance.json', args.docs / 'gated-license-on/native-performance.json')
    copy_doc_assets(evidence / 'ordering-diagnosis', args.docs / 'divergence/gated-ordering')
    shutil.copyfile(evidence / 'source.diff', args.docs / 'bf-licenses-off/source.diff')
    shutil.copyfile(evidence / 'on/performance.json', args.docs / 'performance.json')
    shutil.copyfile(Path(__file__).with_name('measure_dr_evt.py'), args.docs / 'measure_dr_evt.py')
    for item in evidence.glob('*diagnosis*'):
        shutil.copyfile(item, args.docs / 'divergence' / item.name)
    shutil.copytree(historical, args.docs / 'bf-licenses-off/original-protected-comparison', dirs_exist_ok=True)
    print(json.dumps({'on_differing_starts': on['different_start_times'],
                      'off_diagnostic_differing_starts': off['different_start_times'],
                      'off_protected_differing_starts': off['protected_dr_evt_control']['different_start_times'],
                      'evidence': str(evidence)}, indent=2))


if __name__ == '__main__':
    main()
