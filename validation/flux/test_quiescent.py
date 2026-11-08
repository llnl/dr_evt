"""Exercise the real scheduler RPC, including queued and asynchronous work."""
import json
from pathlib import Path
import subprocess

import flux
from flux.job import JobspecV1, submit

RESULTS = Path('/results')
handle = flux.Flux()
checks = []


def command(*args):
    return subprocess.check_output(['flux', *args], text=True).strip()


def barrier(label, expected):
    futures = [handle.rpc('sched.quiescent') for _ in range(3)]
    replies = [future.get() for future in futures]
    assert all(reply == {'status': 0, 'alloc_current': expected} for reply in replies), (
        label, expected, replies
    )
    stats = handle.rpc('sched-fluxion-qmanager.stats-get').get()
    running = sum(len(q['scheduled_queues']['running']) for q in stats['queues'].values())
    assert running == expected, (label, expected, stats)
    checks.append({'case': label, 'expected_allocations': expected,
                   'replies': replies, 'stats': stats})
    print(f'PASS {label}: allocations={expected}', flush=True)


def job(cores, duration=600):
    spec = JobspecV1.from_command(['true'], num_tasks=cores)
    spec.duration = duration
    spec.setattr('system.exec.test', {})
    return submit(handle, spec)


def wait(jobid, event):
    command('job', 'wait-event', '-t', '15', str(jobid), event)


def cleanup(ids):
    command('cancel', '--all')
    for jobid in ids:
        wait(jobid, 'clean')


# Use one broker with four cores. This probes synchronization independently
# of Fiction's four-node graph and virtual clock.
modules = handle.rpc('module.list').get()['mods']
names = {mod['name'] for mod in modules}
for name in ('sched-fluxion-qmanager', 'sched-fluxion-feasibility',
             'sched-fluxion-resource', 'sched-simple'):
    if name in names:
        command('module', 'remove', name)
resources = RESULTS / 'sync-resources.json'
resources.write_text(command('R', 'encode', '--ranks=0', '--cores=0-3') + '\n')
command('resource', 'reload', '--force', str(resources))
command('module', 'load', 'sched-fluxion-resource', 'prune-filters=ALL:core',
        'subsystems=containment', 'policy=low')

for policy in ('fcfs', 'easy', 'hybrid', 'conservative'):
    depth = 1 if policy == 'fcfs' else 100
    command('module', 'load', 'sched-fluxion-qmanager', f'queue-policy={policy}',
            f'queue-params=queue-depth={depth}')
    command('queue', 'start', '--all')
    barrier(f'{policy}: idle', 0)
    occupant = job(4)
    wait(occupant, 'depend')
    barrier(f'{policy}: initial allocation', 1)
    queued = [job(1) for _ in range(30)]
    for jobid in queued:
        wait(jobid, 'depend')
    barrier(f'{policy}: blocked batch remains pending', 1)
    command('cancel', str(occupant))
    wait(occupant, 'clean')
    barrier(f'{policy}: release drains placement work (depth={depth})', 4)
    cleanup(queued)
    barrier(f'{policy}: cancellation and resource release', 0)
    command('module', 'remove', 'sched-fluxion-qmanager')

command('module', 'load', 'sched-fluxion-qmanager', 'queue-policy=easy')
command('queue', 'start', '--all')
occupant = job(3)
wait(occupant, 'start')
head = job(4)
wait(head, 'depend')
borrower = job(1, 30)
wait(borrower, 'depend')
barrier('easy: reservation and safe backfill settle', 2)
cleanup([occupant, head, borrower])
barrier('easy: final idle', 0)
command('module', 'remove', 'sched-fluxion-qmanager')

RESULTS.joinpath('quiescence.json').write_text(json.dumps(checks, indent=2) + '\n')
print(f'{len(checks)} synchronization cases passed', flush=True)
