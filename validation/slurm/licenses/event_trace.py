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

PREFIX = Path('/workspace/slurm-modern-install')
EVENT = Path('/workspace/slurm-event')
CLIENT = EVENT / ('client_state' if os.environ.get('SLURM_LICENSE_STATE_ONLY') == '1' else 'client')
EPOCH = 1597836579


def run_case(directory, jobs, capacity, enabled=True):
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'state').mkdir()
    clock_file = directory / 'clock.bin'
    clock_file.write_bytes(struct.pack('qQI', EPOCH - 120, 0, 0) + bytes(12))
    params = ('sched_min_interval=0,batch_sched_delay=0,sched_interval=60,'
              'bf_interval=30,bf_resolution=1,bf_window=4320,bf_max_job_test=4096,'
              'bf_max_job_part=64,bf_job_part_count_reserve=32,default_queue_depth=100000')
    if enabled:
        params += ',bf_licenses'
    config = directory / 'slurm.conf'
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
Licenses=capacity:{capacity}
NodeName=n[0000-{len(jobs)-1:04d}] NodeHostname=localhost NodeAddr=127.0.0.1 Port=18000 CPUs=1 RealMemory=128 State=IDLE
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
                 'dr_evt-sources.json', 'event-overlay.json'):
        source = Path('/results') / name
        if source.exists():
            shutil.copyfile(source, directory / name)
    manifest = {'scheduler': 'Slurm 26.11.0-0rc2, select/cons_tres',
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
    manifest['dr_evt_scheduler'] = 'FCFSPeriodicHybridCircularScheduler (scheduler_fcfs_periodic_hybrid_circular.cpp)'
    (directory / 'provenance.json').write_text(json.dumps(manifest, indent=2) + '\n')
    env = dict(os.environ, SLURM_CONF=str(config), SLURM_VALIDATION_CLOCK_FILE=str(clock_file),
               LD_PRELOAD=str(EVENT / 'libvalidation_clock.so'))
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
        for index in range(len(jobs)):
            command(f'REGISTER n{index:04d}')
        command('RELEASE')

        ids, schedule, completed = {}, {}, set()
        cursor, now, frames, idle_since = 0, 0, 0, None
        snapshot_stream = (directory / 'snapshots.jsonl').open('w')
        while len(completed) < len(jobs):
            command(f'TIME {EPOCH + now}')
            for index, row in list(schedule.items()):
                if index not in completed and row['end'] == now:
                    command(f'COMPLETE {ids[index]} n{index:04d}')
                    completed.add(index)
            while cursor < len(jobs) and jobs[cursor][0] <= now:
                _, tokens, limit, _ = jobs[cursor]
                ids[cursor] = command(f'SUBMIT {tokens} {limit // 60} n{cursor:04d}')['id']
                cursor += 1
            command('RELEASE')
            # Observe a stable native state while virtual time remains fixed.
            previous, stable = None, 0
            deadline = time.monotonic() + 10
            while stable < 3:
                time.sleep(.01)
                snapshot = command('DUMP')
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
                    assert row['assignment'] == f'n{index:04d}'
                    assert row['assignment'] not in owned_nodes
                    owned_nodes.add(row['assignment'])
                    if index not in schedule:
                        begin = row['start'] - EPOCH
                        assert begin == now, (now, row)
                        schedule[index] = {'start': begin, 'end': begin + jobs[index][3],
                                           'nodes': jobs[index][1], 'assignment': row['assignment']}
                if index in completed:
                    assert state == 3 and row['end'] == EPOCH + schedule[index]['end'], row
            assert allocated <= capacity
            assert snapshot['license_total'] == capacity and snapshot['license_used'] == allocated
            if allocated == 0 and cursor == len(jobs) and len(completed) < len(jobs):
                idle_since = now if idle_since is None else idle_since
                assert now - idle_since <= 60, ('Idle pool with pending jobs', snapshot)
            else:
                idle_since = None
            snapshot_stream.write(json.dumps({'time': now, 'allocated_node_equivalents': allocated,
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
        dr, dr_elapsed = run_dr_evt(directory / 'dr_evt', jobs,
                                    Path('/workspace/dr_evt-simulator').read_text().strip(),
                                    capacity, 30, 32, 64, 1, 259200)
        report = {'jobs': jobs, 'capacity': capacity, 'slurm_schedule': native, 'dr_evt_schedule': dr,
                  'slurm_label': f'Slurm (license pool, bf_licenses {"on" if enabled else "off"})',
                  'capacity_label': 'node equivalents',
                  'reservation_depth': 32, 'backfill_queue_depth': 64, 'backfill_interval_seconds': 30,
                  'plot_note': 'Native scheduling; trace-driven virtual time and completion RPCs',
                  'bf_licenses': enabled, 'resource_curves': {'slurm': profile(native), 'dr_evt': profile(dr)},
                  'slurm_metrics': metrics(jobs, native, capacity, native_elapsed),
                  'dr_evt_metrics': metrics(jobs, dr, capacity, dr_elapsed),
                  'different_start_times': sum(a['start'] != b['start'] for a, b in zip(native, dr)),
                  'audits': {'runtime_and_capacity': 'passed', 'private_node_assignments': 'passed',
                             'completed_jobs': len(completed), 'snapshots': frames},
                  'clock': 'integer virtual seconds; main FCFS dispatch and periodic backfill serialize with event batches'}
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
    parser.add_argument('--bf-licenses', choices=('on', 'off'), default='on',
                        help='License reservation planning for the Lassen run; controls exercise both settings')
    args = parser.parse_args()
    if args.trace:
        jobs, provenance = load_lassen(args.trace, 2000, 756)
        directory = args.outdir / 'lassen-prefix2000-hybrid-r32-q64'
        directory.mkdir(parents=True, exist_ok=True)
        (directory / 'workload.json').write_text(json.dumps(provenance, indent=2) + '\n')
        report = run_case(directory, jobs, 756, enabled=args.bf_licenses == 'on')
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
            report = run_case(args.outdir / name, jobs, 4, enabled)
            assert [row['start'] for row in report['slurm_schedule']] == expected
            if enabled:
                assert [row['start'] for row in report['dr_evt_schedule']] == expected
            reports.append(report)
        (args.outdir / 'controls.json').write_text(json.dumps(reports, indent=2) + '\n')
        for case in ('pooled', 'protection'):
            subprocess.run([sys.executable, str(Path(__file__).with_name('plot_timeline.py')),
                            str(args.outdir), '--case', case], check=True)


if __name__ == '__main__':
    main()
