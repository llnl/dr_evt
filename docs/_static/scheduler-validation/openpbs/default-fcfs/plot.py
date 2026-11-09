"""Plot saved native OpenPBS comparisons as resource curves and six metric panels."""
import argparse
import csv
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np


def plot(path):
    report = json.loads(path.read_text())
    jobs = json.loads((path.parent / 'workload.json').read_text())
    priority = report.get('priority_policy', report.get('dr_priority', 'fcfs'))
    # Keep replay evidence unchanged while displaying the current policy name.
    if priority == 'fcfs_openpbs':
        priority = 'fcfs_1s_evt_group'
    names = ('dr_evt', 'openpbs')
    labels = {'dr_evt': f'DR_EVT\n({priority})', 'openpbs': 'Native OpenPBS'}
    colors = {'dr_evt': '#2166ac', 'openpbs': '#b35806'}
    title = (f'Native OpenPBS vs DR_EVT ({priority})\n'
             f'{report["jobs"]:,} jobs; capacity={report["capacity"]}, '
             f'R={report["reservation_depth"]}; '
             f'{report["different_jobs"]:,} different starts or completions')
    waits = {name: np.array([start - job[0] for job, (start, _end)
                            in zip(jobs, report[name + '_schedule'])])
             for name in names}
    scale = 3600 if max(w.max() for w in waits.values()) >= 7200 else 1
    unit = 'hours' if scale == 3600 else 'seconds'
    summaries = {}
    metrics = {}
    for name in names:
        wait = waits[name] / scale
        summaries[name] = dict(zip(('min', 'mean', 'median', 'p95', 'max'),
                                  map(float, (wait.min(), wait.mean(),
                                              np.median(wait), np.percentile(wait, 95),
                                              wait.max()))))
        profile = report[name + '_profile']
        area = sum(nodes * (next_time - time)
                   for (time, nodes), (next_time, _nodes) in zip(profile, profile[1:]))
        metrics[name] = {'utilization_percent': report[name + '_metrics']['utilization'] * 100,
                         'makespan_hours': report[name + '_metrics']['makespan_seconds'] / 3600,
                         'capacity_area_thousands': area / 1000}

    def save(figure, stem):
        for extension in ('svg', 'pdf', 'png'):
            figure.savefig(path.parent / (stem + '.' + extension), dpi=170)
        plt.close(figure)

    figure, axis = plt.subplots(figsize=(12, 5), constrained_layout=True)
    figure.suptitle(title)
    for index, name in enumerate(names):
        profile = report[name + '_profile']
        axis.step([time / 3600 for time, _nodes in profile],
                  [nodes for _time, nodes in profile], where='post',
                  color=colors[name], label=labels[name],
                  linestyle='-' if index == 0 else '--')
    axis.set(xlabel='Simulation time (hours)', ylabel='Allocated capacity units',
             title='Full resource-use curves', ylim=(0, report['capacity'] * 1.05))
    axis.grid(alpha=.2)
    axis.legend()
    save(figure, 'resource-curves')

    figure, axes = plt.subplots(2, 3, figsize=(15, 9), constrained_layout=True)
    figure.suptitle(title)
    positions = np.arange(5)
    for index, name in enumerate(names):
        values = list(summaries[name].values())
        bars = axes[0, 0].bar(positions + (index - .5) * .36, values,
                              width=.36, label=labels[name], color=colors[name])
        axes[0, 0].bar_label(bars, fmt='%.4g', fontsize=7)
        wait = np.sort(waits[name]) / scale
        axes[0, 1].step(wait, np.arange(1, len(wait) + 1) / len(wait),
                        where='post', label=labels[name], color=colors[name],
                        linestyle='-' if index == 0 else '--')
    axes[0, 0].set_xticks(positions, ['min', 'mean', 'median', 'p95', 'max'])
    axes[0, 0].set(ylabel=f'Waiting time ({unit})', title='Waiting-time summary')
    axes[0, 0].margins(y=.2)
    axes[0, 0].legend(loc='upper left', fontsize=8)
    axes[0, 1].set(xlabel=f'Waiting time ({unit})', ylabel='Cumulative fraction of jobs',
                   title='Waiting-time distribution', ylim=(0, 1.03))
    axes[0, 1].legend(loc='lower right', fontsize=8)
    for axis, key, ylabel, title_text in (
            (axes[0, 2], 'utilization_percent', 'Percent of total capacity', 'Utilization'),
            (axes[1, 0], 'makespan_hours', 'Hours', 'Makespan'),
            (axes[1, 1], 'capacity_area_thousands', 'Thousand capacity-unit seconds',
             'Area under resource curve')):
        bars = axis.bar([labels[name] for name in names],
                        [metrics[name][key] for name in names],
                        color=[colors[name] for name in names], width=.6)
        axis.bar_label(bars, fmt='%.4g', fontsize=8)
        axis.set(ylabel=ylabel, title=title_text)
        axis.margins(y=.2)
    difference = (waits['openpbs'] - waits['dr_evt']) / scale
    extent = max(1 / scale, float(np.max(np.abs(difference))) * 1.05)
    edges = np.linspace(-extent, extent, 31)
    axis = axes[1, 2]
    axis.hist(difference[difference < 0], bins=edges, rwidth=.8,
              color=colors['openpbs'], label='Negative: OpenPBS waits less')
    axis.hist(difference[difference > 0], bins=edges, rwidth=.8,
              color=colors['dr_evt'], label='Positive: OpenPBS waits more')
    axis.bar(0, int(np.sum(difference == 0)), width=(edges[1] - edges[0]) * .12,
             color='#687787', label='Zero: equal waiting time')
    axis.axvline(0, color='gray', linewidth=.6)
    axis.set(xlabel=f'OpenPBS waiting − DR_EVT waiting ({unit})',
             ylabel='Number of jobs', title='Waiting-time differences')
    axis.legend(loc='upper right', fontsize=7)
    save(figure, 'comparison')
    data = {'policy': priority, 'jobs': report['jobs'],
            'different_jobs': report['different_jobs'], 'waiting_time_unit': unit,
            'waiting_summary': summaries, 'metrics': metrics,
            'resource_curves': {name: report[name + '_profile'] for name in names},
            'waiting_cdf': {name: {'waiting_time': (np.sort(waits[name]) / scale).tolist(),
                                  'fraction': (np.arange(1, len(jobs) + 1) / len(jobs)).tolist()}
                            for name in names},
            'waiting_difference_histogram': {
                'definition': 'OpenPBS wait minus DR_EVT wait', 'unit': unit,
                'edges': edges.tolist(),
                'negative_counts': np.histogram(difference[difference < 0], edges)[0].tolist(),
                'positive_counts': np.histogram(difference[difference > 0], edges)[0].tolist(),
                'zero_count': int(np.sum(difference == 0))}}
    (path.parent / 'plot-data.json').write_text(json.dumps(data, indent=2) + '\n')
    with (path.parent / 'plot-summary.csv').open('w', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(['scheduler', 'policy', 'mean_wait_seconds', 'makespan_seconds',
                         'utilization_percent', 'capacity_area_thousands'])
        for name in names:
            writer.writerow([name, priority if name == 'dr_evt' else 'native',
                             report[name + '_metrics']['mean_wait_seconds'],
                             report[name + '_metrics']['makespan_seconds'],
                             metrics[name]['utilization_percent'],
                             metrics[name]['capacity_area_thousands']])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    plot(parser.parse_args().report)
