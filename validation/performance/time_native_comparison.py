"""Time DR_EVT against completed native Slurm and OpenPBS comparison runs.

Two steps, both reading OUT/{slurm,openpbs}-{capacity,placement}-*/:

``run`` times the DR_EVT simulator on the same workload and settings as the
native runs, with one untimed warmup and N timed runs per case, and writes
OUT/dr_evt-timing.json. It does not read native times.

``summarize`` reads every completed report and prints one line per case with
native and DR_EVT times; it runs nothing and writes OUT/summary.json. Each
report records one DR_EVT time; for placement that time comes from the test
driver, which also writes reservation snapshots. When OUT/dr_evt-timing.json
exists, ``summarize`` uses its plain-simulator times instead. The ``dr_evt[...]``
label shows the source: ``harness``, ``driver``, or ``simulator``.

Capacity cases rerun the simulator command saved by the first completed run;
each output must match the saved schedule byte for byte. The harnesses ran
placement through a test driver that also writes reservation snapshots, so
placement cases instead run the saved capacity command of the same scheduler
with the driver's settings: ``--resource_model placement --placement_order
low``, without the capacity-only planning and per-pass options. Each schedule's
start and end times must match the DR_EVT schedule in the placement report.
The simulator does not write node IDs, so those are not compared here; the
harness reports already compare them with the native scheduler.

Usage:
  python3 time_native_comparison.py run OUT [--runs N]
  python3 time_native_comparison.py summarize OUT
"""
import argparse
import csv
import glob
import json
import statistics as st
import subprocess
import time
from pathlib import Path


def first(out, pattern):
    matches = sorted(glob.glob(str(out / pattern)))
    if not matches:
        raise SystemExit('missing ' + pattern)
    return Path(matches[0])


def schedule_times(path):
    with path.open() as stream:
        return [[float(row['begin_time']), float(row['end_time'])]
                for row in csv.DictReader(stream)]


# Capacity options that placement rejects. The placement driver leaves the
# corresponding settings at their defaults: exact planning times and no
# per-pass accounting.
CAPACITY_ONLY = {'--per_pass_accounting': 0, '--planning_time_resolution': 1,
                 '--planning_horizon': 1}


def placement_command(command):
    """Convert a saved capacity command to the placement driver's settings."""
    result, skip = [], 0
    for arg in command:
        if skip:
            skip -= 1
        elif arg in CAPACITY_ONLY:
            skip = CAPACITY_ONLY[arg]
        else:
            result.append(arg)
    return result + ['--resource_model', 'placement', '--placement_order', 'low']


