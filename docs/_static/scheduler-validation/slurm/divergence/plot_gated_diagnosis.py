"""Plot saved evidence separating reservation policy from equal-time RPC ordering."""
import json
from pathlib import Path
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def profile(schedule):
    changes = {0: 0}
    for job in schedule:
        changes[job['start']] = changes.get(job['start'], 0) + job['nodes']
        changes[job['end']] = changes.get(job['end'], 0) - job['nodes']
    allocated = 0
    curve = []
    for timestamp, delta in sorted(changes.items()):
        allocated += delta
        curve.append((timestamp, allocated))
    return curve


def main():
    path = Path(sys.argv[1])
    data = json.loads(path.read_text())
    schedules = data['schedules']
    labels = {'protected': 'DR_EVT: protected',
              'diagnostic': 'DR_EVT: no reservations',
              'native': 'Slurm: bf_licenses off, batched RPCs'}
    colors = {'protected': '#2166ac', 'diagnostic': '#318b61', 'native': '#b35806'}
    fig, axes = plt.subplots(2, 2, figsize=(14, 9), constrained_layout=True)
    for name in ('protected', 'native', 'diagnostic'):
        curve = profile(schedules[name])
        axes[0, 0].step([t / 3600 for t, _ in curve], [n for _, n in curve],
                        where='post', label=labels[name], color=colors[name],
                        linestyle='--' if name == 'diagnostic' else '-', alpha=.8)
    axes[0, 0].set(title='Future protection changes the full schedule',
                   xlabel='Time (hours)', ylabel='Allocated node equivalents')
    axes[0, 0].legend(fontsize=8)
    for name in ('protected', 'diagnostic'):
        delta = [a['start'] - b['start'] for a, b in zip(schedules[name], schedules['native'])]
        count = sum(value != 0 for value in delta)
        axes[0, 1].plot(range(1, len(delta) + 1), [value / 3600 for value in delta],
                        '.', markersize=2, color=colors[name],
                        label=f'{labels[name]}: {count:,} differing starts')
    axes[0, 1].set(title='No-reservation reference tests the remaining divergence',
                   xlabel='Job in trace order', ylabel='DR_EVT minus Slurm start (hours)')
    axes[0, 1].legend(fontsize=8)
    selected = (57, 58, 59)
    for offset, name in enumerate(('protected', 'native', 'diagnostic')):
        axes[1, 0].bar([i + (offset - 1) * .25 for i in range(len(selected))],
                       [schedules[name][job - 1]['start'] for job in selected],
                       width=.25, color=colors[name], label=labels[name])
    axes[1, 0].set(xticks=range(len(selected)), xticklabels=[str(job) for job in selected],
                   title='At 13 s: the head needs 512 resources; only 504 are free',
                   xlabel='Later fitting job', ylabel='Start (seconds)')
    axes[1, 0].legend(fontsize=8)
    selected = (232, 243, 245)
    for offset, (name, label) in enumerate((('original_native', 'Original Slurm: RPCs processed separately'),
                                          ('native', labels['native']), ('diagnostic', labels['diagnostic']))):
        axes[1, 1].bar([i + (offset - 1) * .25 for i in range(len(selected))],
                       [schedules[name][job - 1]['start'] for job in selected],
                       width=.25, label=label)
    axes[1, 1].set(xticks=range(len(selected)), xticklabels=[str(job) for job in selected],
                   title='At 798 s: apply both one-resource releases before dispatch',
                   xlabel='Job (232 needs two resources; 243 and 245 need one)', ylabel='Start (seconds)')
    axes[1, 1].legend(fontsize=8)
    fig.suptitle('License-off diagnosis: current capacity only, with equal-time events batched\n'
                 'Exact agreement is a result for this workload and adapter, not ordinary node-based Slurm')
    for extension in ('pdf', 'svg', 'png'):
        fig.savefig(path.with_name('gated-diagnosis.' + extension), dpi=170)
    plt.close(fig)


if __name__ == '__main__':
    main()
