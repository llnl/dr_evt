"""Plot job metrics and directly measured resource curves for paired runs."""
import argparse
import csv
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

LABELS = ('dr_evt', 'fiction')
COLORS = ('#2369bd', '#d77718')


def area(samples):
    """Integrate a right-continuous resource curve using rectangles."""
    return sum((end[0] - start[0]) * start[1]
               for start, end in zip(samples, samples[1:]))


def read_curve(path):
    with path.open() as stream:
        return [[float(row['time']), int(row['allocated_nodes'])]
                for row in csv.DictReader(stream)]


def align_curves(curves, start, end):
    """Align right-continuous steps on the union of their change times."""
    times = sorted({start, end} | {t for curve in curves for t, _ in curve
                                  if start <= t <= end})
    values = []
    for curve in curves:
        cursor = 0
        row = []
        for time in times:
            while cursor + 1 < len(curve) and curve[cursor + 1][0] <= time:
                cursor += 1
            row.append(curve[cursor][1] if curve[cursor][0] <= time else 0)
        values.append(row)
    return np.array(times), np.array(values)


def stats(values):
    return dict(zip(('min', 'mean', 'median', 'p95', 'max'),
                    map(float, (np.min(values), np.mean(values), np.median(values),
                                np.percentile(values, 95), np.max(values)))))


def bars(axis, names, groups, ylabel, title):
    positions = np.arange(len(names))
    for index, (label, color, values) in enumerate(zip(LABELS, COLORS, groups)):
        rectangles = axis.bar(positions + (index - 0.5) * 0.36, values,
                              width=0.36, label=label, color=color)
        axis.bar_label(rectangles, labels=[f'{value:.4g}' for value in values], fontsize=7)
    axis.set_xticks(positions, names)
    axis.set(ylabel=ylabel, title=title)
    axis.margins(y=0.2)


def save(figure, directory, name):
    for extension in ('png', 'svg', 'pdf'):
        figure.savefig(directory / f'{name}.{extension}', dpi=170)
    plt.close(figure)


def scheduler_bars(axis, values, ylabel, title):
    rectangles = axis.bar([0, 1], values, color=COLORS, width=0.6)
    axis.bar_label(rectangles, labels=[f'{value:.4g}' for value in values], fontsize=8)
    axis.set_xticks([0, 1], LABELS)
    axis.set(ylabel=ylabel)
    axis.set_title(title, fontsize=10)
    axis.margins(y=0.2)


