"""Compare the recorded Lassen workload using capacity or exact placement."""
import argparse
import csv
from datetime import datetime, timezone
from decimal import Decimal
import hashlib
import json
import math
import os
from pathlib import Path

from fiction_runner import run_case
from test_paired import observed_profile, profile, reference_schedule, simulator_case
from resource_profiles import area, export_curves, native_profile
from plot_comparison import plot_case


def source_period(path, row_indices=None):
    """Find recorded timestamp bounds for the source log or selected rows."""
    selected = None if row_indices is None else set(row_indices)
    earliest, latest = None, None
    with path.open() as stream:
        for index, row in enumerate(csv.DictReader(stream)):
            if selected is not None and index not in selected:
                continue
            for key in ('job_submit_time', 'submit_time', 'begin_time', 'end_time'):
                if key not in row:
                    continue
                value = Decimal(row[key])
                earliest = value if earliest is None else min(earliest, value)
                latest = value if latest is None else max(latest, value)
    if earliest is None:
        raise ValueError('Source log has no timestamps')
    start = datetime.fromtimestamp(float(earliest), timezone.utc)
    end = datetime.fromtimestamp(float(latest), timezone.utc)
    return {'earliest_timestamp': str(earliest), 'latest_timestamp': str(latest),
            'earliest_utc': start.isoformat(), 'latest_utc': end.isoformat(),
            'label': f'Lassen ({start:%Y-%m-%d}–{end:%Y-%m-%d} UTC)'}


