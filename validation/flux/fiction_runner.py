"""Run reproducible policy cases through the real reference backend."""
import csv
from datetime import datetime, timedelta, timezone
import json
import os
from pathlib import Path
import signal
import subprocess
import uuid

EPOCH = 1700000000
CAPACITY = 4


def run_case(case, jobs, policy, queue_depth=100, reservation_depth=None, timeout=120,
             match_policy='lonodex'):
    """Return raw transition rows, ordered by input job index.

    Jobs are (submission offset, nodes, time limit, actual runtime).
    Each node has one core; jobs own complete nodes. Priorities are equal,
    and runtime estimates are the submitted limits.
    """
    case = Path(case)
    case.mkdir(parents=True, exist_ok=True)
    trace = case / 'trace.csv'
    with trace.open('w', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(['JobID', 'NNodes', 'NCPUS', 'Timelimit', 'Submit', 'Elapsed'])
        for jobid, (arrival, nodes, limit, actual) in enumerate(jobs):
            submitted = datetime.fromtimestamp(EPOCH + arrival, timezone.utc).strftime(
                '%Y-%m-%dT%H:%M:%S')
            writer.writerow([jobid, nodes, nodes, str(timedelta(seconds=limit)),
                             submitted, str(timedelta(seconds=actual))])
    queue = {'queue-policy': policy, 'queue-params': f'queue-depth={queue_depth}'}
    if reservation_depth is not None:
        queue['policy-params'] = f'reservation-depth={reservation_depth}'
    scheduler = case / 'scheduler.json'
    scheduler.write_text(json.dumps({
        'sched-fluxion-resource': {'match-policy': match_policy, 'subsystems': 'containment',
                                  'prune-filters': 'ALL:core'},
        'sched-fluxion-qmanager': queue,
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
    run_dir = case / ('run-' + uuid.uuid4().hex[:12])
    (case / 'run-directory.txt').write_text(str(run_dir) + '\n')
    with (case / 'run.log').open('w') as log:
        with subprocess.Popen(['flux-fiction-run', str(config), '--run-dir', str(run_dir)],
                              stdout=log, stderr=subprocess.STDOUT,
                              start_new_session=True) as process:
            try:
                status = process.wait(timeout=timeout)
            except BaseException:
                # A timed-out emulator can leave its broker and scheduling
                # daemons running. Terminate this run's entire process group.
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
                raise
            if status:
                raise subprocess.CalledProcessError(status, process.args)
    with (run_dir / 'output/job_transitions.csv').open() as stream:
        transitions = sorted(csv.DictReader(stream), key=lambda row: int(row['trace_idx']))
    if [int(row['trace_idx']) for row in transitions] != list(range(len(jobs))):
        raise AssertionError(('missing/duplicate reference transitions', transitions))
    return transitions
