"""Plot saved comparison inputs; no scheduler installation is needed."""
import json
from pathlib import Path
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np


def comparison_title(report):
    return (f'Periodic hybrid backfill: R={report["reservation_depth"]}, '
            f'Q={report["backfill_queue_depth"]}, '
            f'interval={report["backfill_interval_seconds"]} s\n'
            f'{len(report["jobs"]):,} jobs, {report["capacity"]} '
            f'{report.get("capacity_label", "nodes")}; main-scheduler throttling off\n'
            f'{report.get("plot_note", "Runtimes rounded up to whole seconds in both inputs")}')


def plot_metrics(report, path, colors, labels):
    names = ('dr_evt', 'slurm')
    waits = [np.array([job['start'] - workload[0]
                       for job, workload in zip(report[name + '_schedule'], report['jobs'])])
             for name in names]
    wait_scale = 3600 if max(wait.max() for wait in waits) >= 7200 else 1
    wait_unit = 'hours' if wait_scale == 3600 else 'seconds'
    waits = [wait / wait_scale for wait in waits]
    fig, axes = plt.subplots(2, 3, figsize=(15, 9), constrained_layout=True)
    fig.suptitle(comparison_title(report))
    positions = np.arange(5)
    for index, (name, wait) in enumerate(zip(names, waits)):
        values = [wait.min(), wait.mean(), np.median(wait),
                  np.percentile(wait, 95), wait.max()]
        bars = axes[0, 0].bar(positions + (index - .5) * .36, values,
                              width=.36, label=labels[name], color=colors[name])
        axes[0, 0].bar_label(bars, fmt='%.4g', fontsize=7)
        axes[0, 1].step(np.sort(wait), np.arange(1, len(wait) + 1) / len(wait),
                        where='post', label=labels[name], color=colors[name],
                        linestyle='-' if index == 0 else '--')
    axes[0, 0].set_xticks(positions, ['min', 'mean', 'median', 'p95', 'max'])
    axes[0, 0].set(ylabel=f'Waiting time ({wait_unit})', title='Waiting-time summary')
    axes[0, 0].margins(y=.2)
    axes[0, 0].legend(loc='upper left', fontsize=8)
    axes[0, 1].set(xlabel=f'Waiting time ({wait_unit})', ylabel='Cumulative fraction of jobs',
                   title='Waiting-time distribution', ylim=(0, 1.03))
    axes[0, 1].legend(loc='lower right')
    for axis, key, scale, ylabel, title in (
            (axes[0, 2], 'utilization_percent', 1, 'Percent of total capacity',
             'Utilization (100 × area / (nodes × makespan))'),
            (axes[1, 0], 'makespan_seconds', 3600, 'Hours',
             'Makespan (Last completion − first arrival)'),
            (axes[1, 1], 'resource_area_node_seconds', 1000, 'Thousand node-seconds',
             'Area under resource curve')):
        values = [report[name + '_metrics'][key] / scale for name in names]
        bars = axis.bar([labels[name] for name in names], values,
                        color=[colors[name] for name in names], width=.6)
        axis.bar_label(bars, fmt='%.4g', fontsize=8)
        axis.set(ylabel=ylabel)
        axis.set_title(title, fontsize=10)
        axis.margins(y=.2)
    difference = waits[1] - waits[0]
    extent = max(1 / wait_scale, np.max(np.abs(difference)) * 1.05)
    edges = np.linspace(-extent, extent, 31)
    axis = axes[1, 2]
    axis.hist(difference[difference < 0], bins=edges, rwidth=.8,
              color=colors['dr_evt'], label='Negative: Slurm waits less')
    axis.hist(difference[difference > 0], bins=edges, rwidth=.8,
              color=colors['slurm'], label='Positive: Slurm waits more')
    axis.bar(0, int(np.sum(difference == 0)), width=(edges[1] - edges[0]) * .12,
             color='#687787', label='Zero: equal waiting time')
    axis.axvline(0, color='gray', linewidth=.6)
    axis.set(xlabel=f'Slurm waiting − DR_EVT waiting ({wait_unit})', ylabel='Number of jobs',
              title='Waiting-time differences')
    axis.legend(loc='upper right', fontsize=7)
    for extension in ('pdf', 'png', 'svg'):
        fig.savefig(path.with_name('comparison-metrics.' + extension), dpi=170)
    plt.close(fig)
    data = {
        'waiting_time_unit': wait_unit,
        'waiting_summary': {name: dict(zip(('min', 'mean', 'median', 'p95', 'max'),
                               map(float, (wait.min(), wait.mean(), np.median(wait),
                                           np.percentile(wait, 95), wait.max()))))
                            for name, wait in zip(names, waits)},
        'waiting_cdf': {name: {'waiting_time': np.sort(wait).tolist(),
                              'cumulative_fraction': (np.arange(1, len(wait) + 1) / len(wait)).tolist()}
                        for name, wait in zip(names, waits)},
        'metrics': {name: report[name + '_metrics'] for name in names},
        'resource_curves': report['resource_curves'],
        'waiting_difference_histogram': {
            'definition': 'Slurm waiting time minus DR_EVT waiting time', 'unit': wait_unit,
            'bin_edges': edges.tolist(),
            'negative_counts': np.histogram(difference[difference < 0], edges)[0].tolist(),
            'positive_counts': np.histogram(difference[difference > 0], edges)[0].tolist(),
            'zero_count': int(np.sum(difference == 0)),
        },
    }
    path.with_name('plot-data.json').write_text(json.dumps(data, indent=2) + '\n')