def time_simulator(out, command, saved_in, tag, runs, model='capacity', expected=None):
    """Time one simulator command and check every output.

    Without ``expected``, each schedule must equal the saved one byte for byte.
    With ``expected``, each schedule's start/end times must equal it.
    """
    # Saved paths name the folder the run was written to; follow a renamed folder.
    original = str(Path(command[command.index('--outfile') + 1]).parent)
    command = [str(saved_in) + arg[len(original):] if arg.startswith(original) else arg
               for arg in command]
    reference = Path(command[command.index('--outfile') + 1]).read_bytes()
    folder = out / 'dr_evt-reruns' / tag
    folder.mkdir(parents=True, exist_ok=True)
    output = folder / 'schedule.csv'
    command[command.index('--outfile') + 1] = str(output)
    command[command.index('--resource_trace') + 1] = str(folder / 'resources.csv')
    if model == 'placement':
        command = placement_command(command)
    (folder / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    times, same = [], True
    for run in range(runs + 1):
        started = time.perf_counter()
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        elapsed = time.perf_counter() - started
        if expected is None:
            same &= output.read_bytes() == reference
        else:
            same &= schedule_times(output) == expected
        if run:
            times.append(elapsed)
    return times, same


def report_schedule(path):
    """Return the DR_EVT start/end times recorded in a placement report."""
    rows = json.loads(path.read_text())['dr_evt_schedule']
    return [[float(row['start']), float(row['end'])] if isinstance(row, dict)
            else [float(row[0]), float(row[1])] for row in rows]


def native(out, case):
    """Return native times, schedule differences, and harness DR_EVT times."""
    seconds, differences, dr_evt = [], [], []
    for path in sorted(glob.glob(str(out / f'{case}-*/lassen-*/report.json'))):
        report = json.loads(Path(path).read_text())
        if 'slurm_metrics' in report:
            seconds.append(report['slurm_metrics']['process_elapsed_seconds'])
            differences.append(report['different_start_times'])
            dr_evt.append(report['dr_evt_metrics']['process_elapsed_seconds'])
        else:
            seconds.append(report['openpbs_wall_seconds'])
            differences.append(report['different_jobs'])
            dr_evt.append(report['dr_evt_wall_seconds'])
    return seconds, differences, dr_evt


TIMING = 'dr_evt-timing.json'


def run(out, runs):
    """Time DR_EVT for every case and write OUT/dr_evt-timing.json."""
    # The first completed capacity run of each scheduler supplies the command.
    slurm = first(out, 'slurm-capacity-*/lassen-*/dr_evt/command.json')
    openpbs = first(out, 'openpbs-capacity-*/lassen-*/commands.json')
    slurm_command = json.loads(slurm.read_text())
    openpbs_command = json.loads(openpbs.read_text())['dr_evt']
    cases = {
        'slurm-capacity': lambda: time_simulator(
            out, slurm_command, slurm.parent, 'slurm-capacity', runs),
        'slurm-placement': lambda: time_simulator(
            out, slurm_command, slurm.parent, 'slurm-placement', runs, 'placement',
            report_schedule(first(out, 'slurm-placement-*/lassen-*/report.json'))),
        'openpbs-capacity': lambda: time_simulator(
            out, openpbs_command, openpbs.parent, 'openpbs-capacity', runs),
        'openpbs-placement': lambda: time_simulator(
            out, openpbs_command, openpbs.parent, 'openpbs-placement', runs, 'placement',
            report_schedule(first(out, 'openpbs-placement-*/lassen-*/report.json'))),
    }
    timing = {}
    for case, measure in cases.items():
        times, same = measure()
        timing[case] = {'dr_evt_seconds': times, 'dr_evt_schedule_matches': same,
                        'command': str(out / 'dr_evt-reruns' / case / 'command.json')}
        print(f'{case:18s} dr_evt n={len(times)} median={st.median(times):.4f}s '
              f'schedule_match={same}', flush=True)
    (out / TIMING).write_text(json.dumps(timing, indent=2) + '\n')


CASES = ('slurm-capacity', 'slurm-placement', 'openpbs-capacity', 'openpbs-placement')


def summarize(out):
    """Summarize completed runs; prefer DR_EVT times from the run step.

    Every harness report already records one DR_EVT time. For placement that
    time comes from the test driver, which also writes reservation snapshots.
    When OUT/dr_evt-timing.json exists, its plain-simulator times are used
    instead and the source column says so.
    """
    path = out / TIMING
    timing = json.loads(path.read_text()) if path.is_file() else {}
    summary = {}
    for case in CASES:
        seconds, differences, harness = native(out, case)
        if not seconds:
            print(f'{case:18s} no completed native runs')
            continue
        if case in timing:
            times, source = timing[case]['dr_evt_seconds'], 'simulator'
            same = timing[case]['dr_evt_schedule_matches']
        else:
            times, same = harness, None
            source = 'driver' if case.endswith('placement') else 'harness'
        summary[case] = {'native_seconds': seconds, 'schedule_differences': differences,
                         'dr_evt_seconds': times, 'dr_evt_source': source,
                         'dr_evt_schedule_matches': same}
        print(f'{case:18s} native n={len(seconds)} mean={st.mean(seconds):8.2f}s '
              f'diffs={differences} | dr_evt[{source}] n={len(times)} '
              f'median={st.median(times):.4f}s | '
              f'speedup={st.mean(seconds) / st.median(times):,.0f}x')
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    steps = parser.add_subparsers(dest='step', required=True)
    timed = steps.add_parser('run', help='time DR_EVT and write dr_evt-timing.json')
    timed.add_argument('out', type=Path)
    timed.add_argument('--runs', type=int, default=10, help='timed runs per case')
    steps.add_parser('summarize', help='combine native and DR_EVT times').add_argument(
        'out', type=Path)
    args = parser.parse_args()
    if args.step == 'run':
        if args.runs < 1:
            parser.error('--runs must be positive')
        run(args.out, args.runs)
    else:
        summarize(args.out)


if __name__ == '__main__':
    main()