def load_jobs(path, limit, capacity):
    rows = []
    with path.open() as stream:
        for index, row in enumerate(csv.DictReader(stream)):
            arrival = Decimal(row.get('job_submit_time', row.get('submit_time')))
            duration = (Decimal(row['duration']) if 'duration' in row else
                        Decimal(row['end_time']) - Decimal(row['begin_time']))
            nodes, walltime = int(row['num_nodes']), Decimal(row['time_limit'])
            if not (arrival.is_finite() and duration.is_finite() and walltime.is_finite()
                    and duration > 0 and walltime > 0 and 0 < nodes <= capacity):
                raise ValueError(f'Invalid workload row {index + 2}: {row}')
            rows.append((arrival, index, nodes, walltime, duration))
    rows.sort(key=lambda row: (row[0], row[1]))
    if limit:
        rows = rows[:limit]
    if not rows:
        raise ValueError('Empty workload')
    origin = rows[0][0]
    if any(walltime != walltime.to_integral_value() for _, _, _, walltime, _ in rows):
        raise ValueError('DR_EVT simple CSV requires integer time limits')
    jobs = [(float(arrival - origin), nodes, int(walltime), float(duration))
            for arrival, _, nodes, walltime, duration in rows]
    # Fiction's trace parser currently formats submit times to whole seconds.
    if any(not job[0].is_integer() for job in jobs):
        raise ValueError('Fractional arrivals require a higher-precision Fiction input adapter')
    return jobs, str(origin), [row[1] for row in rows]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('simulator', type=Path)
    parser.add_argument('--total-nodes', type=int, default=756)
    parser.add_argument('--max-jobs', type=int, default=2000,
                        help='Chronological prefix; 0 selects the whole file')
    parser.add_argument('--queue-depth', type=int, default=64)
    parser.add_argument('--reservation-depth', type=int, default=32)
    parser.add_argument('--policy', choices=('none', 'hybrid'), default='hybrid')
    parser.add_argument('--scheduler-backend', choices=('fluxion', 'sched-simple'),
                        default='fluxion')
    parser.add_argument('--resource-model', choices=('capacity', 'placement'), default='capacity',
                        help='DR_EVT resource model; default preserves the existing capacity comparison')
    parser.add_argument('--placement-order', choices=('low', 'high'), default='low')
    parser.add_argument('--placement-optimizations', choices=('none', 'fast', 'batch', 'scratch', 'boolean', 'all'), default='all')
    parser.add_argument('--placement-coarse-filter', action='store_true')
    parser.add_argument('--timeout', type=int, default=900)
    parser.add_argument('--outdir', type=Path, default=Path('/results/lassen-backfill'))
    args = parser.parse_args()
    if args.scheduler_backend == 'sched-simple' and args.policy != 'none':
        parser.error('sched-simple requires --policy none')
    reservations = args.reservation_depth if args.policy == 'hybrid' else 0
    if (args.total_nodes < 1 or args.max_jobs < 0 or args.queue_depth < 0 or
            (args.policy == 'hybrid' and args.reservation_depth < 1) or args.timeout <= 0):
        parser.error('nodes and timeout must be positive; hybrid needs positive reservation depth; job limit and queue depth nonnegative')
    if args.resource_model == 'capacity' and (args.placement_order != 'low' or
            args.placement_optimizations != 'all' or args.placement_coarse_filter):
        parser.error('placement options require --resource-model placement')
    period = source_period(args.trace)
    jobs, origin, source_indices = load_jobs(args.trace, args.max_jobs, args.total_nodes)
    selected_period = source_period(args.trace, source_indices)
    root = args.outdir
    root.mkdir(parents=True, exist_ok=True)
    if args.resource_model == 'placement' and (root / 'workload.json').exists():
        parser.error('placement output already exists; choose a new --outdir to preserve earlier evidence')
    label = f'{args.policy}_r{reservations}_q{args.queue_depth}_{args.trace.stem}_jobs_{len(jobs)}'
    if args.resource_model == 'placement':
        label += f'_placement_{args.placement_order}_{args.placement_optimizations}'
        if args.placement_coarse_filter:
            label += '_coarse'
    case = root / label
    case.mkdir(exist_ok=True)
    os.environ['DR_EVT_OBSERVE_RESOURCES'] = '1'
    observer = Path(__file__).with_name('resource_observer')
    os.environ['PYTHONPATH'] = str(observer) + ':' + os.environ.get('PYTHONPATH', '')
    observed = simulator_case(case, jobs, args.simulator, args.policy, args.queue_depth,
                              reservations, args.total_nodes, resource_model=args.resource_model,
                              placement_optimizations=args.placement_optimizations,
                              placement_order=args.placement_order, coarse_filter=args.placement_coarse_filter,
                              timeout=args.timeout)
    manifest = {
        'workload_source': 'user-provided Lassen dataset', 'source_file': args.trace.name,
        'source_period': period,
        'selected_source_period': selected_period,
        'source_sha256': hashlib.sha256(args.trace.read_bytes()).hexdigest(),
        'source_time_origin': origin, 'source_row_indices': source_indices,
        'source_selection': 'earliest arrivals, stable input order for ties',
        'initial_state': 'empty cluster', 'capacity': args.total_nodes,
        'per_pass_accounting': args.resource_model == 'capacity', 'output_time_precision_decimals': 3,
        'dr_evt_resource_model': args.resource_model,
        'dr_evt_queue_depth_semantics': ('per-pass examination' if args.resource_model == 'capacity'
                                         else 'shared per-timestamp backfill examination'),
        'placement_order': args.placement_order if args.resource_model == 'placement' else None,
        'placement_optimizations': args.placement_optimizations if args.resource_model == 'placement' else None,
        'placement_coarse_filter': args.placement_coarse_filter if args.resource_model == 'placement' else None,
        'durations': 'recorded duration, or recorded end_time minus begin_time; unchanged',
        'resource_model': 'homogeneous whole-node pool, one core per node',
        'queue_depth': args.queue_depth, 'reservation_depth': reservations,
        'backfill_policy': args.policy, 'fiction_scheduler': args.scheduler_backend,
        'jobs': len(jobs), 'status': 'running',
        'simulator_sha256': hashlib.sha256(args.simulator.read_bytes()).hexdigest(),
    }
    (root / 'workload.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Running {len(jobs)} recorded jobs on {args.total_nodes} nodes; '
          f'{args.policy}, R={reservations}, Q={args.queue_depth}; '
          f'Fiction scheduler={args.scheduler_backend}', flush=True)
    try:
        transitions = run_case(case, jobs, 'hybrid' if args.policy == 'hybrid' else 'fcfs',
                               args.queue_depth, reservations or None, timeout=args.timeout,
                               total_nodes=args.total_nodes,
                               scheduler_backend=args.scheduler_backend,
                               match_policy='hinodex' if args.resource_model == 'placement' and args.placement_order == 'high' else 'lonodex')
    except Exception as error:
        manifest.update(status='incomplete', error=str(error))
        (root / 'workload.json').write_text(json.dumps(manifest, indent=2) + '\n')
        raise
    reference = reference_schedule(transitions, jobs, args.total_nodes, time_precision=3)
    run = Path((case / 'run-directory.txt').read_text().strip())
    native, count = native_profile(run / 'output/resource-manager-snapshots.jsonl', jobs,
                                   args.total_nodes, time_precision=3)
    dr_curve = observed_profile(case / 'dr_evt-resources.csv', args.total_nodes)
    assert native == profile(reference, args.total_nodes), 'Native manager/job discrepancy'
    assert dr_curve == profile(observed, args.total_nodes), 'DR_EVT manager/job discrepancy'
    expected = sum(job[1] * job[3] for job in jobs)
    # Each job's rounded start/end can change its interval by at most 1 ms.
    rounding_bound = 0.001 * sum(job[1] for job in jobs)
    for curve in (native, dr_curve):
        assert math.isclose(area(curve), expected, rel_tol=1e-9, abs_tol=rounding_bound)
    export_curves(case, dr_curve, native, False, args.total_nodes)
    entry = {'case': f'{args.trace.stem}_jobs_{len(jobs)}', 'jobs': jobs,
             'workload_label': selected_period['label'], 'source_period': period,
             'selected_source_period': selected_period,
             'backfill_policy': args.policy, 'reservation_depth': reservations,
             'fiction_scheduler': args.scheduler_backend,
             'fiction_match_policy': ('native resource-pool allocation' if
                                      args.scheduler_backend == 'sched-simple' else
                                      'hinodex' if args.resource_model == 'placement' and args.placement_order == 'high' else 'lonodex'),
             'reference_queue_depth': args.queue_depth,
             'per_pass_accounting': args.resource_model == 'capacity',
             'dr_evt_resource_model': args.resource_model,
             'dr_evt_schedule': observed, 'reference_schedule': reference}
    # The plotter expects the case-directory name derived from these policies.
    capture = {'case': label, 'native_duplicate_owners': 0,
               'reference_replay_differing_jobs': [],
               'dr_evt_schedule': observed, 'reference_schedule': reference}
    metrics = plot_case(entry, capture, root, args.total_nodes)
    metrics['resource_snapshot_count'] = count
    metrics['source_area_node_seconds'] = expected
    metrics['output_rounding_area_bound_node_seconds'] = rounding_bound
    metrics['process_runtime_seconds'] = {
        name: json.loads((case / f'{name}-process-timing.json').read_text())['elapsed_seconds']
        for name in ('dr_evt', 'fiction')}
    (root / 'comparison-metrics.json').write_text(json.dumps(metrics, indent=2) + '\n')
    manifest.update(status='completed', differing_jobs=sum(a != b for a, b in zip(observed, reference)))
    (root / 'workload.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (root / 'paired-report.json').write_text(json.dumps(entry, indent=2) + '\n')
    for name in ('validate_lassen.py', 'plot_comparison.py', 'test_paired.py', 'fiction_runner.py'):
        (root / name).write_bytes(Path(__file__).with_name(name).read_bytes())
    print('Completed:', manifest['differing_jobs'], 'job schedules differ;',
          count, 'resource snapshots; plots:', case, flush=True)


if __name__ == '__main__':
    main()