def plot_case(entry, capture, root, capacity):
    directory = root / capture['case']
    directory.mkdir(parents=True, exist_ok=True)
    schedules = [capture.get('dr_evt_schedule', entry['dr_evt_schedule']),
                 capture.get('reference_schedule', entry['reference_schedule'])]
    waits = [np.array([job['start'] - job['submit'] for job in schedule])
             for schedule in schedules]
    start = min(job['submit'] for job in schedules[0])
    end = max(job['finish'] for schedule in schedules for job in schedule)
    curves = [read_curve(directory / f'{name}-resource-profile.csv')
              for name in ('dr_evt', 'fiction')]
    times, values = align_curves(curves, start, end)
    difference = values[0] - values[1]
    difference_area = float(np.sum(np.diff(times) * np.abs(difference[:-1])))
    areas = [area(curve) for curve in curves]
    makespans = [max(job['finish'] for job in schedule) - start for schedule in schedules]
    utilizations = [100 * value / (capacity * span) for value, span in zip(areas, makespans)]
    shared_utilizations = [100 * value / (capacity * (end - start)) for value in areas]
    wait_stats = [stats(wait) for wait in waits]
    absolute_wait = np.abs(waits[0] - waits[1])
    signed_wait = waits[1] - waits[0]
    result = {
        'case': entry['case'], 'jobs': len(schedules[0]), 'capacity': capacity,
        'per_pass_accounting': True, 'queue_depth': entry['reference_queue_depth'],
        'window_seconds': [start, end],
        'waiting_seconds': dict(zip(LABELS, wait_stats)),
        'utilization_percent': dict(zip(LABELS, utilizations)),
        'utilization_window': 'first arrival to each scheduler own last completion',
        'shared_window_utilization_percent': dict(zip(LABELS, shared_utilizations)),
        'makespan_seconds': dict(zip(LABELS, makespans)),
        'resource_area_node_seconds': dict(zip(LABELS, areas)),
        'area_between_curves_node_seconds': difference_area,
        'normalized_area_between_curves': difference_area / (capacity * (end - start)),
        'absolute_wait_difference_seconds': stats(absolute_wait),
        'waiting_time_difference_definition': 'fiction waiting time minus dr_evt waiting time',
        'signed_wait_difference_seconds': stats(signed_wait),
        'waiting_time_difference_job_counts': {
            'negative_fiction_waits_less': int(np.sum(signed_wait < 0)),
            'zero_equal_waiting': int(np.sum(signed_wait == 0)),
            'positive_fiction_waits_more': int(np.sum(signed_wait > 0)),
        },
        'native_duplicate_owners': capture['native_duplicate_owners'],
        'reference_replay_differing_jobs': capture['reference_replay_differing_jobs'],
    }
    # Metrics must use the jobs from the same run as the measured curves.
    assert not result['reference_replay_differing_jobs'] or 'reference_schedule' in capture
    policy = entry['backfill_policy']
    policy_label = (f"no backfill (Q={entry['reference_queue_depth']})" if policy == 'none' else
                    f"{policy} backfill (R={entry['reservation_depth']},Q={entry['reference_queue_depth']})")
    entry = dict(entry)
    if 'fiction_match_policy' not in entry:
        config = json.loads((directory / 'scheduler.json').read_text())
        entry['fiction_match_policy'] = config['sched-fluxion-resource']['match-policy']
    title = (f"FCFS / {policy_label} · {len(schedules[0]):,} jobs · "
             f"{entry.get('workload_label', entry['case'].split('_jobs_')[0])}\n"
             f"fiction match policy: {entry['fiction_match_policy']}")
    figure, axes = plt.subplots(3, 1, figsize=(12, 9), layout='constrained')
    figure.suptitle(title)
    for i, (label, color) in enumerate(zip(LABELS, COLORS)):
        axes[0].step(times / 3600, values[i], where='post', label=label,
                     color=color, linewidth=1.8 if i == 0 else 1.1,
                     linestyle='-' if i == 0 else '--')
        axes[1].step(times, values[i], where='post', label=label, color=color,
                     linewidth=2 if i == 0 else 1.5, linestyle='-' if i == 0 else '--')
    arrivals = sorted({job['submit'] for job in schedules[0]})
    first_finish = max(job['finish'] for schedule in schedules
                       for job in schedule if job['submit'] == start)
    zoom_end = min(end, start + (first_finish - start) * 1.08)
    if len(arrivals) > 1:
        zoom_end = min(zoom_end, arrivals[1])
    zoom_start = start
    zoom_title = 'First burst — zoom'
    different_steps = np.flatnonzero(difference[:-1])
    if len(different_steps):
        first_difference = times[different_steps[0]]
        zoom_start = max(start, first_difference - 300)
        zoom_end = min(end, first_difference + 600)
        zoom_title = f'Zoom near first resource-curve difference at {first_difference:g} s'
    axes[0].set(xlabel='Simulation time (hours)', ylabel='Allocated nodes',
                title='Full resource-manager curves', ylim=(-0.15, capacity + 0.4))
    axes[0].legend(loc='upper right')
    axes[1].set(xlabel='Simulation time (seconds)', ylabel='Allocated nodes',
                title=zoom_title, xlim=(zoom_start, zoom_end),
                ylim=(-0.15, capacity + 0.4))
    axes[2].step(times / 3600, difference, where='post', color='#7a4799')
    axes[2].axhline(0, color='gray', linewidth=0.6)
    axes[2].set(xlabel='Simulation time (hours)', ylabel='Node difference',
                ylim=(-capacity, capacity),
                title=(f'dr_evt − Fiction · area between curves: {difference_area:g} node-s'
                       f" · normalized: {100 * result['normalized_area_between_curves']:.3f}%"))
    save(figure, directory, 'resource-curves')

    figure, axes = plt.subplots(2, 3, figsize=(15, 9), layout='constrained')
    figure.suptitle(title)
    wait_scale = 3600 if max(wait.max() for wait in waits) >= 7200 else 1
    wait_unit = 'hours' if wait_scale == 3600 else 'seconds'
    names = ['min', 'mean', 'median', 'p95', 'max']
    bars(axes[0, 0], names, [[s[name] / wait_scale for name in names] for s in wait_stats],
         f'Waiting time ({wait_unit})', 'Waiting-time summary')
    axes[0, 0].legend(loc='upper left', fontsize=8)
    for label, color, wait in zip(LABELS, COLORS, waits):
        axes[0, 1].step(np.sort(wait) / wait_scale, np.arange(1, len(wait) + 1) / len(wait),
                        where='post', color=color, label=label,
                        linestyle='-' if label == LABELS[0] else '--')
    axes[0, 1].set(xlabel=f'Waiting time ({wait_unit})', ylabel='Cumulative fraction of jobs',
                   title='Waiting-time distribution', ylim=(0, 1.03))
    scheduler_bars(axes[0, 2], utilizations, 'Percent of total capacity',
                    'Utilization (100 × area / (nodes × makespan))')
    scheduler_bars(axes[1, 0], [m / 3600 for m in makespans], 'Hours',
                    'Makespan (Last completion - first arrival)')
    scheduler_bars(axes[1, 1], [a / 1000 for a in areas], 'Thousand node-seconds',
                    'Area under resource curve')
    differences = signed_wait / wait_scale
    extent = max(1 / wait_scale, float(np.max(np.abs(differences))) * 1.05)
    edges = np.linspace(-extent, extent, 31)
    axis = axes[1, 2]
    axis.hist(differences[differences < 0], bins=edges, rwidth=0.8, color=COLORS[0],
              label='Negative: Fiction waits less')
    axis.hist(differences[differences > 0], bins=edges, rwidth=0.8, color=COLORS[1],
              label='Positive: Fiction waits more')
    axis.bar(0, int(np.sum(differences == 0)), width=(edges[1] - edges[0]) * 0.12,
             color='#687787', label='Zero: equal waiting time')
    axis.axvline(0, color='gray', linewidth=0.6)
    axis.set(xlabel=f'Fiction waiting − dr_evt waiting ({wait_unit})',
              ylabel='Number of jobs', title='Waiting-time differences')
    axis.legend(loc='upper right', fontsize=7)
    axes[0, 1].legend(loc='lower right')
    save(figure, directory, 'comparison-metrics')
    plot_data = {
        'waiting_time_unit': wait_unit,
        'waiting_summary': {label: {name: value / wait_scale for name, value in summary.items()}
                            for label, summary in zip(LABELS, wait_stats)},
        'waiting_cdf': {label: {'waiting_time': (np.sort(wait) / wait_scale).tolist(),
                               'cumulative_fraction': (np.arange(1, len(wait) + 1) / len(wait)).tolist()}
                        for label, wait in zip(LABELS, waits)},
        'utilization_percent': dict(zip(LABELS, utilizations)),
        'makespan_hours': dict(zip(LABELS, [m / 3600 for m in makespans])),
        'resource_area_thousand_node_seconds': dict(zip(LABELS, [a / 1000 for a in areas])),
        'waiting_difference_histogram': {
            'definition': 'fiction waiting time - dr_evt waiting time',
            'unit': wait_unit, 'bin_edges': edges.tolist(),
            'negative_counts': np.histogram(differences[differences < 0], edges)[0].tolist(),
            'positive_counts': np.histogram(differences[differences > 0], edges)[0].tolist(),
            'zero_count': int(np.sum(differences == 0)),
        },
        'resource_curves': {
            'time_seconds': times.tolist(), 'time_hours': (times / 3600).tolist(),
            'dr_evt_allocated_nodes': values[0].tolist(),
            'fiction_allocated_nodes': values[1].tolist(),
            'dr_evt_minus_fiction_nodes': difference.tolist(),
            'zoom_window_seconds': [float(zoom_start), float(zoom_end)],
        },
    }
    for name, data in [('comparison-metrics.json', result), ('plot-data.json', plot_data)]:
        (directory / name).write_text(json.dumps(data, indent=2) + '\n')
    plot_entry = dict(entry, dr_evt_schedule=schedules[0], reference_schedule=schedules[1])
    (directory / 'plot-input.json').write_text(json.dumps({
        'entry': plot_entry, 'capture': capture, 'capacity': capacity}, indent=2) + '\n')
    (directory / 'plot_comparison.py').write_bytes(Path(__file__).read_bytes())
    with (directory / 'plot-summary.csv').open('w', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(['metric', *LABELS, 'unit'])
        for name in ('min', 'mean', 'median', 'p95', 'max'):
            writer.writerow([f'waiting_{name}', *[s[name] / wait_scale for s in wait_stats], wait_unit])
        writer.writerow(['utilization', *utilizations, 'percent'])
        writer.writerow(['makespan', *[m / 3600 for m in makespans], 'hours'])
        writer.writerow(['resource_area', *[a / 1000 for a in areas], 'thousand node-seconds'])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paired_report', type=Path, nargs='?')
    parser.add_argument('manager_report', type=Path, nargs='?')
    parser.add_argument('--plot-input', type=Path,
                        help='Regenerate figures from saved inputs in the figure folder')
    args = parser.parse_args()
    if args.plot_input:
        data = json.loads(args.plot_input.read_text())
        if not data['entry'].get('per_pass_accounting'):
            parser.error('Saved input must use per_pass_accounting')
        capture = dict(data['capture'], case='.')
        plot_case(data['entry'], capture, args.plot_input.parent, data['capacity'])
        return
    if not args.paired_report or not args.manager_report:
        parser.error('Supply both reports or --plot-input')
    paired = json.loads(args.paired_report.read_text())
    manager = json.loads(args.manager_report.read_text())
    if not paired['assumptions'].get('per_pass_accounting') or not manager.get('per_pass_accounting'):
        parser.error('Both reports must use per_pass_accounting')
    results = []
    for entry, capture in zip(paired['cases'], manager['cases']):
        expected = (f"{entry['backfill_policy']}_r{entry['reservation_depth']}"
                    f"_q{entry['reference_queue_depth']}_{entry['case']}")
        assert expected == capture['case'], 'Case order differs'
        config = json.loads((args.paired_report.parent / expected / 'scheduler.json').read_text())
        entry['fiction_match_policy'] = config['sched-fluxion-resource']['match-policy']
        results.append(plot_case(entry, capture, args.manager_report.parent, manager['capacity']))
    assert len(results) == len(paired['cases']) == len(manager['cases'])
    (args.manager_report.parent / 'comparison-metrics.json').write_text(
        json.dumps(results, indent=2) + '\n')
    (args.manager_report.parent / 'plot-script.py').write_bytes(Path(__file__).read_bytes())
    print('Plotted', len(results), 'cases:', args.manager_report.parent, flush=True)


if __name__ == '__main__':
    main()
