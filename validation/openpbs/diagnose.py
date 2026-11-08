"""Reconstruct reservations at the first differing start, without replaying PBS."""
import argparse
import csv
import json
from pathlib import Path


def feasible(intervals, at, units, duration, capacity):
    events = {at: 0}
    for start, end, amount in intervals:
        if end <= at or start >= at + duration:
            continue
        events[max(at, start)] = events.get(max(at, start), 0) + amount
        events[end] = events.get(end, 0) - amount
    allocated = 0
    for timestamp, delta in sorted(events.items()):
        if timestamp >= at + duration:
            break
        allocated += delta
        if allocated + units > capacity:
            return False
    return True


def diagnose(path):
    report = json.loads(path.read_text())
    jobs = json.loads((path.parent / 'workload.json').read_text())
    native, dr = report['openpbs_schedule'], report['dr_evt_schedule']
    timestamp = min(min(a[0], b[0]) for a, b in zip(native, dr) if a != b)
    capacity = report['capacity']
    active = {i + 1: [timestamp, start + jobs[i][2], jobs[i][1]]
              for i, (start, end) in enumerate(native) if start < timestamp < end}
    completed = {i + 1 for i, (_, end) in enumerate(native) if end <= timestamp}
    started = set(active) | completed
    intervals = list(active.values())
    native_plan = {}
    with (path.parent / 'native-events.csv').open() as stream:
        for kind, at, identity, value in csv.reader(stream):
            if int(at) > timestamp:
                break
            if kind == 'plan' and int(at) == timestamp:
                native_plan[int(identity.split('.')[0])] = int(value)
    reconstructed = {}
    selected = []
    for identity, (submit, units, limit, _) in enumerate(jobs, 1):
        if submit > timestamp or identity in started:
            continue
        candidates = sorted({timestamp} | {end for _, end, _ in intervals if end > timestamp})
        at = next(t for t in candidates if feasible(intervals, t, units, limit, capacity))
        if at == timestamp:
            selected.append(identity)
            intervals.append([at, at + limit, units])
        elif len(reconstructed) < report['reservation_depth']:
            reconstructed[identity] = at
            intervals.append([at, at + limit, units])
    assert selected == [i + 1 for i, (start, _) in enumerate(dr) if start == timestamp]
    differences = [{'job_id': i, 'reconstructed_dr_evt': at, 'openpbs': native_plan.get(i)}
                   for i, at in reconstructed.items() if native_plan.get(i) != at]
    native_selected = [i + 1 for i, (start, _) in enumerate(native) if start == timestamp]
    dr_intervals = list(active.values()) + [[at, at + jobs[i - 1][2], jobs[i - 1][1]]
                                          for i, at in reconstructed.items()]
    native_intervals = list(active.values()) + [[at, at + jobs[i - 1][2], jobs[i - 1][1]]
                                              for i, at in native_plan.items()]
    candidate_checks = []
    for i in native_selected:
        _, units, limit, _ = jobs[i - 1]
        candidate_checks.append({'job_id': i, 'units': units, 'requested_seconds': limit,
                                 'fits_reconstructed_dr_evt_plan': feasible(dr_intervals, timestamp, units, limit, capacity),
                                 'fits_native_plan': feasible(native_intervals, timestamp, units, limit, capacity)})
    releases = [{'job_id': i, 'units': interval[2], 'projected_release': interval[1]}
                for i, interval in active.items()
                if any(d['reconstructed_dr_evt'] <= interval[1] <= d['openpbs'] for d in differences)]
    diagnosis = {'first_differing_time': timestamp, 'openpbs_starts': native_selected,
                 'dr_evt_starts': selected, 'free_capacity_before_dispatch': capacity - sum(x[2] for x in active.values()),
                 'reservation_differences': differences, 'adjacent_projected_releases': releases,
                 'candidate_fit_checks': candidate_checks,
                 'method': 'Independent half-open capacity sweep reconstructed from the common running state, requested walltimes, FCFS queue and protection depth; native forecasts read from the same replay.',
                 'native_source': 'src/scheduler/simulate.cpp: SIM_NEXT_EVENT uses a minimum window of one second and processes events through the window endpoint, including with opt_backfill_fuzzy=off.'}
    (path.parent / 'first-divergence.json').write_text(json.dumps(diagnosis, indent=2) + '\n')
    return diagnosis


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    print(json.dumps(diagnose(parser.parse_args().report), indent=2))
