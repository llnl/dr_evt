"""Plot saved capacity and job timing at a reservation endpoint divergence."""
import argparse
import json
from pathlib import Path
import shutil

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('data', type=Path)
    args = parser.parse_args()
    data = json.loads(args.data.read_text())
    first = data['first_chronological_difference']
    job = first['job']
    end = data['planned_end']
    reservation = data['reservations_at_candidate_end'][0]
    fig, (capacity, timing) = plt.subplots(1, 2, figsize=(14, 6))
    fig.suptitle(f'First remaining divergence with bf_licenses on: job {job}', fontsize=15)
    curve = data['protected_capacity_before_candidate']
    capacity.step([t for t, _ in curve], [n for _, n in curve], where='post', color='#526879')
    capacity.axhline(first['workload'][1], color='#a52b72', linestyle='--',
                     label=f'Job {job} needs {first["workload"][1]} node equivalents')
    capacity.axvline(end, color='#b35806', linestyle=':')
    capacity.annotate(f'{end:g} s: job {reservation["job"]} reservation begins\n'
                      f'Job {job} reaches its walltime endpoint',
                      xy=(end, 0), xytext=(end - 650, 12), fontsize=10,
                      arrowprops={'arrowstyle': '->', 'color': '#b35806'})
    capacity.set(xlim=(end - 900, end + 600), ylim=(-1, 36), xlabel='Simulation time (seconds)',
                 ylabel='Unreserved node equivalents', title='Capacity after protecting earlier jobs')
    capacity.legend(loc='upper left', fontsize=9)
    capacity.grid(alpha=.2)
    for name, y, color in (('dr_evt', 0, '#2166ac'), ('slurm', 1, '#b35806')):
        row = data['observed_schedules'][name]
        timing.barh(y, row['end'] - row['start'], left=row['start'], height=.32, color=color)
        timing.text(row['end'] + 35, y, f'{row["start"]:g}–{row["end"]:g} s', va='center', fontsize=10)
    timing.set(xlim=(first['dr_evt_start'] - 100, first['slurm_start'] + first['workload'][3] + 500),
               ylim=(-.7, 1.7), yticks=[0, 1], yticklabels=['DR_EVT', 'Slurm'],
               xlabel='Simulation time (seconds)', title=f'Observed job {job} execution ({first["workload"][3]} s)')
    timing.grid(axis='x', alpha=.2)
    timing.text(.03, .05, f'Submitted walltime: {first["workload"][2]:g} s\n'
                 f'Queue rank: {data["candidate_queue_rank"]}', transform=timing.transAxes, fontsize=10)
    fig.text(.5, .08, 'DR_EVT excludes the walltime endpoint from its capacity check. Slurm includes the segment beginning there.\n'
             'The schedules agree before this decision; this identifies the first remaining difference.',
             ha='center', fontsize=10)
    fig.subplots_adjust(left=.07, right=.97, top=.84, bottom=.25, wspace=.3)
    for extension in ('pdf', 'svg', 'png'):
        fig.savefig(args.data.with_name('first-divergence.' + extension), dpi=180)
    plt.close(fig)
    shutil.copy2(Path(__file__), args.data.with_name(Path(__file__).name))


if __name__ == '__main__':
    main()
