"""Reconstruct capacity at the first remaining license-enabled divergence."""
import argparse
import json
from pathlib import Path
import shutil

from diagnose_first_divergence import Timeline, reconstruct_pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('result', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    report = json.loads((args.result / 'report.json').read_text())
    assert report['bf_licenses'] is True
    native, observed = report['slurm_schedule'], report['dr_evt_schedule']
    now, index = min((min(a['start'], b['start']), i)
                     for i, (a, b) in enumerate(zip(native, observed)) if a['start'] != b['start'])
    for a, b in zip(native, observed):
        if min(a['start'], b['start']) < now:
            assert (a['start'], a['end']) == (b['start'], b['end'])
    pending = [i + 1 for i, (job, row) in enumerate(zip(report['jobs'], observed))
               if job[0] <= now and row['start'] >= now]
    accepted, reservations = reconstruct_pass(report, now, pending)
    candidate = index + 1
    assert candidate in accepted
    running = [(i, row) for i, row in enumerate(observed) if row['start'] < now < row['end']]
    free = report['capacity'] - sum(row['nodes'] for _, row in running)
    releases = {}
    for i, row in running:
        end = row['start'] + report['jobs'][i][2]
        releases[end] = releases.get(end, 0) + row['nodes']
    segments = {now: free}
    for end, nodes in sorted(releases.items()):
        free += nodes
        segments[end] = free
    protection = Timeline(segments)
    for row in reservations:
        protection.reserve(row['start'], row['nodes'], row['end'] - row['start'])
    for job in accepted:
        if job == candidate:
            break
        workload = report['jobs'][job - 1]
        protection.reserve(now, workload[1], workload[2])
    workload = report['jobs'][index]
    end = now + workload[2]
    assert protection.earliest(workload[1], workload[2], now) == now
    assert protection.earliest(workload[1], workload[2] + 1, now) != now
    conflicts = [row for row in reservations if row['start'] == end]
    assert conflicts
    data = {
        'kind': 'reservation_endpoint',
        'first_chronological_difference': {'job': candidate, 'slurm_start': native[index]['start'],
                                         'dr_evt_start': observed[index]['start'], 'workload': workload},
        'candidate_queue_rank': pending.index(candidate) + 1,
        'planned_end': end, 'protected_capacity_before_candidate': protection.segments,
        'reservations_at_candidate_end': conflicts,
        'replayed_accepted_jobs': accepted, 'replayed_reservations': reservations,
        'observed_schedules': {'slurm': native[index], 'dr_evt': observed[index]},
        'previous_divergence_fixed': report['retry_verification']['job243'],
        'source_evidence': {
            'slurm': 'src/plugins/sched/backfill/backfill.c:3285 checks segments with begin_time <= end_time',
            'dr_evt': 'The resource-time replay checks capacity over [start, end), excluding a reservation beginning exactly at end'},
        'limit': 'Identifies the first remaining boundary-rule difference, not every later difference',
    }
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'first-divergence-data.json').write_text(json.dumps(data, indent=2) + '\n')
    for name in ('diagnose_reservation_boundary.py', 'diagnose_first_divergence.py'):
        shutil.copy2(Path(__file__).with_name(name), args.output / name)
    print(json.dumps(data['first_chronological_difference'], indent=2))


if __name__ == '__main__':
    main()
