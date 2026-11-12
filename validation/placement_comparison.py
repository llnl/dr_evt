"""Run and audit DR_EVT's exact-placement validation driver.

This module is shared by native scheduler harnesses.  It sends a normalized
whole-node workload to ``test_flux_placement_driver`` (the driver exercises the
main Simulation implementation despite its historical name), captures exact
allocations and reservation snapshots, and returns a scheduler-neutral record.
Inputs are ``(submit, nodes, limit, runtime)`` tuples.  Outputs contain one
schedule entry per input job plus the driver's transition snapshots.
"""
import json
from pathlib import Path
import subprocess
import time


def audit_schedule(jobs, schedule, capacity):
    """Audit exact exclusive-node ownership and return normalized allocations."""
    if len(jobs) != len(schedule):
        raise AssertionError('placement schedule job count differs from input')
    intervals = {node: [] for node in range(capacity)}
    normalized = []
    for index, (job, record) in enumerate(zip(jobs, schedule)):
        submit, quantity, _, runtime = job
        nodes = sorted(record['node_ids'])
        if (record['job'] != index or record['start'] < submit or
                record['finish'] - record['start'] != runtime or
                len(nodes) != quantity or len(set(nodes)) != quantity or
                any(type(node) is not int or not 0 <= node < capacity for node in nodes)):
            raise AssertionError(('invalid exact placement', index, record))
        for node in nodes:
            for start, finish in intervals[node]:
                if record['start'] < finish and start < record['finish']:
                    raise AssertionError(('overlapping exact placement', node, index))
            intervals[node].append((record['start'], record['finish']))
        normalized.append({'start': record['start'], 'end': record['finish'],
                           'nodes': quantity, 'node_ids': nodes})
    return normalized


def run_dr_evt_placement(directory, jobs, driver, capacity, policy='hybrid',
                         reservations=32, order='low', timeout=900):
    """Execute one placement run and retain its command protocol and output."""
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=False)
    if policy not in ('none', 'easy', 'hybrid', 'conservative'):
        raise ValueError('unsupported placement policy: ' + policy)
    if order not in ('low', 'high'):
        raise ValueError('placement order must be low or high')
    slots = {'none': 0, 'easy': 1, 'hybrid': reservations,
             'conservative': len(jobs)}[policy]
    strict = int(policy == 'none')
    request = f'{capacity} {slots} {strict} {order} {len(jobs)}\n'
    request += ''.join(' '.join(map(str, job)) + '\n' for job in jobs)
    command = [str(Path(driver).resolve()), '--exact-only']
    (directory / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    (directory / 'request.txt').write_text(request)
    started = time.perf_counter()
    result = subprocess.run(command, input=request, text=True, capture_output=True,
                            timeout=timeout)
    elapsed = time.perf_counter() - started
    (directory / 'stdout.jsonl').write_text(result.stdout)
    (directory / 'stderr.log').write_text(result.stderr)
    if result.returncode:
        raise RuntimeError(f'placement driver exited {result.returncode}: {result.stderr}')
    records = [json.loads(line) for line in result.stdout.splitlines()]
    schedule = sorted((row['schedule'] for row in records if 'schedule' in row),
                      key=lambda row: row['job'])
    snapshots = [row['snapshot'] for row in records if 'snapshot' in row]
    normalized = audit_schedule(jobs, schedule, capacity)
    report = {'resource_model': 'placement', 'policy': policy,
              'reservation_depth': slots, 'placement_order': order,
              'elapsed_seconds': elapsed, 'schedule': normalized,
              'snapshots': snapshots}
    (directory / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return normalized, snapshots, elapsed
