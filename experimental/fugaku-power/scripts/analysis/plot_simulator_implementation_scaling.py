#!/usr/bin/env python3
"""Plot Python-reference and C++ simulator implementation scaling."""

import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


REQUIRED_COLUMNS = (
    "case",
    "jobs",
    "python_wall_seconds",
    "cpp_wall_seconds",
    "python_peak_rss_kib",
    "cpp_peak_rss_kib",
)


def read_measurements(path):
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        missing = [name for name in REQUIRED_COLUMNS
                   if name not in (reader.fieldnames or [])]
        if missing:
            raise ValueError(
                f"{path} is missing required columns: {', '.join(missing)}")

        rows = []
        for row in reader:
            rows.append({
                "case": row["case"],
                **{name: float(row[name]) for name in REQUIRED_COLUMNS
                   if name != "case"},
            })

    if not rows:
        raise ValueError(f"{path} contains no measurements")
    return rows


def plot(rows, output):
    jobs = [row["jobs"] / 1000.0 for row in rows]
    python_time = [row["python_wall_seconds"] for row in rows]
    cpp_time = [row["cpp_wall_seconds"] for row in rows]
    python_memory = [row["python_peak_rss_kib"] / (1024.0 * 1024.0)
                     for row in rows]
    cpp_memory = [row["cpp_peak_rss_kib"] / (1024.0 * 1024.0)
                  for row in rows]

    fig, time_axis = plt.subplots(figsize=(11, 5))
    memory_axis = time_axis.twinx()

    python_color = "#1f77b4"
    cpp_color = "#ff7f0e"
    time_axis.plot(jobs, python_time, color=python_color, marker="o",
                   label="Python time")
    time_axis.plot(jobs, cpp_time, color=cpp_color, marker="o",
                   label="C++ time")
    memory_axis.plot(jobs, python_memory, color=python_color, marker="s",
                     linestyle="--", label="Python memory")
    memory_axis.plot(jobs, cpp_memory, color=cpp_color, marker="s",
                     linestyle="--", label="C++ memory")

    time_axis.set(xlabel="Jobs (thousands)",
                  ylabel="Wall time (seconds)")
    memory_axis.set_ylabel("Peak resident memory (GiB, log scale)")
    memory_axis.set_yscale("log")
    time_axis.grid(alpha=0.25)

    lines = time_axis.lines + memory_axis.lines
    time_axis.legend(lines, [line.get_label() for line in lines],
                     loc="upper left")

    last = rows[-1]
    memory_axis.annotate(
        f"Two-month Python: {last['python_peak_rss_kib'] / (1024 ** 2):.2f} GiB",
        xy=(jobs[-1], python_memory[-1]),
        xytext=(-180, -30),
        textcoords="offset points",
        arrowprops={"arrowstyle": "->", "color": "#555555"},
    )

    time_axis.set_title("Simulator implementation scaling on LLNL Dane")
    fig.tight_layout()
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=180)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("measurements", type=Path)
    parser.add_argument("--output", type=Path,
                        default=Path("simulator_implementation_scaling.png"))
    args = parser.parse_args()

    rows = read_measurements(args.measurements)
    plot(rows, args.output)
    print(f"points={len(rows)}")
    print(f"largest_job_count={int(rows[-1]['jobs'])}")
    print(f"output={args.output}")


if __name__ == "__main__":
    main()