def main():
    path = Path(sys.argv[1])
    report = json.loads(path.read_text())
    colors = {'dr_evt': '#2166ac', 'slurm': '#b35806'}
    labels = {'dr_evt': 'DR_EVT', 'slurm': report.get('slurm_label', 'Slurm simulator')}
    fig, axes = plt.subplots(2, 2, figsize=(12, 7), constrained_layout=True)
    for name, curve in report['resource_curves'].items():
        axes[0, 0].step([t / 3600 for t, _ in curve], [n for _, n in curve],
                        where='post', label=labels[name], color=colors[name], alpha=.85)
    axes[0, 0].set(xlabel='Simulation time (hours)', ylabel='Allocated nodes',
                    title='Periodic hybrid backfill: R=32, Q=64, interval=30 s')
    axes[0, 0].legend()
    difference = [(native['start'] - dr['start']) / 3600
                  for native, dr in zip(report['slurm_schedule'], report['dr_evt_schedule'])]
    axes[0, 1].hist(difference, bins=40, color=colors['slurm'])
    axes[0, 1].set(xlabel='Slurm minus DR_EVT start time (hours)', ylabel='Jobs',
                    title=f'{report["different_start_times"]:,} differing starts')
    for axis, key, scale, label in (
            (axes[1, 0], 'mean_wait_seconds', 3600, 'Mean waiting time (hours)'),
            (axes[1, 1], 'utilization_percent', 1, 'Utilization (%)')):
        values = [report[name + '_metrics'][key] / scale for name in ('dr_evt', 'slurm')]
        bars = axis.bar([labels[name] for name in ('dr_evt', 'slurm')], values,
                        color=[colors[name] for name in ('dr_evt', 'slurm')])
        axis.bar_label(bars, fmt='%.3f', padding=3)
        axis.set(ylabel=label, ylim=(0, max(values) * 1.15))
    fig.suptitle(comparison_title(report))
    fig.savefig(path.with_name('comparison.svg'))
    fig.savefig(path.with_name('comparison.png'), dpi=160)
    fig.savefig(path.with_name('comparison.pdf'))
    plt.close(fig)
    plot_metrics(report, path, colors, labels)


if __name__ == '__main__':
    main()
