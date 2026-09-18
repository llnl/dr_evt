#!/usr/bin/env python3
"""Collect per-half EASYPower sweep outputs into CSV and Markdown tables."""

import argparse
import csv
import re
from pathlib import Path


KEY_METRICS = (
    "fcfs_to_backfill_job_ratio",
    "avgpcon_per_used_cpu_cv",
    "arrival_sampled_mean_queue_length",
    "estimated_to_actual_horizon_time_weighted_ratio",
    "time_weighted_mean_power_mw",
    "makespan_s",
    "time_weighted_power_cv",
    "normalized_absolute_power_penalty",
    "mean_turnaround_s",
    "mean_bounded_slowdown",
    "max_wait_s",
    "max_bounded_slowdown",
    "max_power_weighted_transition_mw",
    "system_utilization",
)


def fiscal_key(path):
    match = re.fullmatch(r"FY(\d+)-(H[12])", path.name)
    if not match:
        raise ValueError("invalid fiscal-half directory: {}".format(path))
    return int(match.group(1)), int(match.group(2)[1])


def job_window(path):
    match = re.fullmatch(r"job-window-(\d+)", path.name)
    if not match:
        raise ValueError("invalid job-window directory: {}".format(path))
    return int(match.group(1))


def read_csv(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def write_csv(path, rows, leading_fields):
    fields = list(leading_fields)
    fields.extend(sorted({key for row in rows for key in row} - set(fields)))
    with path.open("x", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def markdown_value(value):
    if value in (None, ""):
        return "n/a"
    try:
        return "{:.6g}".format(float(value))
    except ValueError:
        return str(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_directory", type=Path)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()

    run_directory = args.run_directory.resolve()
    metric_paths = sorted(
        run_directory.glob("FY*-H*/job-window-*/metrics.csv"),
        key=lambda path: (fiscal_key(path.parent.parent), job_window(path.parent)),
    )
    if not metric_paths:
        parser.error("no fiscal-half metrics.csv files found under {}".format(
            run_directory))

    metrics = []
    comparisons = []
    baseline_by_half = {}
    for metrics_path in metric_paths:
        half_dir = metrics_path.parent.parent
        fiscal_year, half_number = fiscal_key(half_dir)
        window = job_window(metrics_path.parent)
        for row in read_csv(metrics_path):
            prefix = {
                "fiscal_year": fiscal_year,
                "fiscal_half": "H{}".format(half_number),
                "season": half_dir.name,
                "job_window_run": window,
            }
            combined = {**prefix, **row}
            if row["algorithm"] == "EASY":
                previous = baseline_by_half.get(half_dir.name)
                comparable = {key: value for key, value in combined.items()
                              if key != "job_window_run"}
                if previous is not None and previous != comparable:
                    raise ValueError(
                        "EASY baseline differs across job windows for {}".format(
                            half_dir.name))
                baseline_by_half[half_dir.name] = comparable
            else:
                metrics.append(combined)

        comparison_path = metrics_path.with_name("comparisons.csv")
        if not comparison_path.is_file():
            raise FileNotFoundError(comparison_path)
        for row in read_csv(comparison_path):
            comparisons.append({
                "fiscal_year": fiscal_year,
                "fiscal_half": "H{}".format(half_number),
                "season": half_dir.name,
                "job_window_run": window,
                **row,
            })

    for season in sorted(baseline_by_half,
                         key=lambda value: fiscal_key(Path(value))):
        metrics.append(baseline_by_half[season])
    metrics.sort(key=lambda row: (
        int(row["fiscal_year"]), int(row["fiscal_half"][1:]),
        row["algorithm"] != "EASY",
        int(row["candidate_jobs"] or 0),
        float(row["candidate_time_window_s"] or 0.0),
    ))
    comparisons.sort(key=lambda row: (
        int(row["fiscal_year"]), int(row["fiscal_half"][1:]),
        int(row["candidate_jobs"] or 0),
        float(row["candidate_time_window_s"] or 0.0), row["metric"],
    ))

    output_dir = (args.output_dir or run_directory / "aggregate").resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    metrics_output = output_dir / "fiscal_half_metrics.csv"
    comparisons_output = output_dir / "fiscal_half_comparisons.csv"
    markdown_output = output_dir / "fiscal_half_summary.md"
    existing = [path for path in (metrics_output, comparisons_output,
                                  markdown_output) if path.exists()]
    if existing:
        parser.error("refusing to overwrite {}; choose a new --output-dir".format(
            existing[0]))

    write_csv(
        metrics_output, metrics,
        ("fiscal_year", "fiscal_half", "season", "job_window_run", "trace",
         "algorithm", "candidate_jobs", "candidate_time_window_s"))
    write_csv(
        comparisons_output, comparisons,
        ("fiscal_year", "fiscal_half", "season", "job_window_run", "trace",
         "candidate_jobs", "candidate_time_window_s", "metric"))

    columns = ("season", "algorithm", "candidate_jobs",
               "candidate_time_window_s") + KEY_METRICS
    lines = ["# Fiscal-half comparison sweep", "",
             "| " + " | ".join(columns) + " |",
             "|" + "|".join("---" for _ in columns) + "|"]
    for row in metrics:
        lines.append("| " + " | ".join(
            markdown_value(row.get(column)) for column in columns) + " |")
    with markdown_output.open("x") as stream:
        stream.write("\n".join(lines) + "\n")

    print(metrics_output)
    print(comparisons_output)
    print(markdown_output)


if __name__ == "__main__":
    main()
