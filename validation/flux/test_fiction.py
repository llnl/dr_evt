"""Smoke-test Fiction's real Flux backend and clock with hand-computed starts."""
import json
from pathlib import Path

from fiction_runner import EPOCH, run_case

ROOT = Path('/results/fiction')
ROOT.mkdir(exist_ok=True)
cases = {
    'single': ([(0, 1, 20)], [0]),
    'backfill': ([(0, 3, 120), (6, 4, 20), (6, 1, 20)], [0, 120, 6]),
}
report = []
for name, (jobs, starts) in cases.items():
    case = ROOT / name
    case.mkdir(exist_ok=True)
    transitions = run_case(case, [(arrival, nodes, duration, duration)
                                  for arrival, nodes, duration in jobs], 'easy')
    assert len(transitions) == len(jobs), (name, transitions)
    for row, (arrival, nodes, duration), start in zip(transitions, jobs, starts):
        actual = [float(row[key]) - EPOCH for key in ('SUBMIT', 'START', 'FINISH')]
        assert actual == [arrival, start, start + duration], (name, actual, start, row)
        assert int(row['nnodes']) == nodes, (name, row)
        assert len(row['NODELIST'].split(',')) == nodes, (name, row)
    report.append({'case': name, 'expected_start_offsets': starts, 'transitions': transitions})
    print(f'PASS Fiction {name}: start offsets={starts}', flush=True)
ROOT.joinpath('report.json').write_text(json.dumps(report, indent=2) + '\n')
