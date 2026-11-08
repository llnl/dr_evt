"""Save matched native Slurm license-planning on/off comparison data."""
import argparse
import csv
import json
from pathlib import Path
import shutil


def summarize(on_directory, off_directory, output):
    on = json.loads((on_directory / 'report.json').read_text())
    off = json.loads((off_directory / 'report.json').read_text())
    assert on['bf_licenses'] is True and off['bf_licenses'] is False
    for key in ('jobs', 'capacity', 'reservation_depth', 'backfill_queue_depth',
                'backfill_interval_seconds'):
        assert on[key] == off[key], key
    manifests = [json.loads((directory / 'provenance.json').read_text())
                 for directory in (on_directory, off_directory)]
    assert manifests[0]['binary_sha256'] == manifests[1]['binary_sha256']
    for directory in (on_directory, off_directory):
        assert json.loads((directory / 'audit.json').read_text())['status'] == 'passed'
    output.mkdir(parents=True, exist_ok=True)
    metrics = {'DR_EVT (protected)': on['dr_evt_metrics'],
               'DR_EVT (no-reservation diagnostic)': off['dr_evt_metrics'],
               'Slurm bf_licenses on': on['slurm_metrics'],
               'Slurm bf_licenses off': off['slurm_metrics']}
    keys = ('mean_wait_seconds', 'makespan_seconds', 'utilization_percent',
            'resource_area_node_seconds')
    with (output / 'license-planning-metrics.csv').open('w') as stream:
        writer = csv.writer(stream)
        writer.writerow(['scheduler', *keys])
        for label, values in metrics.items():
            writer.writerow([label, *(values[key] for key in keys)])
    deltas = [b['start'] - a['start'] for a, b in
              zip(on['slurm_schedule'], off['slurm_schedule'])]
    fields = ('different_start_times', 'max_start_difference_seconds',
              'resource_curve_difference_node_seconds', 'resource_curve_difference_percent')
    result = {'jobs': len(on['jobs']), 'capacity': on['capacity'],
              'matched_inputs_and_binaries': True, 'both_audits': 'passed',
              'metrics': metrics,
              'dr_evt_models': {'on': on.get('dr_evt_label', 'DR_EVT'),
                                'off': off.get('dr_evt_label', 'DR_EVT')},
              'batch_main_dispatch': on.get('batch_main_dispatch', False),
              'protected_off_control_different_starts': off.get('protected_dr_evt_control', {}).get('different_start_times'),
              'versus_dr_evt': {name: {key: report[key] for key in fields}
                                for name, report in (('on', on), ('off', off))},
              'off_minus_on_start_times': {
                  'different_jobs': sum(delta != 0 for delta in deltas),
                  'max_absolute_seconds': max(map(abs, deltas)),
                  'per_job_seconds': deltas},
              'result_directories': {'on': str(on_directory), 'off': str(off_directory)}}
    (output / 'license-planning-comparison.json').write_text(json.dumps(result, indent=2) + '\n')
    script = output / 'summarize_licenses.py'
    if Path(__file__).resolve() != script.resolve():
        shutil.copyfile(__file__, script)
    print(json.dumps(result['versus_dr_evt'], indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('on_directory', type=Path)
    parser.add_argument('off_directory', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    summarize(args.on_directory, args.off_directory, args.output)
