"""Draw feasible node assignments for pooled-license and DR_EVT schedules."""
import json
import argparse
from pathlib import Path
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

COLORS = dict(zip('ABCD', ('#317b71', '#bf8429', '#377aba', '#7953b6')))


def assignments(schedule, capacity):
    occupied_until = [0] * capacity
    rows = []
    for index in sorted(range(len(schedule)), key=lambda i: (schedule[i]['start'], i)):
        job = schedule[index]
        free = [node for node, end in enumerate(occupied_until) if end <= job['start']]
        assert len(free) >= job['nodes']
        nodes = free[:job['nodes']]
        for node in nodes:
            occupied_until[node] = job['end']
        rows.append(dict(job, job='ABCD'[index], assigned_node_equivalents=nodes))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--case', choices=('pooled', 'protection'), default='protection')
    args = parser.parse_args()
    root = args.root
    folders = ('four-job-pooled', 'four-job-pooled-off') if args.case == 'pooled' else (
        'reservation-protection-on', 'reservation-protection-off')
    on = json.loads((root / folders[0] / 'report.json').read_text())
    off = json.loads((root / folders[1] / 'report.json').read_text())
    assert on['jobs'] == off['jobs']
    panels = [('Slurm: license planning on', on['slurm_schedule']),
              ('Slurm: license planning off', off['slurm_schedule']),
              ('DR_EVT: protected periodic backfill', on['dr_evt_schedule']),
              ('DR_EVT: no-reservation diagnostic', off['dr_evt_schedule'])]
    data = {'jobs': on['jobs'], 'capacity': 4, 'assignments': {},
            'assignment_kind': 'one feasible mapping onto interchangeable node equivalents; '
                               'neither licenses nor DR_EVT emit these node IDs',
            'native_dummy_node_assignments': {
                'license_on': [row['assignment'] for row in on['slurm_schedule']],
                'license_off': [row['assignment'] for row in off['slurm_schedule']]}}
    fig = plt.figure(figsize=(15, 11), layout='constrained')
    grid = fig.add_gridspec(len(panels), 2, width_ratios=(1, 2.6))
    table_axis = fig.add_subplot(grid[:, 0])
    table_axis.axis('off')
    table_axis.set_title('Workload in submission order', loc='left', fontweight='bold')
    cells = [[label, arrival, nodes, duration] for label, (arrival, nodes, _, duration)
             in zip(('A', 'B', 'C (head)', 'D (fill)'), on['jobs'])]
    table = table_axis.table(cellText=cells, colLabels=['Job', 'Arrival', 'Nodes', 'Duration'],
                            cellLoc='center', bbox=(0, .55, 1, .32))
    table.auto_set_font_size(False)
    table.set_fontsize(11)
    table_axis.text(0, .46, '4 node equivalents total\nFCFS; hybrid backfill\n'
                    'Protected models: R=32, Q=64\nInterval=30 s; duration equals limit',
                    va='top', fontsize=12, linespacing=1.6)
    table_axis.text(0, .18, 'Observed C / D starts (seconds):\n'
                    f'License on: {on["slurm_schedule"][2]["start"]:g} / {on["slurm_schedule"][3]["start"]:g}\n'
                    f'License off: {off["slurm_schedule"][2]["start"]:g} / {off["slurm_schedule"][3]["start"]:g}\n'
                    f'DR_EVT protected: {on["dr_evt_schedule"][2]["start"]:g} / {on["dr_evt_schedule"][3]["start"]:g}\n'
                    f'DR_EVT diagnostic: {off["dr_evt_schedule"][2]["start"]:g} / {off["dr_evt_schedule"][3]["start"]:g}',
                    va='top', fontsize=12, fontweight='bold', linespacing=1.6)
    table_axis.text(0, .045, 'Rows show one feasible placement.\n'
                    'Licenses have no individual node IDs.', fontsize=10, va='top')
    end = max(row['end'] for _, schedule in panels for row in schedule)
    for i, (name, schedule) in enumerate(panels):
        axis = fig.add_subplot(grid[i, 1])
        assigned = assignments(schedule, 4)
        data['assignments'][name] = assigned
        for row in assigned:
            for node in row['assigned_node_equivalents']:
                axis.broken_barh([(row['start'], row['end'] - row['start'])],
                                 (node - .38, .76), facecolors=COLORS[row['job']])
                axis.text((row['start'] + row['end']) / 2, node, row['job'],
                          ha='center', va='center', color='white', fontweight='bold')
            if row['job'] in ('C', 'D'):
                axis.axvline(row['start'], color=COLORS[row['job']], linestyle=':', alpha=.5)
        axis.set(title=name + f' — C starts {schedule[2]["start"]:g} s; D starts {schedule[3]["start"]:g} s', xlim=(0, end + 5),
                 ylim=(3.55, -.55), yticks=range(4), yticklabels=['N0', 'N1', 'N2', 'N3'])
        axis.set_xticks([0, 120, 180, 240, 300, 360])
        axis.grid(axis='x', alpha=.2)
        if i == len(panels) - 1:
            axis.set_xlabel('Time (seconds)')
    fig.suptitle('Four-job scheduling with pooled license capacity\n'
                 'Node equivalents are interchangeable; native dummy nodes do not constrain placement',
                 fontsize=16, fontweight='bold')
    filename = 'four-job-node-assignments' if args.case == 'pooled' else 'reservation-protection-node-assignments'
    for extension in ('pdf', 'svg', 'png'):
        fig.savefig(root / f'{filename}.{extension}', dpi=160)
    (root / f'{filename}-data.json').write_text(json.dumps(data, indent=2) + '\n')
    (root / 'plot_timeline.py').write_bytes(Path(__file__).read_bytes())


if __name__ == '__main__':
    main()
