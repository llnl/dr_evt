"""Check whether one backfill-budget difference explains the gated license-on run."""
import argparse
import csv
import importlib.util
import json
from pathlib import Path
import shutil
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--reference', type=Path, required=True)
    args = parser.parse_args()
    report = json.loads(args.report.read_text())
    assert report['bf_licenses'] and report['batch_main_dispatch']
    spec = importlib.util.spec_from_file_location('ordering_reference', args.reference)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    differences = [i for i, (a, b) in enumerate(zip(report['slurm_schedule'], report['dr_evt_schedule']))
                   if a['start'] != b['start']]
    first = min(differences, key=lambda i: report['dr_evt_schedule'][i]['start'])
    timestamp = report['dr_evt_schedule'][first]['start']
    observations = []

    class ChargeStartingHead(module.HybridBackfillingScheduler):
        def try_start_jobs(self, allow_backfill=True):
            if self.current_time == timestamp and self.backfill_depth_time != timestamp:
                # The native log starts this fitting head in backfill, before
                # main dispatch removes it. Charge it to that tick's Q budget.
                assert self.wait_queue[0].nodes <= self.get_free_nodes()
                assert self.wait_queue[report['backfill_queue_depth']].idx == first
                observations.append({'time': timestamp,
                                     'starting_head': self.wait_queue[0].idx + 1,
                                     'first_differing_job': first + 1,
                                     'position_before_dispatch': report['backfill_queue_depth'] + 1,
                                     'extra_backfill_slot': 1})
                self.backfill_depth_time = timestamp
                self.backfill_depth_examined = 1
                self.backfill_depth_head_accounted = False
            return super().try_start_jobs(allow_backfill)

    schedules = {}
    for name, cls in (('baseline', module.HybridBackfillingScheduler),
                      ('charge_starting_head_once', ChargeStartingHead)):
        scheduler = cls(report['capacity'], reservation_depth=report['reservation_depth'],
                        backfill_queue_depth=report['backfill_queue_depth'],
                        backfill_interval=report['backfill_interval_seconds'],
                        planning_time_resolution=1, planning_horizon=259200)
        completed = scheduler.simulate([module.Job(i, *row) for i, row in enumerate(report['jobs'])])
        schedules[name] = [{'start': job.start_time, 'end': job.end_time, 'nodes': job.nodes}
                           for job in completed]
    assert schedules['baseline'] == report['dr_evt_schedule']
    native = [{key: row[key] for key in ('start', 'end', 'nodes')} for row in report['slurm_schedule']]
    assert schedules['charge_starting_head_once'] == native
    args.output.mkdir(parents=True, exist_ok=False)
    data = {'jobs': len(report['jobs']), 'observations': observations,
            'baseline_differing_starts': len(differences),
            'controlled_differing_starts': 0, 'controlled_differing_completions': 0,
            'scope': 'One controlled Q-budget change at the first difference, not a universal ordering rule',
            'schedules': schedules}
    (args.output / 'ordering-diagnosis.json').write_text(json.dumps(data, indent=2) + '\n')
    for name, rows in schedules.items():
        with (args.output / (name + '.csv')).open('w') as stream:
            writer = csv.writer(stream, lineterminator='\n')
            writer.writerow(['job', 'start', 'end', 'nodes'])
            writer.writerows((i + 1, row['start'], row['end'], row['nodes']) for i, row in enumerate(rows))
    shutil.copyfile(__file__, args.output / Path(__file__).name)
    shutil.copyfile(args.reference, args.output / 'reference.py')
    print(json.dumps({key: value for key, value in data.items() if key != 'schedules'}, indent=2))


if __name__ == '__main__':
    main()
