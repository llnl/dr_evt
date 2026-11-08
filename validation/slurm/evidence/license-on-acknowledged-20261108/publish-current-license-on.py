"""Publish the completed current-adapter license-on run and paired controls."""
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys

repo = Path('/Users/yeom2/work/dr_evt')
raw = repo / 'validation/slurm/results/licenses/license-on-acknowledged-20261108'
evidence = repo / 'validation/slurm/evidence/license-on-acknowledged-20261108'
previous = repo / 'validation/slurm/evidence/gated-lassen-20261108'
docs = repo / 'docs/_static/scheduler-validation/slurm'
spec = importlib.util.spec_from_file_location('publisher', repo / 'validation/slurm/licenses/publish_gated_results.py')
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)
spec = importlib.util.spec_from_file_location('summary', repo / 'validation/slurm/licenses/summarize_licenses.py')
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)
sys.path.insert(0, str(repo / 'validation/slurm'))
from compare import profile

evidence.mkdir(exist_ok=False)
on = publisher.archive_run(raw / 'paired/lassen-prefix2000-hybrid-r32-q64', evidence / 'on')
off = publisher.archive_run(raw / 'paired-license-off/lassen-prefix2000-hybrid-r32-q64', evidence / 'off')
for item in raw.iterdir():
    if item.is_file():
        publisher.copy_file(item, evidence / item.name)
summary.summarize(evidence / 'on', evidence / 'off', evidence)
native_performance = {
    'scope': on['timing_scope'], 'platform': 'Linux validation container',
    'batch_main_dispatch': True, 'main_dispatch_acknowledgement': True,
    'same_time_clock_monotonic': True, 'interventions': [],
    'on_seconds': on['slurm_metrics']['process_elapsed_seconds'],
    'off_seconds': off['slurm_metrics']['process_elapsed_seconds'],
    'on_evidence': 'on/',
    'off_evidence': 'off/',
    'note': 'On was rerun with the same native and adapter binaries as off; scheduler order remains uncontrolled.'}
(evidence / 'native-performance.json').write_text(json.dumps(native_performance, indent=2) + '\n')

protected = json.loads(json.dumps(off))
protected['dr_evt_schedule'] = off['protected_dr_evt_control']['schedule']
protected['dr_evt_metrics'] = off['protected_dr_evt_control']['metrics']
protected['dr_evt_label'] = 'DR_EVT (protected control)'
protected['diagnostic_no_reservations'] = False
protected['plot_note'] = 'Current acknowledged adapter; future license reservations disabled'
protected.pop('protected_dr_evt_control')
protected['resource_curves']['dr_evt'] = profile(protected['dr_evt_schedule'])
deltas = [a['start'] - b['start'] for a, b in zip(protected['slurm_schedule'], protected['dr_evt_schedule'])]
protected['different_start_times'] = sum(delta != 0 for delta in deltas)
protected['max_start_difference_seconds'] = max(map(abs, deltas))
curves = list(protected['resource_curves'].values())
times = sorted({t for curve in curves for t, _ in curve})
pointers = [0, 0]
area = 0
for begin, end in zip(times, times[1:]):
    for i, curve in enumerate(curves):
        while pointers[i] + 1 < len(curve) and curve[pointers[i] + 1][0] <= begin:
            pointers[i] += 1
    area += (end - begin) * abs(curves[0][pointers[0]][1] - curves[1][pointers[1]][1])
protected['resource_curve_difference_node_seconds'] = area
protected['resource_curve_difference_percent'] = 100 * area / (protected['capacity'] * times[-1])
control = evidence / 'protected-off-comparison'
control.mkdir()
for name in ('report.json', 'plot-input.json'):
    (control / name).write_text(json.dumps(protected, indent=2) + '\n')
for name, curve in protected['resource_curves'].items():
    with (control / f'{name}-resource-curve.csv').open('w', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(['time', 'allocated_node_equivalents'])
        writer.writerows(curve)
for name in ('provenance.json', 'audit.json', 'dr_evt-sources.json', 'jobs.csv', 'slurm.conf', 'workload.json'):
    publisher.copy_file(evidence / 'off' / name, control / name)
# The source audit covers the diagnostic schedule; independently check this
# protected schedule and trace against the same native evidence before publication.
for name in ('command.json', 'jobs.csv', 'schedule.csv', 'resources.csv'):
    publisher.copy_file(evidence / 'off/dr_evt_protected' / name, control / 'dr_evt' / name)
publisher.copy_file(evidence / 'off/snapshots.jsonl.gz', control / 'snapshots.jsonl.gz')
subprocess.run([sys.executable, str(repo / 'validation/slurm/licenses/audit_event.py'), str(control)], check=True)
shutil.copyfile(repo / 'validation/slurm/plot.py', control / 'plot.py')

# Regenerate the protected control and current on plots in the plotting container.
# Both saved scripts also regenerate their summary CSVs.
for target in ('on', 'off', 'protected-off-comparison'):
    subprocess.run(['docker', 'run', '--rm', '--mount', f'type=bind,src={evidence},dst=/evidence',
                    '--entrypoint', 'python3', 'dr-evt-slurm-validation',
                    f'/evidence/{target}/plot.py', f'/evidence/{target}/plot-input.json'], check=True)

# Replace current folders as a unit; keep older runs only in historical evidence.
for directory in (docs / 'gated-license-on', docs / 'dr_evt'):
    if directory.exists():
        shutil.rmtree(directory)
for name in ('source.diff.gz', 'run_performance.py'):
    (docs / name).unlink(missing_ok=True)
publisher.copy_doc_assets(evidence / 'on', docs)
publisher.copy_doc_assets(evidence / 'off', docs / 'bf-licenses-off')
publisher.copy_doc_assets(control, docs / 'bf-licenses-off/protected-comparison')
for name in ('license-planning-comparison.json', 'license-planning-metrics.csv',
             'summarize_licenses.py', 'native-performance.json'):
    publisher.copy_file(evidence / name, docs / name)
print(json.dumps({'license_on_differing_starts': on['different_start_times'],
                  'license_on_max_difference': on['max_start_difference_seconds'],
                  'license_on_curve_difference': on['resource_curve_difference_node_seconds'],
                  'license_on_snapshots': on['audits']['snapshots'],
                  'protected_off_differing_starts': protected['different_start_times'],
                  'protected_off_max_difference': protected['max_start_difference_seconds'],
                  'protected_off_curve_difference': area}, indent=2))
