"""Isolate license-off dispatch, reservation, and event-order differences."""
import argparse
import csv
from datetime import datetime, timezone
import gzip
import hashlib
import heapq
import importlib.util
import json
from pathlib import Path
import shutil
import sys

from compare_endpoint_rules import differences


def replay(jobs, capacity, batch_events):
    """Scan FCFS order, skipping shortages, with no future pool reservations."""
    events = [(row[0], 1, i) for i, row in enumerate(jobs)]
    heapq.heapify(events)
    pending, schedule, decisions = [], [None] * len(jobs), []
    free = capacity
    while events:
        now = events[0][0]
        batch = [heapq.heappop(events)]
        if batch_events:
            while events and events[0][0] == now:
                batch.append(heapq.heappop(events))
        for _, kind, index in batch:
            if kind == 0:
                free += jobs[index][1]
            else:
                pending.append(index)
        before, starts, remaining = free, [], []
        for index in pending:
            _, nodes, _, duration = jobs[index]
            if nodes > free:
                remaining.append(index)
                continue
            schedule[index] = {'start': now, 'end': now + duration, 'nodes': nodes}
            free -= nodes
            starts.append(index + 1)
            heapq.heappush(events, (now + duration, 0, index))
        pending = remaining
        assert 0 <= free <= capacity
        if now in (13, 798):
            decisions.append({'time': now,
                              'events': [{'kind': 'completion' if kind == 0 else 'arrival',
                                          'job': index + 1} for _, kind, index in batch],
                              'free_before_dispatch': before, 'starts': starts,
                              'free_after_dispatch': free})
    assert not pending and all(row is not None for row in schedule)
    return schedule, decisions


