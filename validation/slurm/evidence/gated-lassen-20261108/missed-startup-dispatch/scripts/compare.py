"""Focused native bf_licenses experiments; physical nodes are private to each job."""
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import json
import csv
import os
from pathlib import Path
import re
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compare import metrics, profile, run_dr_evt

PREFIX = Path('/workspace/slurm-modern-install')
ROOT = Path('/results')
SELECT_TYPE = os.environ.get('SLURM_LICENSE_SELECT_TYPE', 'select/cons_tres')


def fields(text):
    return dict(re.findall(r'(\w+)=(\S+)', text))


def timestamp(value):
    return datetime.fromisoformat(value).replace(tzinfo=timezone.utc).timestamp()


def experiment(name, index, c_tokens, enabled):
    directory = ROOT / name
    directory.mkdir()
    (directory / 'state').mkdir()
    (directory / 'cgroup.conf').write_text('CgroupPlugin=disabled\n')
    jobs = [(0, 2, 120, 120), (0, 2, 180, 180),
            (0, c_tokens, 60, 60), (0, 1, 120, 120)]
    parameters = ('sched_min_interval=0,batch_sched_delay=0,sched_interval=60,'
                  'bf_interval=30,bf_resolution=1,bf_window=4320,'
                  'bf_max_job_test=4096,bf_max_job_part=64,'
                  'bf_job_part_count_reserve=32,default_queue_depth=100000')
    if enabled:
        parameters += ',bf_licenses'
    node_names = [f'case{index}n{i}' for i in range(4)]
    for node in node_names:
        (directory / f'spool-{node}').mkdir()
    node_config = '\n'.join(f'NodeName={node} NodeHostname=localhost NodeAddr=127.0.0.1 '
                            f'Port={18000 + index * 10 + i} CPUs=1 RealMemory=128 State=UNKNOWN'
                            for i, node in enumerate(node_names))
    config = directory / 'slurm.conf'
    config.write_text(f'''ClusterName=licenses{index}
SlurmctldHost=localhost
SlurmctldPort={17900 + index}
SlurmUser=root
SlurmdUser=root
AuthType=auth/none
CredType=cred/none
PluginDir={PREFIX}/lib/slurm
StateSaveLocation={directory}/state
SlurmctldPidFile={directory}/slurmctld.pid
SlurmctldLogFile={directory}/controller.log
SlurmctldDebug=debug2
DebugFlags=Backfill,BackfillMap
SlurmdSpoolDir={directory}/spool-%n
SlurmdPidFile={directory}/slurmd-%n.pid
SlurmdParameters=config_overrides
ReturnToService=2
SchedulerType=sched/backfill
SchedulerParameters={parameters}
SelectType={SELECT_TYPE}
SelectTypeParameters=CR_CPU
PriorityType=priority/basic
ProctrackType=proctrack/linuxproc
TaskPlugin=task/none
JobAcctGatherType=jobacct_gather/none
JobCompType=jobcomp/filetxt
JobCompLoc={directory}/jobcomp.log
JobRequeue=0
OverTimeLimit=1
MinJobAge=600
SlurmdTimeout=300
SlurmctldTimeout=300
MessageTimeout=10
MailProg=/bin/true
Licenses=capacity:4
{node_config}
PartitionName=normal Nodes=ALL Default=YES MaxTime=INFINITE State=UP OverSubscribe=EXCLUSIVE
''')
    env = dict(os.environ, SLURM_CONF=str(config))
    processes, logs = [], []

    def daemon(binary, *args):
        log = (directory / (binary + '-' + str(len(processes)) + '.log')).open('w')
        logs.append(log)
        processes.append(subprocess.Popen([str(PREFIX / 'sbin' / binary), '-D', '-f',
                                            str(config), *args], env=env,
                                           stdout=log, stderr=subprocess.STDOUT))

    def command(binary, *args):
        return subprocess.check_output([str(PREFIX / 'bin' / binary), *args],
                                        env=env, text=True, stderr=subprocess.STDOUT,
                                        timeout=20)

    try:
        daemon('slurmctld')
        for node in node_names:
            daemon('slurmd', '-N', node)
        deadline = time.monotonic() + 60
        while True:
            if any(process.poll() is not None for process in processes):
                raise RuntimeError(f'{name}: a native daemon exited; see logs')
            try:
                ready = command('sinfo', '-h', '-o', '%T').splitlines()
                if ready and all(state == 'idle' for state in ready):
                    break
            except subprocess.CalledProcessError:
                pass
            if time.monotonic() >= deadline:
                raise TimeoutError(f'{name}: nodes did not register')
            time.sleep(.5)
        (directory / 'effective-config.txt').write_text(command('scontrol', 'show', 'config'))
        (directory / 'initial-licenses.txt').write_text(command('scontrol', 'show', 'licenses'))
        identifiers = []
        for label, node, (_, tokens, limit, duration) in zip('ABCD', node_names, jobs):
            script = directory / f'{label}.sh'
            script.write_text(f'#!/bin/sh\ndate -u +%s\nsleep {duration}\ndate -u +%s\n')
            identifiers.append(command('sbatch', '--parsable', '--hold', '--job-name', label,
                                       '--nodes=1', '--ntasks=1', '--nodelist', node,
                                       '--licenses', f'capacity:{tokens}', '--time', str(limit // 60),
                                       '--output', str(directory / f'{label}.out'), str(script)).strip())
        (directory / 'workload.json').write_text(json.dumps({
            'jobs': jobs, 'job_ids': identifiers, 'private_nodes': node_names,
            'resource_mapping': 'each demanded node is one global capacity license; '
                                'each job has one private physical node',
            'bf_licenses': enabled, 'capacity': 4, 'backfill_interval_seconds': 30,
            'clock': 'native real wall clock; no source overlays'}, indent=2) + '\n')
        release = time.time()
        command('scontrol', 'release', ','.join(identifiers))
        snapshots = []
        deadline = time.monotonic() + 450
        while True:
            job_text = command('scontrol', '-o', 'show', 'jobs')
            license_text = command('scontrol', 'show', 'licenses')
            rows = [fields(line) for line in job_text.splitlines() if 'JobId=' in line]
            snapshots.append({'time': time.time(), 'jobs': rows, 'licenses': license_text})
            with (directory / 'snapshots.jsonl').open('a') as stream:
                stream.write(json.dumps(snapshots[-1]) + '\n')
            running = [row for row in rows if row['JobState'] in ('RUNNING', 'COMPLETING')]
            assert sum(jobs[identifiers.index(row['JobId'])][1] for row in running) <= 4
            assert len({row['NodeList'] for row in running}) == len(running)
            if len(rows) == 4 and all(row['JobState'] == 'COMPLETED' for row in rows):
                break
            failed = [row for row in rows if row['JobState'] in ('FAILED', 'TIMEOUT', 'CANCELLED', 'NODE_FAIL')]
            if failed:
                raise AssertionError(failed)
            if time.monotonic() >= deadline:
                raise TimeoutError(name)
            time.sleep(.5)
        (directory / 'final-jobs.txt').write_text(job_text)
        (directory / 'diagnostics.txt').write_text(command('sdiag'))
        by_id = {row['JobId']: row for row in rows}
        origin = min(timestamp(row['StartTime']) for row in rows)
        schedule = []
        for identifier, node, (_, tokens, limit, duration) in zip(identifiers, node_names, jobs):
            row = by_id[identifier]
            assert row['NodeList'] == node and row['Licenses'] == f'capacity:{tokens}'
            begin, end = timestamp(row['StartTime']), timestamp(row['EndTime'])
            schedule.append({'start': begin - origin, 'end': end - origin,
                             'nodes': tokens, 'assignment': node})
        observed, dr_elapsed = run_dr_evt(directory / 'dr_evt', jobs,
                                 Path('/workspace/dr_evt-simulator').read_text().strip(),
                                 4, 30, 32, 64, 1, 259200)
        report = {'case': name, 'bf_licenses': enabled, 'jobs': jobs,
                  'capacity': 4, 'release_wall_time': release, 'origin_wall_time': origin,
                  'select_type': SELECT_TYPE,
                  'slurm_label': 'Slurm (native)',
                  'capacity_label': 'pooled license tokens (node equivalents)',
                  'reservation_depth': 32, 'backfill_queue_depth': 64,
                  'backfill_interval_seconds': 30,
                  'plot_note': f'Native Slurm wall clock; private nodes; bf_licenses={enabled}',
                  'slurm_schedule': schedule, 'dr_evt_schedule': observed,
                  'slurm_metrics': metrics(jobs, schedule, 4, time.time() - release),
                  'dr_evt_metrics': metrics(jobs, observed, 4, dr_elapsed),
                  'different_start_times': sum(a['start'] != b['start'] for a, b in zip(schedule, observed)),
                  'resource_curves': {'slurm': profile(schedule), 'dr_evt': profile(observed)},
                  'start_differences_seconds': [a['start'] - b['start'] for a, b in zip(schedule, observed)],
                  'audits': {'capacity_and_private_node_ownership': 'passed',
                             'completed_jobs': 4, 'snapshots': len(snapshots)},
                  'clock': 'native wall clock; start times normalized to first native start'}
        (directory / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        (directory / 'plot-input.json').write_text(json.dumps(report, indent=2) + '\n')
        (directory / 'plot.py').write_bytes((Path(__file__).resolve().parents[1] / 'plot.py').read_bytes())
        for scheduler, curve in report['resource_curves'].items():
            with (directory / f'{scheduler}-resource-curve.csv').open('w') as stream:
                writer = csv.writer(stream)
                writer.writerow(['time', 'allocated_node_equivalents'])
                writer.writerows(curve)
        print(name, [row['start'] for row in schedule], 'DR_EVT',
              [row['start'] for row in observed], flush=True)
        return report
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
        for process in processes:
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        for log in logs:
            log.close()


def main():
    Path('/workspace/slurm-modern-etc').mkdir(exist_ok=True)
    Path('/workspace/slurm-modern-etc/cgroup.conf').write_text('CgroupPlugin=disabled\n')
    cases = [('four-job-pooled', 0, 3, True),
             ('reservation-protection-on', 1, 4, True),
             ('reservation-protection-off', 2, 4, False)]
    with ThreadPoolExecutor(max_workers=3) as pool:
        futures = [pool.submit(experiment, *case) for case in cases]
        reports = [future.result() for future in futures]
    from finalize import finalize
    finalize(ROOT)


if __name__ == '__main__':
    main()
