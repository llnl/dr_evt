"""Independently audit a completed native Slurm license-pool experiment."""
import argparse
import csv
import gzip
import json
from pathlib import Path


def audit(directory):
    report = json.loads((directory / 'report.json').read_text())
    jobs = report['jobs']
    schedule = report['slurm_schedule']
    capacity = report['capacity']
    for engine in ('slurm', 'dr_evt'):
        rows = report[engine + '_schedule']
        assert len(jobs) == len(rows)
        for job, row in zip(jobs, rows):
            assert row['start'] >= job[0]
            assert row['end'] - row['start'] == job[3]
            assert row['nodes'] == job[1]
    expected_area = sum(job[1] * job[3] for job in jobs)
    for engine in ('slurm', 'dr_evt'):
        curve = report['resource_curves'][engine]
        area = sum((end[0] - begin[0]) * begin[1]
                   for begin, end in zip(curve, curve[1:]))
        assert area == expected_area
        assert all(0 <= count <= capacity for _, count in curve)
    dr_samples = {}
    with (directory / 'dr_evt/resources.csv').open() as stream:
        for row in csv.DictReader(stream):
            count = int(row['allocated_nodes'])
            assert count + int(row['free_nodes']) == capacity
            dr_samples[float(row['time'])] = count
    dr_curve = sorted(dr_samples.items())
    dr_area = sum((end[0] - begin[0]) * begin[1]
                  for begin, end in zip(dr_curve, dr_curve[1:]))
    assert dr_area == expected_area
    for now, count in dr_curve:
        expected = sum(row['nodes'] for row in report['dr_evt_schedule']
                       if row['start'] <= now < row['end'])
        assert count == expected
    path = directory / 'snapshots.jsonl'
    opener = path.open
    if not path.exists():
        path = path.with_suffix('.jsonl.gz')
        opener = lambda mode: gzip.open(path, mode)
    seen = {}
    frames = 0
    area = 0
    previous = None
    with opener('rt') as stream:
        for line in stream:
            frame = json.loads(line)
            now = frame['time']
            native = frame['native']
            running = [row for row in native['jobs'] if row['state'] & 255 == 1]
            allocated = sum(int(row['licenses'].split(':')[1]) for row in running)
            assert allocated == frame['allocated_node_equivalents'] == native['license_used']
            assert native['license_total'] == capacity and allocated <= capacity
            assert len({row['assignment'] for row in running}) == len(running)
            expected = sum(row['nodes'] for row in schedule if row['start'] <= now < row['end'])
            assert allocated == expected, (now, allocated, expected)
            for row in native['jobs']:
                if row['state'] & 255 in (1, 3):
                    if row['id'] in seen:
                        assert seen[row['id']]['start'] == row['start']
                    seen[row['id']] = row
            if previous:
                assert now > previous['time']
                area += (now - previous['time']) * previous['allocated_node_equivalents']
            previous = frame
            frames += 1
    assert area == expected_area
    completed = sorted(seen.values(), key=lambda row: row['id'])
    assert len(completed) == len(jobs)
    for index, (native, row) in enumerate(zip(completed, schedule)):
        assert native['state'] & 255 == 3
        assert native['assignment'] == row['assignment'] == f'n{index:04d}'
        assert native['end'] - native['start'] == row['end'] - row['start']
        assert native['start'] - completed[0]['start'] == row['start'] - schedule[0]['start']
    result = {'status': 'passed', 'completed_jobs': len(completed), 'snapshots': frames,
              'runtime_node_seconds': expected_area, 'native_snapshot_node_seconds': area,
              'dr_evt_resource_trace_node_seconds': dr_area,
              'checks': ['arrival constraints', 'exact runtimes', 'license capacity',
                         'private node ownership', 'native states and start times',
                         'snapshot allocation equals job schedule', 'both resource curve areas',
                         'DR_EVT resource trace equals job schedule']}
    (directory / 'audit.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    audit(parser.parse_args().directory)
