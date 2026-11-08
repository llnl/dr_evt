"""Test the effect of the missed time-zero dispatch in the first gated off run."""
import argparse
import csv
import json
from pathlib import Path
import shutil

from diagnose_license_off import replay


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    report = json.loads(args.report.read_text())
    assert not report['bf_licenses'] and report['diagnostic_no_reservations']
    baseline, _ = replay(report['jobs'], report['capacity'], True)
    assert baseline == report['dr_evt_schedule']
    jobs = [tuple(row) for row in report['jobs']]
    initial = [i for i, row in enumerate(jobs) if row[0] == 0]
    assert initial == [0, 1]
    assert all(report['slurm_schedule'][i]['start'] == 1 for i in initial)
    for i in initial:
        jobs[i] = (1, *jobs[i][1:])
    delayed, _ = replay(jobs, report['capacity'], True)
    native = [{key: row[key] for key in ('start', 'end', 'nodes')}
              for row in report['slurm_schedule']]
    assert delayed == native
    args.output.mkdir(parents=True, exist_ok=False)
    data = {'jobs': len(jobs), 'delayed_initial_jobs': [i + 1 for i in initial],
            'original_arrival': 0, 'controlled_release_to_queue': 1,
            'baseline_differing_starts': report['different_start_times'],
            'controlled_differing_starts': 0, 'controlled_differing_completions': 0,
            'scope': 'Delay only the initial two jobs in a controlled replay; original native inputs remain unchanged',
            'controlled_schedule': delayed}
    (args.output / 'startup-diagnosis.json').write_text(json.dumps(data, indent=2) + '\n')
    with (args.output / 'delayed-startup.csv').open('w') as stream:
        writer = csv.writer(stream, lineterminator='\n')
        writer.writerow(['job', 'start', 'end', 'nodes'])
        writer.writerows((i + 1, row['start'], row['end'], row['nodes']) for i, row in enumerate(delayed))
    for name in (Path(__file__).name, 'diagnose_license_off.py', 'compare_endpoint_rules.py'):
        shutil.copyfile(Path(__file__).with_name(name), args.output / name)
    print(json.dumps({key: value for key, value in data.items() if key != 'controlled_schedule'}, indent=2))


if __name__ == '__main__':
    main()
