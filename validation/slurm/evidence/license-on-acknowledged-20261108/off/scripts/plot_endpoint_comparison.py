"""Plot the saved controlled experiment isolating reservation endpoint checks."""
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
    names = ('baseline', 'admission_only', 'inclusive_searches')
    labels = ('Previous rule', 'Admission only', 'Admission +\nreservation placement')
    colors = ('#687787', '#b35806', '#317b71')
    fig, axes = plt.subplots(1, 2, figsize=(12, 5.5))
    for axis, key, title, ylabel in (
            (axes[0], 'different_start_times', 'Jobs with different starts', 'Number of jobs'),
            (axes[1], 'resource_curve_difference_node_seconds', 'Difference between allocation curves', 'Node-seconds')):
        values = [data['variants'][name][key] for name in names]
        bars = axis.bar(labels, values, color=colors)
        axis.bar_label(bars, labels=[f'{value:,}' for value in values], padding=4)
        axis.set(title=title, ylabel=ylabel, ylim=(0, max(values) * 1.2))
        axis.grid(axis='y', alpha=.2)
        axis.set_axisbelow(True)
    fig.suptitle('Endpoint checks explain the remaining license-enabled Lassen divergence\n'
                 '2,000 jobs; identical workload, capacity and backfill settings', fontsize=14)
    fig.text(.5, .035, 'The unchanged Python reference reproduces DR_EVT before the endpoint fix.\n'
             'Including endpoints in both searches matches native Slurm; the corrected C++ simulator confirms that match.',
             ha='center', fontsize=10)
    fig.subplots_adjust(left=.08, right=.97, top=.78, bottom=.23, wspace=.3)
    for extension in ('pdf', 'svg', 'png'):
        fig.savefig(args.data.with_name('endpoint-rule-comparison.' + extension), dpi=170)
    plt.close(fig)
    target = args.data.with_name(Path(__file__).name)
    if Path(__file__).resolve() != target.resolve():
        shutil.copy2(__file__, target)


if __name__ == '__main__':
    main()
