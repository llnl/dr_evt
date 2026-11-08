"""Plot dispatch and completion-order evidence from the license-off replay."""
import argparse
import json
from pathlib import Path
import shutil

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    data = json.loads((args.directory / 'plot-data.json').read_text())
    diagnosis = json.loads((args.directory / 'diagnosis.json').read_text())
    fig, axes = plt.subplots(1, 3, figsize=(15, 5), constrained_layout=True)
    colors = {'slurm': '#27824b', 'corrected_dr_evt': '#2876b8',
              'skip_dispatch_batched': '#c77b24'}
    for ax, indices, models, window, title in (
        (axes[0], [57, 58, 59], ['slurm', 'corrected_dr_evt'], (0, 110),
         'Main dispatch continues when the head cannot fit'),
        (axes[1], [232, 243, 245], ['slurm', 'skip_dispatch_batched'], (780, 930),
         'Dispatch between two one-license releases'),
    ):
        labels = []
        for position, job in enumerate(indices):
            labels.append(f'Job {job} ({data["jobs"][job - 1][1]} licenses)')
            for offset, model in enumerate(models):
                row = data[model][job - 1]
                ax.broken_barh([(row['start'], row['end'] - row['start'])],
                               (position + offset * .32, .27), facecolors=colors[model])
                ax.text(row['start'], position + offset * .32 + .14,
                        str(int(row['start'])), va='center', fontsize=8)
        ax.set_yticks([i + .3 for i in range(len(indices))], labels)
        ax.set_xlim(*window)
        ax.set_xlabel('Seconds from trace start')
        ax.set_title(title, fontsize=11)
        ax.invert_yaxis()
        ax.grid(axis='x', alpha=.2)
    axes[0].axvline(13, color='gray', linestyle=':', label='Arrivals at 13 s')
    axes[1].axvline(798, color='gray', linestyle=':', label='Two completions at 798 s')
    from matplotlib.patches import Patch
    axes[0].legend(handles=[Patch(color=colors['slurm'], label='Native Slurm, licenses off'),
                           Patch(color=colors['corrected_dr_evt'], label='Corrected DR_EVT')], fontsize=8)
    axes[1].legend(handles=[Patch(color=colors['slurm'], label='Native Slurm, serial RPCs'),
                           Patch(color=colors['skip_dispatch_batched'], label='Skipping dispatch, batched events')], fontsize=8)
    names = list(diagnosis['variants'])
    areas = [diagnosis['variants'][name]['resource_curve_difference_node_seconds'] / 1e6
             for name in names]
    axes[2].barh(range(4), areas, color=['#2876b8', '#8d6ab8', '#c77b24', '#27824b'])
    axes[2].set_yticks(range(4), ['Corrected DR_EVT', 'Remove future protection',
                                'Also consider later jobs', 'Also use serial events'])
    axes[2].invert_yaxis()
    axes[2].set_xlabel('Area between curves (million node-seconds)')
    axes[2].set_title('Controlled 2,000-job reconstruction', fontsize=11)
    for i, area in enumerate(areas):
        axes[2].text(area + .15, i, f'{area:.3f}', va='center', fontsize=9)
    axes[2].set_xlim(0, 14)
    fig.suptitle('License-off differences: dispatch policy and adapter event ordering', fontsize=14)
    for extension in ('pdf', 'svg', 'png'):
        fig.savefig(args.directory / ('license-off-diagnosis.' + extension), dpi=170)
    plt.close(fig)
    if Path(__file__).resolve() != (args.directory / Path(__file__).name).resolve():
        shutil.copy2(__file__, args.directory / Path(__file__).name)


if __name__ == '__main__':
    main()
