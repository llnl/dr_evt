"""Smoke-test Fiction's real Flux backend and clock with hand-computed starts."""
import csv
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import subprocess

ROOT = Path('/results/fiction')
ROOT.mkdir(exist_ok=True)
EPOCH = 1700000000
cases = {
    'single': ([(0, 1, 20)], [0]),
    'backfill': ([(0, 3, 120), (6, 4, 20), (6, 1, 20)], [0, 120, 6]),
}
report = []
for name, (jobs, starts) in cases.items():
    case = ROOT / name
    case.mkdir(exist_ok=True)
    trace = case / 'trace.csv'
    with trace.open('w', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(['JobID', 'NNodes', 'NCPUS', 'Timelimit', 'Submit', 'Elapsed'])
        for jobid, (arrival, nodes, duration) in enumerate(jobs):
            walltime = str(timedelta(seconds=duration))
            submitted = datetime.fromtimestamp(EPOCH + arrival, timezone.utc).strftime(
                '%Y-%m-%dT%H:%M:%S')
            writer.writerow([jobid, nodes, nodes, walltime, submitted, walltime])
    scheduler = case / 'scheduler.json'
    scheduler.write_text(json.dumps({
        'sched-fluxion-resource': {'match-policy': 'lonodex', 'subsystems': 'containment',
                                  'prune-filters': 'ALL:core'},
        'sched-fluxion-qmanager': {'queue-policy': 'easy', 'queue-params': 'queue-depth=100'},
    }, indent=2) + '\n')
    config = case / 'config.toml'
    config.write_text('\n'.join([
        '[flux_fiction]', f'job_traces = {json.dumps(str(trace))}',
        f'config_json = {json.dumps(str(scheduler))}',
        'nnodes = 4', 'ncpus = 1', 'ngpus = 0', 'backend = "flux"',
        'exclusive = true', 'account_system_latency = false',
        'quiescent_accumulation_window = 0.0', 'batch_job_starts = true',
        'make_plots = false', 'jobtap_logging = true', 'quiet = true', '',
    ]))
    run_dir = case / 'run'
    with (case / 'run.log').open('w') as log:
        subprocess.run(['flux-fiction-run', str(config), '--run-dir', str(run_dir)],
                       check=True, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    with (run_dir / 'output/job_transitions.csv').open() as stream:
        transitions = sorted(csv.DictReader(stream), key=lambda row: int(row['trace_idx']))
    assert len(transitions) == len(jobs), (name, transitions)
    for row, (arrival, nodes, duration), start in zip(transitions, jobs, starts):
        actual = [float(row[key]) - EPOCH for key in ('SUBMIT', 'START', 'FINISH')]
        assert actual == [arrival, start, start + duration], (name, actual, start, row)
        assert int(row['nnodes']) == nodes, (name, row)
        assert len(row['NODELIST'].split(',')) == nodes, (name, row)
    report.append({'case': name, 'expected_start_offsets': starts, 'transitions': transitions})
    print(f'PASS Fiction {name}: start offsets={starts}', flush=True)
ROOT.joinpath('report.json').write_text(json.dumps(report, indent=2) + '\n')
