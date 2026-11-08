"""Plot saved queue-window and start-time evidence for the first divergence."""
import argparse
import json
from pathlib import Path
import shutil

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def plot(path):
    data = json.loads(path.read_text())
    first = data['first_chronological_difference']
    candidate = first['job']
    fig, (queue, timing) = plt.subplots(1, 2, figsize=(14, 5.7), gridspec_kw={'width_ratios': [1, 1.35]})
    fig.suptitle(f'First chronological divergence with bf_licenses on: job {candidate}', fontsize=15)
    depth = data['queue_depth']
    queue.axhspan(depth - 7.5, depth + .5, color='#e8f0f8')
    queue.axhline(depth + .5, color='#777777', linestyle='--', linewidth=1)
    for x, name in enumerate(('queue_before_previous_tick', 'queue_before_first_difference_tick')):
        for rank, job in enumerate(data[name], 1):
            if depth - 5 <= rank <= depth + 3:
                color = '#a52b72' if job == candidate else '#526879'
                queue.scatter(x, rank, s=550 if job == candidate else 430, color=color, zorder=3)
                queue.text(x, rank, str(job), ha='center', va='center', color='white', fontsize=9, zorder=4)
    queue.annotate('', xy=(1, data['candidate_rank_after']), xytext=(0, data['candidate_rank_before']),
                   arrowprops={'arrowstyle': '->', 'color': '#a52b72', 'lw': 2})
    queue.set(xlim=(-.6, 1.6), ylim=(depth + 3.8, depth - 5.8), ylabel='FCFS queue position',
              title=f'Two starts move job {candidate} into Q={depth}')
    queue.set_xticks([0, 1], [f'Before the {data["previous_tick"]:g} s pass', 'After that pass'])
    queue.set_yticks(range(depth - 5, depth + 4))
    queue.text(.5, depth + 3.55, f'Jobs {" and ".join(map(str, data["previous_tick_starts"]))} leave the queue',
               ha='center', fontsize=10)
    queue.text(-.56, depth + .28, 'Q cutoff', fontsize=9, color='#555555')
    rows = [('slurm', 1, '#b35806', 'Slurm'), ('dr_evt', 0, '#2166ac', 'DR_EVT')]
    for name, y, color, label in rows:
        row = next(row for row in data['observed_schedules'][name] if row['job'] == candidate)
        timing.barh(y, row['end'] - row['start'], left=row['start'], height=.36, color=color)
        timing.text((row['start'] + row['end']) / 2, y, f'Job {candidate}: {first["workload"][3]} s actual runtime',
                    ha='center', va='center', color='white', fontsize=9)
        timing.text(row['end'] + 8, y, f'{row["end"]:g}', va='center', fontsize=9)
    now = first['slurm_start']
    timing.scatter(now, 0, marker='x', color='#a52b72', s=75, zorder=5)
    timing.annotate('5820 s: DR_EVT skips the tick\nDirty flag was cleared before the prior pass',
                    xy=(now, 0), xytext=(now + 175, -.62), fontsize=9,
                    arrowprops={'arrowstyle': '->', 'color': '#a52b72'})
    event = data['next_state_events'][0]
    timing.annotate(f'{event["time"]:g} s: job {event["job"]} completes\n5850 s: DR_EVT runs backfill and starts job 243',
                    xy=(first['dr_evt_start'], .18), xytext=(now + 175, .55), fontsize=9,
                    arrowprops={'arrowstyle': '->', 'color': '#2166ac'})
    timing.annotate('Slurm starts at 5820 s', xy=(now, 1.18), xytext=(now + 140, 1.55), fontsize=10,
                    arrowprops={'arrowstyle': '->', 'color': '#b35806'})
    timing.set(xlim=(now - 90, now + 730), ylim=(-.85, 1.85), xlabel='Simulation time (seconds)',
               title='Same capacity and waiting queue, different retry ticks')
    timing.set_yticks([0, 1], ['DR_EVT', 'Slurm'])
    timing.grid(axis='x', alpha=.2)
    fig.text(.5, .035, 'At 5820 s there is no new arrival or completion. A reconstructed DR_EVT pass would admit job 243.\n'
             'Slurm retries the changed queue; DR_EVT waits for the next state event. This isolates the first difference, not every later one.',
             ha='center', fontsize=10)
    fig.subplots_adjust(left=.06, right=.98, top=.84, bottom=.2, wspace=.25)
    for extension in ('pdf', 'svg', 'png'):
        fig.savefig(path.with_name('first-divergence.' + extension), dpi=180)
    plt.close(fig)
    target = path.with_name('plot_first_divergence.py')
    if Path(__file__).resolve() != target.resolve():
        shutil.copyfile(__file__, target)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('data', type=Path)
    plot(parser.parse_args().data)