def reference_replays(path, report):
    """Verify the corrected DR_EVT baseline before changing only protection."""
    spec = importlib.util.spec_from_file_location('license_off_reference', path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    class NoProtection(module.HybridBackfillingScheduler):
        def calculate_conservative_window(self, position, timeline):
            return None

    result = {}
    for name, scheduler_type in (('corrected_dr_evt', module.HybridBackfillingScheduler),
                                 ('no_future_protection', NoProtection)):
        scheduler = scheduler_type(
            report['capacity'], reservation_depth=report['reservation_depth'],
            backfill_queue_depth=report['backfill_queue_depth'],
            backfill_interval=report['backfill_interval_seconds'],
            planning_time_resolution=1, planning_horizon=259200)
        completed = scheduler.simulate([module.Job(i, *row)
                                       for i, row in enumerate(report['jobs'])])
        result[name] = [{'start': job.start_time, 'end': job.end_time, 'nodes': job.nodes}
                        for job in completed]
    assert result['corrected_dr_evt'] == report['dr_evt_schedule']
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path, help='Endpoint-corrected license-off report')
    parser.add_argument('output', type=Path)
    parser.add_argument('--reference', type=Path, required=True,
                        help='Saved inclusive-search Python reference')
    parser.add_argument('--native-run', type=Path, required=True,
                        help='Original native run containing controller.log.gz')
    args = parser.parse_args()
    report = json.loads(args.report.read_text())
    assert report['bf_licenses'] is False and report['audits']['status'] == 'passed'
    native_report = json.loads((args.native_run / 'report.json').read_text())
    assert native_report['jobs'] == report['jobs']
    assert native_report['slurm_schedule'] == report['slurm_schedule']
    args.output.mkdir(parents=True, exist_ok=False)
    schedules = reference_replays(args.reference, report)
    batched, batched_decisions = replay(report['jobs'], report['capacity'], True)
    serial, serial_decisions = replay(report['jobs'], report['capacity'], False)
    schedules.update(skip_dispatch_batched=batched, skip_dispatch_serial=serial)
    native = report['slurm_schedule']
    summary = {
        'jobs': len(report['jobs']), 'capacity': report['capacity'],
        'bf_licenses': False, 'native_slurm_rerun': False,
        'inputs': {name: {'path': str(path.resolve()),
                          'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                   for name, path in (('corrected_report', args.report),
                                      ('inclusive_reference', args.reference),
                                      ('native_report', args.native_run / 'report.json'))},
        'variants': {},
        'changes': {
            'corrected_dr_evt': 'Baseline Python reference reproduces saved corrected C++ DR_EVT',
            'no_future_protection': 'Remove only future pooled reservations; retain strict event dispatch and Q=64 periodic scans',
            'skip_dispatch_batched': 'Also scan past shortages during main dispatch; batch simultaneous events',
            'skip_dispatch_serial': 'Same skipping dispatch without future pool protection; dispatch after each individual completion or arrival, completions first, job-ID order'},
        'decisions': {'batched': batched_decisions, 'serial': serial_decisions},
        'first_dispatch_difference': {
            'time': 13, 'blocked_head': 56, 'head_request': report['jobs'][55][1],
            'free_capacity': report['capacity'] - sum(row['nodes'] for row in native
                                                     if row['start'] < 13 < row['end']),
            'jobs_started_by_native_main_dispatch': [57, 58, 59],
            'corrected_dr_evt_starts': {str(i): report['dr_evt_schedule'][i - 1]['start']
                                        for i in (57, 58, 59)}},
        'first_batching_difference': {
            'time': 798, 'completing_jobs': [135, 180], 'each_release': 1,
            'earlier_two_license_job': 232, 'serial_one_license_starts': [243, 245],
            'native_job232_start': native[231]['start'], 'batched_job232_start': batched[231]['start']},
        'source_evidence': {
            'main_dispatch': 'external/slurm/src/slurmctld/job_scheduler.c: ESLURM_LICENSES_UNAVAILABLE breaks scheduling only when bf_licenses is true',
            'backfill': 'external/slurm/src/plugins/sched/backfill/backfill.c: with bf_licenses false, license_job_test checks current availability and skips failures; future license map is absent',
            'adapter': 'validation/slurm/licenses/event_trace.py issues individual COMPLETE RPCs before RELEASE; client.c sends completion and epilog RPCs',
            'dr_evt': 'src/sim/sim.cpp applies the full equal-time state-event batch before dispatch'},
        'scope': 'Exact reconstruction of this saved adapter run, not proof that production Slurm always dispatches between simultaneous completions. Real controller/RPC timing can change that ordering.',
    }
    for name, rows in schedules.items():
        assert all(row['start'] >= job[0] and row['end'] - row['start'] == job[3]
                   for row, job in zip(rows, report['jobs']))
        summary['variants'][name] = differences(native, rows, report['capacity'])
        with (args.output / (name + '.csv')).open('w') as stream:
            writer = csv.writer(stream)
            writer.writerow(['job', 'start', 'end', 'nodes'])
            writer.writerows((i + 1, row['start'], row['end'], row['nodes'])
                             for i, row in enumerate(rows))
    assert summary['variants']['skip_dispatch_serial']['different_start_times'] == 0
    assert summary['variants']['skip_dispatch_serial']['different_end_times'] == 0
    assert summary['variants']['skip_dispatch_serial']['resource_curve_difference_node_seconds'] == 0
    log_path = args.native_run / 'controller.log.gz'
    stamp = lambda time: datetime.fromtimestamp(1597836579 + time, timezone.utc).strftime('%Y-%m-%dT%H:%M:%S')
    stamps = {stamp(time) for time in (13, 798, 847)}
    with gzip.open(log_path, 'rt') as stream:
        evidence = [line.rstrip() for line in stream if line[1:20] in stamps]
    assert any('Allocate JobId=57 ' in line for line in evidence)
    assert any('Allocate JobId=243 ' in line for line in evidence)
    assert any('Allocate JobId=245 ' in line for line in evidence)
    summary['native_log_evidence'] = evidence
    (args.output / 'diagnosis.json').write_text(json.dumps(summary, indent=2) + '\n')
    (args.output / 'plot-data.json').write_text(json.dumps({
        'jobs': report['jobs'], 'slurm': native, **schedules}, indent=2) + '\n')
    shutil.copy2(args.reference, args.output / 'reference.py')
    shutil.copy2(__file__, args.output / Path(__file__).name)
    shutil.copy2(Path(__file__).with_name('compare_endpoint_rules.py'),
                 args.output / 'compare_endpoint_rules.py')
    shutil.copy2(Path(__file__).resolve().parents[1] / 'compare.py',
                 args.output / 'compare.py')
    print(json.dumps(summary['variants'], indent=2))


if __name__ == '__main__':
    main()
