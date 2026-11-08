"""Audit saved native license experiments and regenerate their plots and data."""
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compare import metrics, profile


def finalize(root):
    reports = []
    for directory in sorted(root.iterdir()):
        if not (directory / 'report.json').exists():
            continue
        report = json.loads((directory / 'report.json').read_text())
        config = (directory / 'slurm.conf').read_text()
        select_type = re.search(r'^SelectType=(.*)$', config, re.M)[1]
        report.update(select_type=select_type, slurm_label='Slurm (native)',
                      capacity_label='pooled license tokens (node equivalents)',
                      reservation_depth=32, backfill_queue_depth=64, backfill_interval_seconds=30,
                      plot_note=f'Native Slurm wall clock; private nodes; bf_licenses={report["bf_licenses"]}')
        assert 'sched_min_interval=0,batch_sched_delay=0' in config
        assert 'OverTimeLimit=1' in config
        report['over_time_limit_minutes'] = 1
        report['resource_curve_source'] = ('reconstructed from native completed job records; '
                                          'native license use is also sampled in snapshots.jsonl')
        jobs = report['jobs']
        final_rows = [dict(re.findall(r'(\w+)=(\S+)', line))
                      for line in (directory / 'final-jobs.txt').read_text().splitlines()]
        assert all(row['JobState'] == 'COMPLETED' and row['ExitCode'] == '0:0' for row in final_rows)
        runtime_offsets = []
        for scheduler in ('slurm', 'dr_evt'):
            schedule = report[scheduler + '_schedule']
            for job, row in zip(jobs, schedule):
                assert row['start'] >= job[0] and row['nodes'] == job[1]
                offset = row['end'] - row['start'] - job[3]
                if scheduler == 'slurm':
                    runtime_offsets.append(offset)
                    assert 0 <= offset <= 3, ('Unexpected native runtime', offset)
                else:
                    assert offset == 0
            assert all(0 <= count <= report['capacity'] for _, count in profile(schedule))
            elapsed = report.get(scheduler + '_metrics', {}).get('process_elapsed_seconds')
            report[scheduler + '_metrics'] = metrics(jobs, schedule, 4, elapsed)
            with (directory / f'{scheduler}-resource-curve.csv').open('w') as stream:
                writer = csv.writer(stream)
                writer.writerow(['time', 'allocated_node_equivalents'])
                writer.writerows(report['resource_curves'][scheduler])
        snapshots = [json.loads(line) for line in (directory / 'snapshots.jsonl').read_text().splitlines()]
        for snapshot in snapshots:
            used = int(re.search(r'Used=(\d+)', snapshot['licenses'])[1])
            assert 0 <= used <= 4
        controller = (directory / 'controller.log').read_text()
        report['audits'].update(completion_exit_codes='passed', schedule_capacity='passed',
                                sampled_native_license_capacity='passed',
                                native_runtime_overhead_seconds=runtime_offsets)
        report['backfill_started_job_D'] = 'Started JobId=4' in controller
        report['backfill_planned_job_C'] = bool(re.search(r'JobId=3 to start at', controller))
        report['different_start_times'] = sum(a['start'] != b['start'] for a, b in
                                              zip(report['slurm_schedule'], report['dr_evt_schedule']))
        for filename in ('report.json', 'plot-input.json'):
            (directory / filename).write_text(json.dumps(report, indent=2) + '\n')
        plot_script = Path(__file__).resolve().parents[1] / 'plot.py'
        (directory / 'plot.py').write_bytes(plot_script.read_bytes())
        subprocess.run([sys.executable, str(directory / 'plot.py'),
                        str(directory / 'plot-input.json')], check=True)
        with (directory / 'plot-summary.csv').open('w') as stream:
            writer = csv.writer(stream)
            writer.writerow(['metric', 'dr_evt', 'slurm', 'unit'])
            for key, scale, unit in (('mean_wait_seconds', 3600, 'hours'),
                                     ('makespan_seconds', 3600, 'hours'),
                                     ('utilization_percent', 1, 'percent'),
                                     ('resource_area_node_seconds', 1, 'node-equivalent seconds')):
                writer.writerow([key, *[report[name + '_metrics'][key] / scale
                                         for name in ('dr_evt', 'slurm')], unit])
        reports.append(report)
    assert len(reports) == 3
    by_case = {report['case']: report for report in reports}
    pooled = by_case['four-job-pooled']
    on = by_case['reservation-protection-on']
    off = by_case['reservation-protection-off']
    starts = lambda report: [row['start'] for row in report['slurm_schedule']]
    facts = {
        'four_job_D_starts_before_C': starts(pooled)[3] < starts(pooled)[2],
        'four_job_D_started_by_backfill': pooled['backfill_started_job_D'],
        'reservation_on_C_starts_before_D': starts(on)[2] < starts(on)[3],
        'reservation_off_D_starts_before_C': starts(off)[3] < starts(off)[2],
        'reservation_on_C_start_seconds': starts(on)[2],
        'reservation_off_C_start_seconds': starts(off)[2],
        'reservation_off_C_delay_seconds': starts(off)[2] - starts(on)[2],
        'reservation_on_backfill_planned_C': on['backfill_planned_job_C'],
    }
    if pooled['select_type'] == 'select/cons_tres':
        assert all(facts[key] for key in (
            'four_job_D_starts_before_C', 'four_job_D_started_by_backfill',
            'reservation_on_C_starts_before_D', 'reservation_off_D_starts_before_C',
            'reservation_on_backfill_planned_C'))
        assert facts['reservation_off_C_delay_seconds'] > 0
    scripts = root / 'scripts'
    scripts.mkdir(exist_ok=True)
    files = list(Path(__file__).parent.glob('*.py')) + list(Path(__file__).parent.glob('*.sh'))
    files.append(Path(__file__).resolve().parents[1] / 'plot.py')
    hashes = {}
    for source in files:
        data = source.read_bytes()
        (scripts / source.name).write_bytes(data)
        hashes[source.name] = hashlib.sha256(data).hexdigest()
    summary = {'select_type': pooled['select_type'], 'cases': reports, 'findings': facts,
               'script_sha256': hashes, 'native_scheduler_source_overlays': [],
               'scope': 'three focused four-job experiments; real native wall clock, '
                        'not the accelerated 2,000-job simulator comparison'}
    (root / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(facts, indent=2), flush=True)


if __name__ == '__main__':
    finalize(Path(sys.argv[1]))
