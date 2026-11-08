"""Plot saved OpenPBS/DR_EVT schedules without rerunning either scheduler."""
import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def plot(path):
    report = json.loads(path.read_text())
    jobs = json.loads((path.parent / 'workload.json').read_text())
    fig, axes = plt.subplots(3, 1, figsize=(10, 8), constrained_layout=True)
    for key, label, color in [('openpbs', 'Native OpenPBS', '#2563eb'),
                               ('dr_evt', 'DR_EVT', '#d97706')]:
        profile = report[key + '_profile']
        schedule = report[key + '_schedule']
        axes[0].step([p[0] / 3600 for p in profile], [p[1] for p in profile],
                     where='post', label=label, color=color, alpha=.8)
        starts = sorted(s[0] / 3600 for s in schedule)
        axes[1].step([0] + starts, list(range(len(starts) + 1)), where='post',
                     label=label, color=color, alpha=.8)
        waits = [(s[0] - j[0]) / 3600 for j, s in zip(jobs, schedule)]
        axes[2].plot(range(1, len(jobs) + 1), waits, '.', ms=3, label=label,
                     color=color, alpha=.65)
    axes[0].set_ylabel('Allocated capacity')
    axes[0].set_ylim(0, report['capacity'] * 1.05)
    axes[1].set_ylabel('Jobs started')
    axes[2].set_ylabel('Wait (hours)')
    for ax in axes[:2]: ax.set_xlabel('Simulation time (hours)')
    axes[2].set_xlabel('Job in arrival order')
    for ax in axes:
        ax.grid(alpha=.2)
        ax.legend()
    fig.suptitle(f"{report['name']}: {report['jobs']} jobs, "
                 f"{report['different_jobs']} schedules differ")
    for extension in ('svg', 'pdf', 'png'):
        fig.savefig(path.parent / ('comparison.' + extension), dpi=160)
    plt.close(fig)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    plot(parser.parse_args().report)
