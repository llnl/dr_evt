"""Time DR_EVT against completed native Slurm and OpenPBS comparison runs.

Reads report.json files under OUT/{slurm,openpbs}-{capacity,placement}-*/ and
reruns the exact DR_EVT command saved by the first run of each case, with one
untimed warmup and N timed runs. Each rerun's output is compared with the saved
output. Writes OUT/summary.json and prints one line per case.

Usage: python3 time_native_comparison.py OUT [N]
"""
import glob
import json
import statistics as st
import subprocess
import sys
import time
from pathlib import Path


def first(out, pattern):
    matches = sorted(glob.glob(str(out / pattern)))
    if not matches:
        raise SystemExit('missing ' + pattern)
    return Path(matches[0])


def time_capacity(out, command, tag, runs):
    reference = Path(command[command.index('--outfile') + 1]).read_bytes()
    folder = out / 'dr_evt-reruns' / tag
    folder.mkdir(parents=True, exist_ok=True)
    command = list(command)
    command[command.index('--outfile') + 1] = str(folder / 'schedule.csv')
    command[command.index('--resource_trace') + 1] = str(folder / 'resources.csv')
    times, same = [], True
    for run in range(runs + 1):
        started = time.perf_counter()
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        elapsed = time.perf_counter() - started
        same &= (folder / 'schedule.csv').read_bytes() == reference
        if run:
            times.append(elapsed)
    return times, same


def time_placement(folder, runs):
    command = json.loads((folder / 'command.json').read_text())
    request = (folder / 'request.txt').read_text()
    reference = (folder / 'stdout.jsonl').read_text()
    times, same = [], True
    for run in range(runs + 1):
        started = time.perf_counter()
        result = subprocess.run(command, input=request, text=True, capture_output=True, check=True)
        elapsed = time.perf_counter() - started
        same &= result.stdout == reference
        if run:
            times.append(elapsed)
    return times, same


def native(out, case):
    seconds, differences = [], []
    for path in sorted(glob.glob(str(out / f'{case}-*/lassen-*/report.json'))):
        report = json.loads(Path(path).read_text())
        if 'slurm_metrics' in report:
            seconds.append(report['slurm_metrics']['process_elapsed_seconds'])
            differences.append(report['different_start_times'])
        else:
            seconds.append(report['openpbs_wall_seconds'])
            differences.append(report['different_jobs'])
    return seconds, differences


def main():
    out = Path(sys.argv[1])
    runs = int(sys.argv[2]) if len(sys.argv) > 2 else 10
    # The first completed run of each case supplies the DR_EVT command.
    slurm_capacity = first(out, 'slurm-capacity-*/lassen-*/dr_evt/command.json')
    openpbs_capacity = first(out, 'openpbs-capacity-*/lassen-*/commands.json')
    dr_evt = {
        'slurm-capacity': time_capacity(
            out, json.loads(slurm_capacity.read_text()), 'slurm-capacity', runs),
        'slurm-placement': time_placement(
            first(out, 'slurm-placement-*/lassen-*/dr_evt'), runs),
        'openpbs-capacity': time_capacity(
            out, json.loads(openpbs_capacity.read_text())['dr_evt'], 'openpbs-capacity', runs),
        'openpbs-placement': time_placement(
            first(out, 'openpbs-placement-*/lassen-*/dr_evt-placement'), runs),
    }
    summary = {}
    for case, (times, same) in dr_evt.items():
        seconds, differences = native(out, case)
        summary[case] = {'native_seconds': seconds, 'schedule_differences': differences,
                         'dr_evt_seconds': times, 'dr_evt_output_matches_saved': same}
        print(f'{case:18s} native n={len(seconds)} mean={st.mean(seconds):8.2f}s '
              f'diffs={differences} | dr_evt n={len(times)} median={st.median(times):.4f}s | '
              f'speedup={st.mean(seconds) / st.median(times):,.0f}x | same_output={same}')
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    main()
