"""Reconstruct the first chronological license-enabled start difference."""
import argparse
import bisect
import gzip
import json
import re
from datetime import datetime, timezone
from pathlib import Path
import shutil


class Timeline:
    def __init__(self, segments):
        self.segments = sorted(segments.items())

    def earliest(self, nodes, duration, start):
        candidates = [start] + [t for t, _ in self.segments if t > start]
        for begin in candidates:
            index = bisect.bisect_right(self.segments, (begin, float('inf'))) - 1
            relevant = [self.segments[index][1]]
            relevant += [n for t, n in self.segments[index + 1:] if t < begin + duration]
            if min(relevant) >= nodes:
                return begin
        return None

    def reserve(self, start, nodes, duration):
        for time in (start, start + duration):
            index = bisect.bisect_left(self.segments, (time, -1))
            if index == len(self.segments) or self.segments[index][0] != time:
                self.segments.insert(index, (time, self.segments[index - 1][1]))
        self.segments = [(t, n - nodes if start <= t < start + duration else n)
                         for t, n in self.segments]


def reconstruct_pass(report, now, pending):
    """Independent count-timeline replay of the documented DR_EVT pass rule."""
    jobs = report['jobs']
    running = [(i, row) for i, row in enumerate(report['dr_evt_schedule'])
               if row['start'] < now < row['end']]
    free = report['capacity'] - sum(row['nodes'] for _, row in running)
    releases = {}
    for index, row in running:
        end = row['start'] + jobs[index][2]
        if end > now:
            releases[end] = releases.get(end, 0) + row['nodes']
    segments = {now: free}
    balance = free
    for end, count in sorted(releases.items()):
        balance += count
        segments[end] = balance
    protection = Timeline(segments)
    count = 0
    reservations = []
    accepted = []

    def protect(job_id, begin=None):
        nonlocal count
        if count >= report['reservation_depth']:
            return
        _, nodes, limit, _ = jobs[job_id - 1]
        begin = protection.earliest(nodes, limit, now) if begin is None else begin
        if begin is None or begin > now + 259200:
            return
        protection.reserve(begin, nodes, limit)
        count += 1
        reservations.append({'job': job_id, 'start': begin, 'end': begin + limit, 'nodes': nodes})

    protect(pending[0])
    for job_id in pending[1:report['backfill_queue_depth']]:
        _, nodes, limit, _ = jobs[job_id - 1]
        if nodes > free:
            protect(job_id)
            continue
        begin = protection.earliest(nodes, limit, now)
        if begin == now:
            accepted.append(job_id)
            protection.reserve(now, nodes, limit)
            free -= nodes
        else:
            protect(job_id, begin)
    return accepted, reservations


def diagnose(directory, output):
    report = json.loads((directory / 'report.json').read_text())
    assert report['bf_licenses'] is True
    jobs = report['jobs']
    slurm, dr = report['slurm_schedule'], report['dr_evt_schedule']
    now, index = min((min(a['start'], b['start']), i)
                     for i, (a, b) in enumerate(zip(slurm, dr)) if a['start'] != b['start'])
    interval = report['backfill_interval_seconds']
    previous = now - interval
    assert slurm[index]['start'] == now < dr[index]['start']
    # The first decision is examined before differing starts alter either state.
    for a, b in zip(slurm, dr):
        if min(a['start'], b['start']) < now:
            assert (a['start'], a['end']) == (b['start'], b['end'])
    queue = lambda time: [i + 1 for i, (job, row) in enumerate(zip(jobs, dr))
                          if job[0] <= time and row['start'] >= time]
    before, after = queue(previous), queue(now)
    starts = [i + 1 for i, row in enumerate(dr) if row['start'] == previous]
    events = [{'time': row['end'], 'job': i + 1, 'kind': 'completion'}
              for i, row in enumerate(dr) if previous < row['end'] <= dr[index]['start']]
    events += [{'time': job[0], 'job': i + 1, 'kind': 'arrival'}
               for i, job in enumerate(jobs) if previous < job[0] <= dr[index]['start']]
    events.sort(key=lambda event: event['time'])
    assert not any(event['time'] <= now for event in events)
    assert before.index(index + 1) + 1 > report['backfill_queue_depth']
    assert after.index(index + 1) + 1 <= report['backfill_queue_depth']
    accepted, reservations = reconstruct_pass(report, now, after)
    previous_accepted, _ = reconstruct_pass(report, previous, before)
    assert previous_accepted == starts
    assert index + 1 in accepted
    stamps = {datetime.fromtimestamp(1597836579 + time, timezone.utc).strftime('%Y-%m-%dT%H:%M:%S')
              for time in (previous, now)}
    log_path = directory / 'controller.log.gz'
    native_evidence = []
    if log_path.exists():
        related = re.compile(r'JobId=(' + '|'.join(map(str, starts + [index + 1])) + r')(?:\D|$)')
        with gzip.open(log_path, 'rt') as stream:
            for line in stream:
                if line[1:20] in stamps and related.search(line):
                    native_evidence.append(line.strip())
                if line.startswith('[') and line[1:20] > max(stamps):
                    break
    observed_indices = [index] + [event['job'] - 1 for event in events if event['kind'] == 'completion']
    result = {'first_chronological_difference': {'job': index + 1, 'slurm_start': now,
                                                'dr_evt_start': dr[index]['start'], 'workload': jobs[index]},
              'previous_tick': previous, 'interval': interval,
              'queue_depth': report['backfill_queue_depth'],
              'queue_before_previous_tick': before, 'queue_before_first_difference_tick': after,
              'previous_tick_starts': starts, 'next_state_events': events,
              'native_log_evidence': native_evidence,
              'candidate_rank_before': before.index(index + 1) + 1,
              'candidate_rank_after': after.index(index + 1) + 1,
              'reconstructed_dr_evt_pass_at_skipped_tick': {'accepted_jobs': accepted, 'reservations': reservations},
              'observed_schedules': {name: [{'job': i + 1, **rows[i]} for i in observed_indices]
                                     for name, rows in (('slurm', slurm), ('dr_evt', dr))},
              'source_evidence': {
                  'dr_evt': 'src/sim/sim.cpp: periodic_evaluation uses m_periodic_backfill_dirty; it is cleared before the pass and is not set by starts in that pass',
                  'slurm': 'src/plugins/sched/backfill/backfill.c: _more_work accepts last_job_update >= last_backfill_time',
                  'replay_rule': 'src/sim/scheduler_fcfs_hybrid_circular.tpp: bounded Q scan with resource-time protection'},
              'conclusion': 'Starts at the previous tick move the candidate into Q, but DR_EVT skips the next tick until an arrival or completion marks the state dirty',
              'limit': 'This identifies the first chronological divergence; it does not isolate every later difference'}
    output.mkdir(parents=True, exist_ok=True)
    (output / 'first-divergence-data.json').write_text(json.dumps(result, indent=2) + '\n')
    script = output / 'diagnose_first_divergence.py'
    if Path(__file__).resolve() != script.resolve():
        shutil.copyfile(__file__, script)
    print(json.dumps({key: result[key] for key in ('first_chronological_difference',
                       'candidate_rank_before', 'candidate_rank_after', 'previous_tick_starts', 'conclusion')}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    diagnose(args.directory, args.output)
