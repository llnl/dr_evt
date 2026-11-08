"""Refresh DR_EVT references against an unchanged audited native license-off run."""
import json
from pathlib import Path
import shutil
import sys

sys.path.insert(0, '/dr_evt_source/validation/slurm')
from compare import metrics, profile, run_dr_evt

original = Path('/dr_evt_source/validation/slurm/evidence/gated-lassen-20261108/off')
directory = Path('/results/paired-license-off/lassen-prefix2000-hybrid-r32-q64')
shutil.copytree(original, directory)
report = json.loads((directory / 'report.json').read_text())
simulator = Path('/workspace/dr_evt-simulator').read_text().strip()
source = json.loads(Path('/results/dr_evt-sources.json').read_text())
for mode, name in ((True, 'dr_evt'), (False, 'dr_evt_protected')):
    shutil.rmtree(directory / name)
    schedule, elapsed = run_dr_evt(directory / name, report['jobs'], simulator,
                                   report['capacity'], 30, 32, 64, 1, 259200,
                                   diagnostic_no_reservations=mode)
    old_schedule = (report['dr_evt_schedule'] if mode else
                    report['protected_dr_evt_control']['schedule'])
    assert schedule == old_schedule, 'Current DR_EVT differs from the saved reference'
    values = metrics(report['jobs'], schedule, report['capacity'], elapsed)
    if mode:
        report['dr_evt_schedule'] = schedule
        report['dr_evt_metrics'] = values
        report['resource_curves']['dr_evt'] = profile(schedule)
    else:
        report['protected_dr_evt_control']['schedule'] = schedule
        report['protected_dr_evt_control']['metrics'] = values
report['native_record_reused_from'] = 'validation/slurm/evidence/gated-lassen-20261108/off'
report['dr_evt_reference_revision'] = source['revision']
report['dr_evt_reference_binary_sha256'] = source['binary_sha256']
assert report['different_start_times'] == 0
for name in ('report.json', 'plot-input.json'):
    (directory / name).write_text(json.dumps(report, indent=2) + '\n')
shutil.copyfile(directory / 'dr_evt-sources.json', directory / 'original-dr_evt-sources.json')
shutil.copyfile('/results/dr_evt-sources.json', directory / 'dr_evt-sources.json')
provenance = json.loads((directory / 'provenance.json').read_text())
for key in ('native_record_reused_from', 'dr_evt_reference_revision', 'dr_evt_reference_binary_sha256'):
    provenance[key] = report[key]
(directory / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
shutil.copyfile(__file__, directory / 'scripts/refresh_reference.py')
shutil.copyfile('/dr_evt_source/validation/slurm/plot.py', directory / 'plot.py')
print('Current diagnostic and protected DR_EVT reproduce both saved license-off references.', flush=True)
