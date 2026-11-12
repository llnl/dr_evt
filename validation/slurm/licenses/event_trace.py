"""Run native Slurm allocation scheduling with trace-driven time and completions."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compare import load_lassen, metrics, profile, run_dr_evt
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from placement_comparison import audit_schedule, run_dr_evt_placement

PREFIX = Path('/workspace/slurm-modern-install')
EVENT = Path('/workspace/slurm-event')
CLIENT = EVENT / ('client_state' if os.environ.get('SLURM_LICENSE_STATE_ONLY') == '1' else 'client')
EPOCH = 1597836579


def expand_nodes(expression):
    """Expand the n0001/n[0001-0003,0005] forms used by this harness."""
    import re
    if not expression:
        return []
    if '[' not in expression:
        match = re.fullmatch(r'n(\d+)', expression)
        if not match:
            raise ValueError('unexpected Slurm node list: ' + expression)
        return [int(match.group(1))]
    match = re.fullmatch(r'n\[([^]]+)\]', expression)
    if not match:
        raise ValueError('unexpected Slurm node list: ' + expression)
    result = []
    for part in match.group(1).split(','):
        if '-' in part:
            first, last = map(int, part.split('-', 1))
            if first > last:
                raise ValueError('reversed Slurm node range: ' + part)
            result.extend(range(first, last + 1))
        else:
            result.append(int(part))
    result.sort()
    if len(result) != len(set(result)):
        raise ValueError('duplicate Slurm node in list: ' + expression)
    return result


def run_case(directory, jobs, capacity, enabled=True, batch_main_dispatch=False,
             resource_model='capacity'):
    """Run native scheduling with independently controlled license planning and batching.

    batch_main_dispatch defers submission-time allocation and gates main dispatch
    until all equal-time arrival/completion RPCs finish, matching DR_EVT's event
    batching for a known trace. DR_EVT makes same-time arrivals eligible before
    dispatch following completions; arrivals appended later by a streaming caller
    form a separate cycle. This option does not change bf_licenses or reservations.
    Processing arrivals queues them without starting them. Releasing the gates
    does not force Slurm main dispatch to precede its independently triggered backfill.
    """
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'state').mkdir()
    clock_file = directory / 'clock.bin'
    clock_file.write_bytes(struct.pack('qQI', EPOCH - 120, 0, 0) + bytes(12))
    params = ('sched_min_interval=0,batch_sched_delay=0,sched_interval=60,'
              'bf_interval=30,bf_resolution=1,bf_window=4320,bf_max_job_test=4096,'
              'bf_max_job_part=64,bf_job_part_count_reserve=32,default_queue_depth=100000')
    if enabled:
        params += ',bf_licenses'
    if batch_main_dispatch:
        # Queue submissions until the complete equal-time event batch is applied.
        params += ',defer'
    config = directory / 'slurm.conf'
    placement = resource_model == 'placement'
    if resource_model not in ('capacity', 'placement'):
        raise ValueError('resource model must be capacity or placement')
    licenses = '' if placement else f'Licenses=capacity:{capacity}\n'
    node_count = capacity if placement else len(jobs)
    config.write_text(f'''ClusterName=licenseevents
SlurmctldHost=localhost
SlurmctldPort=17900
SlurmUser=root
AuthType=auth/none
CredType=cred/none
PluginDir={EVENT}:{PREFIX}/lib/slurm
StateSaveLocation={directory}/state
SlurmctldPidFile={directory}/slurmctld.pid
SlurmctldLogFile={directory}/controller.log
SlurmctldDebug=info
DebugFlags=Backfill
SchedulerType=sched/backfill
SchedulerParameters={params}
SelectType=select/cons_tres
SelectTypeParameters=CR_CPU
PriorityType=priority/basic
ProctrackType=proctrack/linuxproc
TaskPlugin=task/none
JobAcctGatherType=jobacct_gather/none
JobCompType=jobcomp/filetxt
JobCompLoc={directory}/jobcomp.log
JobRequeue=0
OverTimeLimit=1
MinJobAge=1000000
SlurmdTimeout=0
SlurmctldTimeout=0
MessageTimeout=10
MailProg=/bin/true
{licenses}NodeName=n[0000-{node_count-1:04d}] NodeHostname=localhost NodeAddr=127.0.0.1 Port=18000 CPUs=1 RealMemory=128 State=IDLE
PartitionName=pbatch Nodes=ALL Default=YES MaxTime=INFINITE State=UP OverSubscribe=EXCLUSIVE
''')
    with (directory / 'jobs.csv').open('w') as stream:
        writer = csv.writer(stream)
        writer.writerow(['job_submit_time', 'num_nodes', 'time_limit', 'duration'])
        writer.writerows(jobs)
    scripts = directory / 'scripts'
    scripts.mkdir()
    hashes = {}
    for source in Path(__file__).parent.iterdir():
        if source.suffix in ('.py', '.c', '.sh'):
            data = source.read_bytes()
            (scripts / source.name).write_bytes(data)
            hashes[source.name] = hashlib.sha256(data).hexdigest()
    comparison = Path(__file__).resolve().parents[1] / 'compare.py'
    shutil.copyfile(comparison, scripts / 'slurm_compare.py')
    hashes['slurm_compare.py'] = hashlib.sha256(comparison.read_bytes()).hexdigest()
    for name in ('slurm-source.json', 'slurm-revision.txt', 'slurm-version.txt',
                 'dr_evt-sources.json', 'event-overlay.json', 'dispatch-overlay.json'):
        source = Path('/results') / name
        if source.exists():
            shutil.copyfile(source, directory / name)
    manifest = {'scheduler': 'Slurm 26.11.0-0rc2, select/cons_tres',
                'resource_model': resource_model,
                'bf_licenses': enabled,
                'clock': 'virtual integer seconds; monotonic/RPC time remains real',
                'compute_adapter': 'native allocation requests, node registration, '
                                   'completion and epilog RPCs; no actual compute daemons',
                'backfill_hook': 'serialize event batches with backfill; native placement and reservation code retained',
                'scripts_sha256': hashes,
                'binary_sha256': {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                                  for path in (PREFIX / 'sbin/slurmctld', EVENT / 'sched_backfill.so',
                                               CLIENT, EVENT / 'libvalidation_clock.so')},
                'observer': 'job-state RPC with details on transitions' if CLIENT.name == 'client_state' else 'full job-info RPC'}
    manifest['dr_evt_scheduler'] = (
        'Simulation exact-placement hybrid scheduler' if placement else
        ('FCFSPeriodicHybridCircularScheduler' if enabled
         else 'FCFSPeriodicNoReservationCircularScheduler'))
    manifest['diagnostic_no_reservations'] = not enabled
    manifest['main_dispatch_acknowledgement'] = batch_main_dispatch
    manifest['same_time_clock_monotonic'] = True
    manifest['main_dispatch'] = ('deferred submissions and dispatch after complete equal-time RPC batches'
                                 if batch_main_dispatch else 'native dispatch may run between individual event RPCs')
    (directory / 'provenance.json').write_text(json.dumps(manifest, indent=2) + '\n')
    env = dict(os.environ, SLURM_CONF=str(config), SLURM_VALIDATION_CLOCK_FILE=str(clock_file),
               LD_PRELOAD=str(EVENT / 'libvalidation_clock.so'),
               SLURM_VALIDATION_RESOURCE_MODEL=resource_model,
               SLURM_VALIDATION_BATCH_DISPATCH='1' if batch_main_dispatch else '0')
    log = (directory / 'daemon.log').open('w')
    started = time.monotonic()
    daemon = subprocess.Popen([str(PREFIX / 'sbin/slurmctld'), '-D', '-f', str(config)],
                              env=env, stdout=log, stderr=subprocess.STDOUT)
    client = None
    client_log = (directory / 'client.log').open('w')
    try:
        for _ in range(100):
            if daemon.poll() is not None:
                raise RuntimeError('Controller exited; see daemon.log')
            ping = subprocess.run([str(PREFIX / 'bin/scontrol'), 'ping'], env=env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if ping.returncode == 0:
                break
            time.sleep(.02)
        client = subprocess.Popen([str(CLIENT), str(config)], env=env,
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=client_log, text=True, bufsize=1)

        def command(text):
            client.stdin.write(text + '\n')
            client.stdin.flush()
            reply = client.stdout.readline()
            if not reply:
                raise RuntimeError('Native API client exited; see client.log')
            return json.loads(reply)

        # Finish the controller's mandatory startup probe before registering
        # virtual nodes. Subsequent probes are disabled by SlurmdTimeout=0.
        command(f'TIME {EPOCH}')
        time.sleep(1)
        # Register each virtual node through the same RPC as a compute daemon.
        for index in range(node_count):
            command(f'REGISTER n{index:04d}')
        command('RELEASE')

        ids, schedule, completed = {}, {}, set()
        cursor, now, frames, idle_since = 0, 0, 0, None
        snapshot_stream = (directory / 'snapshots.jsonl').open('w')
        while len(completed) < len(jobs):
            # TIME holds the backfill gate and, with batch_main_dispatch, the
            # main-dispatch gate. RELEASE opens them after every event at now.
            command(f'TIME {EPOCH + now}')
            dispatch_before = command('DISPATCH')['generation'] if batch_main_dispatch else 0
            state_changed = False
            for index, row in list(schedule.items()):
                if index not in completed and row['end'] == now:
                    command(f'COMPLETE {ids[index]} {row["assignment"]}')
                    completed.add(index)
                    state_changed = True
            while cursor < len(jobs) and jobs[cursor][0] <= now:
                _, tokens, limit, _ = jobs[cursor]
                ids[cursor] = command(f'SUBMIT {tokens} {limit // 60} job{cursor:04d}')['id']
                cursor += 1
                state_changed = True
            command('RELEASE')
            # Observe a stable native state while virtual time remains fixed.
            previous, stable = None, 0
            deadline = time.monotonic() + 10
            dispatch_after = dispatch_before
            # RPC responses can precede the queued scheduler pass. Unchanged
            # snapshots alone do not acknowledge dispatch, especially at t=0.
            while stable < 3 or (batch_main_dispatch and state_changed and
                                 dispatch_after <= dispatch_before):
                time.sleep(.01)
                snapshot = command('DUMP')
                if batch_main_dispatch:
                    dispatch_after = command('DISPATCH')['generation']
                signature = [(row['id'], row['state'], row['start']) for row in snapshot['jobs']]
                stable = stable + 1 if signature == previous else 0
                previous = signature
                if time.monotonic() >= deadline:
                    raise TimeoutError('Native scheduling did not settle')
            by_id = {value: key for key, value in ids.items()}
            allocated = 0
            owned_nodes = set()
            for row in snapshot['jobs']:
                index = by_id[row['id']]
                state = row['state'] & 255
                assert state in (0, 1, 3), row  # pending, running, completed
                if state == 1:
                    allocated += jobs[index][1]
                    assigned = expand_nodes(row['assignment']) if placement else [index]
                    assert len(assigned) == jobs[index][1] if placement else row['assignment'] == f'n{index:04d}'
                    assert not owned_nodes.intersection(assigned)
                    owned_nodes.update(assigned)
                    if index not in schedule:
                        begin = row['start'] - EPOCH
                        assert begin == now, (now, row)
                        schedule[index] = {'start': begin, 'end': begin + jobs[index][3],
                                           'nodes': jobs[index][1], 'assignment': row['assignment']}
                if index in completed:
                    assert state == 3 and row['end'] == EPOCH + schedule[index]['end'], row
            assert allocated <= capacity
            if placement:
                assert snapshot['license_total'] == snapshot['license_used'] == 0
            else:
                assert snapshot['license_total'] == capacity and snapshot['license_used'] == allocated
            if not enabled and not placement:
                assert not any(row['state'] & 255 == 0 and
                               int(row['licenses'].split(':')[1]) <= capacity - allocated
                               for row in snapshot['jobs']), ('Fitting job left waiting after dispatch', now)
            if allocated == 0 and cursor == len(jobs) and len(completed) < len(jobs):
                idle_since = now if idle_since is None else idle_since
                assert now - idle_since <= 60, ('Idle pool with pending jobs', snapshot)
            else:
                idle_since = None
            snapshot_stream.write(json.dumps({'time': now, 'allocated_node_equivalents': allocated,
                                               'main_dispatch_ack_required': batch_main_dispatch and state_changed,
                                               'main_dispatch_generation_before': dispatch_before,
                                               'main_dispatch_generation_after': dispatch_after,
                                               'native': snapshot}) + '\n')
            snapshot_stream.flush()
            frames += 1
            if frames % 100 == 0:
                print(directory.name, 'time', now, 'submitted', cursor, 'completed', len(completed), flush=True)
            if len(completed) == len(jobs):
                break
            future = [((now // 30) + 1) * 30]
            if cursor < len(jobs):
                future.append(jobs[cursor][0])
            future.extend(row['end'] for i, row in schedule.items() if i not in completed)
            now = min(t for t in future if t > now)
            if now > 1000000:
                raise TimeoutError('Simulation made no scheduling progress')
        snapshot_stream.close()
        native_elapsed = time.monotonic() - started
        native = [schedule[i] for i in range(len(jobs))]
        if placement:
            dr, _, dr_elapsed = run_dr_evt_placement(
                directory / 'dr_evt', jobs,
                Path('/workspace/dr_evt-placement-driver').read_text().strip(),
                capacity, 'hybrid', 32, 'low')
        else:
            dr, dr_elapsed = run_dr_evt(directory / 'dr_evt', jobs,
                                        Path('/workspace/dr_evt-simulator').read_text().strip(),
                                        capacity, 30, 32, 64, 1, 259200,
                                        diagnostic_no_reservations=not enabled)
        protected_control = None
        if not enabled and len(jobs) > 4:
            protected, protected_elapsed = run_dr_evt(
                directory / 'dr_evt_protected', jobs,
                Path('/workspace/dr_evt-simulator').read_text().strip(),
                capacity, 30, 32, 64, 1, 259200)
            protected_control = {
                'label': 'DR_EVT (protected control)', 'schedule': protected,
                'metrics': metrics(jobs, protected, capacity, protected_elapsed),
                'different_start_times': sum(a['start'] != b['start'] for a, b in zip(native, protected))}
        allocation_differences = []
        if placement:
            native_allocations = [expand_nodes(row['assignment']) for row in native]
            audit_schedule(jobs, [{'job': i, 'start': row['start'], 'finish': row['end'],
                                   'node_ids': nodes}
                                  for i, (row, nodes) in enumerate(zip(native, native_allocations))],
                           capacity)
            allocation_differences = [
                {'job_id': i + 1, 'slurm': native_nodes, 'dr_evt': dr_row['node_ids']}
                for i, (native_nodes, dr_row) in enumerate(zip(native_allocations, dr))
                if native_nodes != dr_row['node_ids']]
        report = {'jobs': jobs, 'capacity': capacity, 'resource_model': resource_model,
                  'slurm_schedule': native, 'dr_evt_schedule': dr,
                  'slurm_label': ('Slurm (exclusive whole-node placement)' if placement else
                                  f'Slurm (license pool, bf_licenses {"on" if enabled else "off"})'),
                  'capacity_label': 'node equivalents',
                  'dr_evt_label': 'DR_EVT (protected)' if enabled else 'DR_EVT (no-reservation diagnostic)',
                  'diagnostic_no_reservations': not enabled,
                  'batch_main_dispatch': batch_main_dispatch,
                  'main_dispatch_acknowledgement': batch_main_dispatch,
                  'reservation_depth': 32,
                  'backfill_queue_depth': None if placement else 64,
                  'backfill_interval_seconds': 0 if placement else 30,
                  'comparison_loop_note': ('DR_EVT placement is event-driven with unlimited examination; '
                                           'native Slurm retains periodic backfill' if placement else None),
                  'plot_note': 'Native scheduling; trace-driven virtual time and completion RPCs',
                  'bf_licenses': enabled, 'resource_curves': {'slurm': profile(native), 'dr_evt': profile(dr)},
                  'slurm_metrics': metrics(jobs, native, capacity, native_elapsed),
                  'dr_evt_metrics': metrics(jobs, dr, capacity, dr_elapsed),
                  'different_start_times': sum(a['start'] != b['start'] for a, b in zip(native, dr)),
                  'exact_allocation_match': not allocation_differences,
                  'different_allocations': len(allocation_differences),
                  'allocation_differences': allocation_differences,
                  'audits': {
                      'runtime_and_capacity': 'passed',
                      'private_node_assignments': 'not_applicable' if placement else 'passed',
                      'exact_node_ownership': 'passed' if placement else 'not_applicable',
                      'completed_jobs': len(completed), 'snapshots': frames},
                  'clock': ('integer virtual seconds; main dispatch and backfill gated during equal-time RPC batches'
                            if batch_main_dispatch else 'integer virtual seconds; main dispatch may run between RPCs')}
        if protected_control is not None:
            report['protected_dr_evt_control'] = protected_control
        report['timing_scope'] = ('Native controller startup, virtual node registration, scheduling, '
                                  'completion RPCs, state observation and snapshot writes; excludes DR_EVT and plotting')
        times = sorted({t for curve in report['resource_curves'].values() for t, _ in curve})
        differences = [a['start'] - b['start'] for a, b in zip(native, dr)]
        report['max_start_difference_seconds'] = max(map(abs, differences))
        pointers = [0, 0]
        curves = [report['resource_curves'][name] for name in ('slurm', 'dr_evt')]
        difference_area = 0
        for begin, end in zip(times, times[1:]):
            for i, curve in enumerate(curves):
                while pointers[i] + 1 < len(curve) and curve[pointers[i] + 1][0] <= begin:
                    pointers[i] += 1
            difference_area += (end - begin) * abs(curves[0][pointers[0]][1] - curves[1][pointers[1]][1])
        report['resource_curve_difference_node_seconds'] = difference_area
        report['resource_curve_difference_percent'] = 100 * difference_area / (capacity * times[-1])
        for name in ('report.json', 'plot-input.json'):
            (directory / name).write_text(json.dumps(report, indent=2) + '\n')
        for scheduler, curve in report['resource_curves'].items():
            with (directory / f'{scheduler}-resource-curve.csv').open('w') as stream:
                writer = csv.writer(stream)
                writer.writerow(['time', 'allocated_node_equivalents'])
                writer.writerows(curve)
        if len(jobs) > 4:
            script = Path(__file__).resolve().parents[1] / 'plot.py'
            shutil.copyfile(script, directory / 'plot.py')
            subprocess.run([sys.executable, str(directory / 'plot.py'), str(directory / 'plot-input.json')], check=True)
        print(directory.name, 'starts', [row['start'] for row in native[:4]],
              'different jobs', report['different_start_times'], flush=True)
        return report
    finally:
        if client:
            client.terminate()
            client.wait(timeout=10)
        daemon.terminate()
        try:
            daemon.wait(timeout=10)
        except subprocess.TimeoutExpired:
            daemon.kill()
            daemon.wait()
        log.close()
        client_log.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--outdir', type=Path, default=Path('/results/event-controls'))
    parser.add_argument('--trace', type=Path)
    parser.add_argument('--batch-main-dispatch', action='store_true',
                        help='Defer submissions and gate main dispatch until the equal-time RPC batch is complete')
    parser.add_argument('--bf-licenses', choices=('on', 'off'), default='on',
                        help='License reservation planning for the Lassen run; controls exercise both settings')
    parser.add_argument('--resource-model', choices=('capacity', 'placement'), default='capacity')
    args = parser.parse_args()
    if args.trace:
        jobs, provenance = load_lassen(args.trace, 2000, 756)
        directory = args.outdir / 'lassen-prefix2000-hybrid-r32-q64'
        directory.mkdir(parents=True, exist_ok=True)
        (directory / 'workload.json').write_text(json.dumps(provenance, indent=2) + '\n')
        report = run_case(directory, jobs, 756, enabled=args.bf_licenses == 'on',
                          batch_main_dispatch=args.batch_main_dispatch,
                          resource_model=args.resource_model)
    else:
        cases = [
            ('four-job-pooled', [(0, 2, 120, 120), (0, 2, 180, 180),
                                 (0, 3, 60, 60), (0, 1, 120, 120)], True, [0, 0, 180, 120]),
            ('four-job-pooled-off', [(0, 2, 120, 120), (0, 2, 180, 180),
                                     (0, 3, 60, 60), (0, 1, 120, 120)], False, [0, 0, 180, 120]),
            ('reservation-protection-on', [(0, 2, 120, 120), (0, 2, 180, 180),
                                           (0, 4, 60, 60), (0, 1, 120, 120)], True, [0, 0, 180, 240]),
            ('reservation-protection-off', [(0, 2, 120, 120), (0, 2, 180, 180),
                                            (0, 4, 60, 60), (0, 1, 120, 120)], False, [0, 0, 240, 120]),
            ('periodic-backfill', [(0, 3, 120, 120), (1, 4, 60, 60),
                                   (1, 1, 60, 60)], True, [0, 120, 30]),
            ('early-completion', [(0, 4, 120, 7), (1, 4, 60, 3)], True, [0, 7]),
        ]
        reports = []
        for name, jobs, enabled, expected in cases:
            report = run_case(args.outdir / name, jobs, 4, enabled,
                              batch_main_dispatch=args.batch_main_dispatch)
            assert [row['start'] for row in report['slurm_schedule']] == expected
            assert [row['start'] for row in report['dr_evt_schedule']] == expected
            reports.append(report)
        if args.batch_main_dispatch:
            # Both releases must be visible before a later one-license job starts.
            jobs = [(0, 1, 120, 120), (0, 1, 120, 120),
                    (1, 2, 60, 60), (1, 1, 60, 60), (1, 1, 60, 60)]
            for enabled in (True, False):
                report = run_case(args.outdir / ('simultaneous-releases-' + ('on' if enabled else 'off')),
                                  jobs, 2, enabled, batch_main_dispatch=True)
                assert [row['start'] for row in report['slurm_schedule']] == [0, 0, 120, 180, 180]
                assert [row['start'] for row in report['dr_evt_schedule']] == [0, 0, 120, 180, 180]
                reports.append(report)
        (args.outdir / 'controls.json').write_text(json.dumps(reports, indent=2) + '\n')
        for case in ('pooled', 'protection'):
            subprocess.run([sys.executable, str(Path(__file__).with_name('plot_timeline.py')),
                            str(args.outdir), '--case', case], check=True)


if __name__ == '__main__':
    main()
