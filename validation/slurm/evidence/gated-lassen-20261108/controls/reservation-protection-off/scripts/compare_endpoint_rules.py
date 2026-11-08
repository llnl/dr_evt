"""Isolate endpoint checks against saved corrected DR_EVT and native Slurm."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import sys
import time
import types

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compare import metrics, profile


def differences(native, observed, capacity):
    differing = [i for i, (a, b) in enumerate(zip(native, observed)) if a['start'] != b['start']]
    first = min(differing, key=lambda i: (min(native[i]['start'], observed[i]['start']), i), default=None)
    curves = {'slurm': profile(native), 'reference': profile(observed)}
    times = sorted({t for curve in curves.values() for t, _ in curve})
    positions, counts = dict.fromkeys(curves, 0), dict.fromkeys(curves, 0)
    area = 0
    for begin, end in zip(times, times[1:]):
        for name, curve in curves.items():
            while positions[name] < len(curve) and curve[positions[name]][0] <= begin:
                counts[name] = curve[positions[name]][1]
                positions[name] += 1
        area += (end - begin) * abs(counts['slurm'] - counts['reference'])
    return {
        'different_start_times': len(differing),
        'different_end_times': sum(a['end'] != b['end'] for a, b in zip(native, observed)),
        'max_start_difference_seconds': max(abs(a['start'] - b['start']) for a, b in zip(native, observed)),
        'resource_curve_difference_node_seconds': area,
        'resource_curve_difference_percent': 100 * area / (capacity * times[-1]),
        'first_chronological_difference': None if first is None else {
            'job': first + 1, 'slurm_start': native[first]['start'], 'reference_start': observed[first]['start']},
    }


def run_reference(source, name, workload, diagnostic_time):
    module = types.ModuleType('endpoint_reference_' + name)
    sys.modules[module.__name__] = module
    # Record protected reservations without changing any scheduling decision.
    marker = '            reserve(reservation, job)\n'
    assert source.count(marker) == 1
    instrumented = source.replace(marker, marker +
        '            if self.current_time == self.diagnostic_time:\n'
        '                self.diagnostic_reservations.append({"job": job.idx + 1, "start": reservation,\n'
        '                    "end": self.planned_end(reservation, job.duration), "nodes": job.nodes})\n')
    exec(compile(instrumented, name, 'exec'), module.__dict__)

    class TracedScheduler(module.HybridBackfillingScheduler):
        def calculate_conservative_window(self, position, timeline):
            result = super().calculate_conservative_window(position, timeline)
            if self.current_time == diagnostic_time:
                job = self.wait_queue[position]
                cached = self._conservative_plan_cache.get(id(timeline))
                if cached:
                    self.diagnostic_candidates.append({
                        'job': job.idx + 1, 'nodes': job.nodes, 'walltime': job.duration,
                        'protected_capacity': list(cached['plan']),
                        'admitted': self.can_fit_timeline(cached['plan'], job.nodes, job.duration)})
            return result

    scheduler = TracedScheduler(
        workload['capacity'], reservation_depth=workload['reservation_depth'],
        backfill_queue_depth=workload['backfill_queue_depth'],
        backfill_interval=workload['backfill_interval_seconds'],
        planning_time_resolution=1, planning_horizon=259200)
    scheduler.diagnostic_time = diagnostic_time
    scheduler.diagnostic_reservations, scheduler.diagnostic_candidates = [], []
    jobs = [module.Job(i, *row) for i, row in enumerate(workload['jobs'])]
    started = time.monotonic()
    completed = scheduler.simulate(jobs)
    elapsed = time.monotonic() - started
    observed = [{'start': job.start_time, 'end': job.end_time, 'nodes': job.nodes} for job in completed]
    return observed, elapsed, {'time': diagnostic_time,
                              'reservations': scheduler.diagnostic_reservations,
                              'candidates': scheduler.diagnostic_candidates}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--reference', type=Path, required=True,
                        help='Saved half-open Python reference from before the endpoint fix')
    parser.add_argument('--diagnostic-time', type=float, default=39870)
    args = parser.parse_args()
    report = json.loads(args.report.read_text())
    assert report['bf_licenses'] is True
    assert report['audits']['status'] == 'passed'
    args.output.mkdir(parents=True, exist_ok=False)
    source = args.reference.read_text()
    assert source.count('timeline[i][0] < end') == 1
    assert source.count('plan[i][0] < end') == source.count('plan[i][0] >= end') == 1
    admission = source.replace('timeline[i][0] < end', 'timeline[i][0] <= end')
    inclusive = admission.replace('plan[i][0] < end', 'plan[i][0] <= end').replace('plan[i][0] >= end', 'plan[i][0] > end')
    native = report['slurm_schedule']
    summary = {
        'jobs': len(report['jobs']), 'capacity': report['capacity'], 'bf_licenses': True,
        'native_reference': str(args.report.resolve()),
        'native_report_sha256': hashlib.sha256(args.report.read_bytes()).hexdigest(),
        'reference_source_sha256': hashlib.sha256(source.encode()).hexdigest(),
        'changes': {
            'baseline': 'Half-open admission and protected-reservation searches',
            'admission_only': 'Include the candidate endpoint in immediate admission only',
            'inclusive_searches': 'Include the endpoint in admission and protected-reservation placement; reservation allocation intervals and actual runtimes remain unchanged'},
        'variants': {}, 'native_slurm_rerun': False,
    }
    for name, text in (('baseline', source), ('admission_only', admission), ('inclusive_searches', inclusive)):
        directory = args.output / name
        directory.mkdir()
        (directory / 'reference.py').write_text(text)
        observed, elapsed, diagnostics = run_reference(text, name, report, args.diagnostic_time)
        for job, row in zip(report['jobs'], observed):
            assert row['start'] >= job[0] and row['end'] - row['start'] == job[3]
        curve = profile(observed)
        assert all(0 <= nodes <= report['capacity'] for _, nodes in curve)
        if name == 'baseline':
            assert observed == report['dr_evt_schedule'], 'Reference baseline must reproduce corrected DR_EVT exactly'
            summary['baseline_matches_corrected_dr_evt'] = True
        result = dict(report, dr_evt_schedule=observed,
                      dr_evt_metrics=metrics(report['jobs'], observed, report['capacity'], elapsed))
        result['resource_curves'] = {'slurm': profile(native), 'dr_evt': curve}
        result.update(differences(native, observed, report['capacity']))
        result['plot_note'] = 'Controlled endpoint experiment: ' + summary['changes'][name]
        result['audits'] = {'status': 'passed', 'native': 'reused saved audit',
                            'reference_checks': ['arrival constraints', 'exact runtimes', 'capacity'],
                            'baseline_matches_corrected_dr_evt': name == 'baseline'}
        for filename, data in (('report.json', result), ('diagnostic-pass.json', diagnostics)):
            (directory / filename).write_text(json.dumps(data, indent=2) + '\n')
        summary['variants'][name] = differences(native, observed, report['capacity'])
        with (directory / 'schedule.csv').open('w') as stream:
            writer = csv.writer(stream)
            writer.writerow(['job', 'start', 'end', 'nodes'])
            writer.writerows((i + 1, row['start'], row['end'], row['nodes']) for i, row in enumerate(observed))
        print(name, json.dumps(summary['variants'][name]), flush=True)
    (args.output / 'endpoint-comparison.json').write_text(json.dumps(summary, indent=2) + '\n')
    shutil.copy2(args.report, args.output / 'native-reference.json')
    shutil.copy2(__file__, args.output / Path(__file__).name)
    shutil.copy2(Path(__file__).resolve().parents[1] / 'compare.py', args.output / 'compare.py')
    with (args.output / 'jobs.csv').open('w') as stream:
        writer = csv.writer(stream)
        writer.writerow(['job_submit_time', 'num_nodes', 'time_limit', 'actual_run_time'])
        writer.writerows(report['jobs'])


if __name__ == '__main__':
    main()
