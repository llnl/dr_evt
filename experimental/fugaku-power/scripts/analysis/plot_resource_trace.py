#!/usr/bin/env python3
"""Plot node allocation and aggregate power usage from a resource trace."""

import argparse
import csv
import math
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


REQUIRED_COLUMNS = (
    "time",
    "allocated_nodes",
    "avgpcon",
    "minpcon",
    "maxpcon",
)

# Keep the actual plotting rectangle identical between separately saved
# figures.  tight_layout() adjusts this rectangle from tick-label widths, so
# the node and power plots otherwise have slightly different day-axis widths.
FIGURE_SIZE = (11, 4.5)
PLOT_MARGINS = {
    "left": 0.10,
    "right": 0.98,
    "bottom": 0.14,
    "top": 0.90,
}


def read_trace(path, max_points):
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        missing = [name for name in REQUIRED_COLUMNS
                   if name not in (reader.fieldnames or [])]
        if missing:
            raise ValueError(
                f"{path} is missing required columns: {', '.join(missing)}")

        rows = [tuple(float(row[name]) for name in REQUIRED_COLUMNS)
                for row in reader]

    if not rows:
        raise ValueError(f"{path} contains no resource samples")

    # The C++ simulator writes an initial idle-state sample at time zero even
    # when the trace itself uses epoch timestamps.  Exclude that sentinel from
    # plotting so it does not stretch the elapsed-time axis by several decades.
    plot_rows = rows
    if (len(rows) > 1 and rows[0][0] == 0
            and rows[1][0] > 365 * 24 * 60 * 60):
        plot_rows = rows[1:]

    stride = max(1, math.ceil(len(plot_rows) / max_points))
    sampled = plot_rows[::stride]
    if sampled[-1] != plot_rows[-1]:
        sampled.append(plot_rows[-1])
    return rows, sampled


def save_plots(sampled, output_dir, total_nodes, mode):
    start = sampled[0][0]
    days = [(row[0] - start) / 86400.0 for row in sampled]

    if mode == "replay":
        qualifier = "historical"
        time_label = "replay"
        filename_prefix = "fugaku-replay-"
        node_reference_label = (
            f"replay maximum reference ({total_nodes:,} nodes)")
    else:
        qualifier = "simulated"
        time_label = "simulation"
        filename_prefix = "fugaku-"
        node_reference_label = (
            f"configured simulation limit ({total_nodes:,} nodes)")

    fig, axis = plt.subplots(figsize=FIGURE_SIZE)
    axis.plot(days, [row[1] for row in sampled], linewidth=0.7,
              color="#1f77b4")
    axis.axhline(total_nodes, color="#555555", linestyle="--", linewidth=0.8,
                 label=node_reference_label)
    axis.set(xlabel=f"Elapsed {time_label} time (days)",
             ylabel="Allocated nodes",
             title=f"Fugaku {qualifier} node allocation")
    axis.grid(alpha=0.25)
    axis.legend(loc="upper right")
    fig.subplots_adjust(**PLOT_MARGINS)
    fig.savefig(output_dir / f"{filename_prefix}node-allocation.png", dpi=180)
    plt.close(fig)

    fig, axis = plt.subplots(figsize=FIGURE_SIZE)
    axis.plot(days, [row[3] for row in sampled], linewidth=0.65,
              label="minimum power usage")
    axis.plot(days, [row[2] for row in sampled], linewidth=0.65,
              label="average power usage")
    axis.plot(days, [row[4] for row in sampled], linewidth=0.65,
              label="maximum power usage")
    axis.set(xlabel=f"Elapsed {time_label} time (days)",
             ylabel="Aggregate power usage (W)",
             title=f"Fugaku {qualifier} power usage")
    axis.grid(alpha=0.25)
    axis.legend(loc="upper right")
    fig.subplots_adjust(**PLOT_MARGINS)
    fig.savefig(output_dir / f"{filename_prefix}power-usage.png", dpi=180)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("resource_trace", type=Path)
    parser.add_argument("--output-dir", type=Path, default=Path.cwd())
    parser.add_argument("--total-nodes", type=int, default=158976)
    parser.add_argument("--max-points", type=int, default=50000)
    parser.add_argument(
        "--mode", choices=("simulation", "replay"), default="simulation",
        help="Select plot labels and output filenames (default: simulation)")
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    rows, sampled = read_trace(args.resource_trace, args.max_points)
    save_plots(sampled, args.output_dir, args.total_nodes, args.mode)

    peak_nodes = max(row[1] for row in rows)
    print(f"samples={len(rows)} plotted={len(sampled)}")
    print(f"peak_allocated_nodes={peak_nodes:.0f}")
    print(f"peak_utilization_percent={100.0 * peak_nodes / args.total_nodes:.3f}")
    print(f"output_dir={args.output_dir}")


if __name__ == "__main__":
    main()
