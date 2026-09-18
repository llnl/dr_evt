#!/usr/bin/env python3
"""Run EASYPower candidate-window sweeps and build comparative reports."""

import argparse
import concurrent.futures
import csv
import itertools
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parents[2]
sys.path.insert(0, str(SCRIPT_DIR / "analysis"))
from compare_schedules import compare_results, summarize  # noqa: E402


DEFINITIONS = {
    "progressive_batch": (
        "An ordered list of trace files loaded one batch at a time but "
        "scheduled and measured as one continuous workload. It produces one "
        "EASY baseline and does not treat the files as independent traces."
    ),
    "capacity_schedule": (
        "A time,total_nodes CSV that changes schedulable capacity without "
        "preempting running jobs. --total-nodes remains the physical maximum; "
        "utilization uses max(scheduled capacity, live allocation) while a "
        "reduction drains."
    ),
    "candidate_job_window": (
        "Maximum number of feasible EASY backfill candidates offered to the "
        "power-aware selector at one decision. It is not the number of queue "
        "entries scanned."
    ),
    "candidate_time_window": (
        "Maximum difference between a candidate's submission time and the "
        "blocked FCFS head's submission time. Zero means unlimited. It is an "
        "arrival-time span through the queue, not a runtime or analysis window."
    ),
    "endpoint": (
        "A physical-window power change: abs(P(t + delta) - P(t)) / delta. "
        "It compares instantaneous power at the two endpoints, does not average "
        "fixed bins, and is unrelated to either scheduler candidate window."
    ),
    "fcfs_and_backfill": (
        "At each timestamp, consecutive jobs starting from the waiting-queue "
        "head are FCFS-prefix jobs. Jobs that start while an earlier head is "
        "blocked are backfill jobs."
    ),
    "arrival_sampled_mean_queue_length": (
        "Jobs already waiting at each arrival, averaged over arrivals; running "
        "jobs are excluded and equal-time arrivals follow input order."
    ),
    "time_weighted_mean_queue_length": (
        "Integral of queue length over the makespan, equivalently total job "
        "waiting time divided by makespan."
    ),
    "bounded_slowdown": "turnaround / max(actual runtime, configured floor)",
    "power_weighted_transition": (
        "abs(delta_P) * mean(P_before, P_after) / P_max, after coalescing all "
        "resource changes at the same timestamp."
    ),
    "normalized_absolute_power_penalty": (
        "integral(abs(P - time-weighted mean P)) / "
        "(makespan * time-weighted mean P)"
    ),
    "time_weighted_power_cv": (
        "Time-weighted population standard deviation of system power divided "
        "by its time-weighted mean."
    ),
    "job_power_density_cv": (
        "Population CV across input jobs of Pcon divided by used CPUs when that "
        "column exists, otherwise requested nodes. A constant CPUs-per-node "
        "conversion does not change the CV."
    ),
    "horizons": (
        "Estimated horizon uses requested time limits; actual-duration horizon "
        "is a fixed-schedule diagnostic using realized runtimes."
    ),
}


KEY_METRICS = (
    "fcfs_jobs",
    "backfill_jobs",
    "fcfs_to_backfill_job_ratio",
    "fcfs_job_fraction",
    "backfill_job_fraction",
    "avgpcon_per_used_cpu_cv",
    "arrival_sampled_mean_queue_length",
    "time_weighted_mean_queue_length",
    "mean_wait_s",
    "mean_turnaround_s",
    "mean_bounded_slowdown",
    "max_wait_s",
    "max_bounded_slowdown",
    "makespan_s",
    "system_utilization",
    "time_weighted_mean_effective_capacity_nodes",
    "time_weighted_mean_power_mw",
    "time_weighted_power_cv",
    "normalized_absolute_power_penalty",
    "p95_power_weighted_transition_mw",
    "p99_power_weighted_transition_mw",
    "max_power_weighted_transition_mw",
    "peak_power_mw",
    "estimated_horizon_time_weighted_mean_s",
    "actual_horizon_time_weighted_mean_s",
    "estimated_to_actual_horizon_time_weighted_ratio",
    "horizon_time_weighted_normalized_absolute_error",
    "target_time_weighted_mean_mw",
)


def comma_ints(text):
    try:
        values = [int(value) for value in text.split(",") if value.strip()]
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error))
    if not values or any(value <= 0 for value in values):
        raise argparse.ArgumentTypeError("provide comma-separated positive integers")
    return values


def duration_seconds(value):
    text = value.strip().lower()
    if text in ("0", "unlimited", "none", "inf"):
        return 0.0
    match = re.fullmatch(r"([0-9]+(?:\.[0-9]+)?)([smhd]?)", text)
    if not match:
        raise argparse.ArgumentTypeError(
            "use seconds or a suffix s, m, h, d (for example 6h)")
    scale = {"": 1.0, "s": 1.0, "m": 60.0, "h": 3600.0,
             "d": 86400.0}[match.group(2)]
    result = float(match.group(1)) * scale
    if result <= 0.0:
        raise argparse.ArgumentTypeError("duration must be positive or unlimited")
    return result


def parse_duration_list(text):
    values = [duration_seconds(value) for value in text.split(",") if value.strip()]
    if not values:
        raise argparse.ArgumentTypeError("provide at least one duration")
    return values


def parse_endpoint_windows(text):
    if not text.strip():
        return []
    values = [duration_seconds(value) for value in text.split(",")]
    if any(value == 0.0 for value in values):
        raise argparse.ArgumentTypeError("endpoint windows cannot be unlimited")
    return values


def slug(text):
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", text).strip("._") or "trace"


def duration_slug(seconds):
    if seconds == 0.0:
        return "unlimited"
    return "{}s".format(format(seconds, "g").replace(".", "p"))


def find_driver(explicit):
    if explicit:
        path = explicit.resolve()
        if not path.is_file():
            raise FileNotFoundError("EASYPower driver not found: {}".format(path))
        return path
    candidates = (
        ROOT / "build/easypower_experiment",
        ROOT / "build/bin/easypower_experiment",
        ROOT / "install/bin/easypower_experiment",
        ROOT / "experimental/fugaku-power/build/easypower_experiment",
    )
    for path in candidates:
        if path.is_file():
            return path.resolve()
    found = shutil.which("easypower_experiment")
    if found:
        return Path(found).resolve()
    raise FileNotFoundError(
        "cannot find easypower_experiment; run "
        "experimental/fugaku-power/scripts/build_easypower_experiment.sh "
        "or pass --driver")


def read_infile_list(path):
    """Read a progressive list using the simulator's path semantics."""
    batch_files = []
    with path.open() as stream:
        for raw_line in stream:
            line = raw_line.rstrip(" \t\r\n")
            if not line:
                continue
            batch = Path(line)
            if not batch.is_absolute():
                # Simulations run with ROOT as cwd, so relative list entries
                # are relative to the repository root, not the list's parent.
                batch = ROOT / batch
            batch_files.append(batch.resolve())
    if not batch_files:
        raise ValueError("infile list contains no trace paths: {}".format(path))
    missing = [batch for batch in batch_files if not batch.is_file()]
    if missing:
        raise FileNotFoundError("batch trace not found: {}".format(missing[0]))
    return batch_files


def run_simulation(driver, mode, simulation_input, output, args, jobs,
                   time_window, progressive=False):
    output.mkdir(parents=True, exist_ok=True)
    if progressive:
        mode += "-progressive"
    command = [
        str(driver), mode, str(simulation_input), str(output),
        str(args.total_nodes),
        str(jobs), format(args.maximum_power, "g"),
        format(args.initial_target, "g"), format(time_window, "g"),
    ]
    if args.capacity_schedule:
        command.append(str(args.capacity_schedule))
    print("RUN " + " ".join(command), flush=True)
    completed = subprocess.run(command, cwd=ROOT, text=True,
                               stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    (output / "run.log").write_text(completed.stdout)
    if completed.returncode:
        raise RuntimeError("simulation failed; see {}".format(output / "run.log"))


def outputs_ready(directory, telemetry=False):
    required = [directory / "jobs.csv", directory / "resources.csv"]
    if telemetry:
        required.append(directory / "target_horizon.csv")
    return all(path.is_file() and path.stat().st_size > 0 for path in required)


def wait_for_shared_baseline(directory, timeout_seconds):
    """Wait for the one producer assigned to this season's EASY baseline."""
    complete_marker = directory / ".complete"
    failure_marker = directory / ".failed"
    deadline = time.monotonic() + timeout_seconds
    while True:
        if complete_marker.is_file() and outputs_ready(directory):
            return
        if failure_marker.is_file():
            raise RuntimeError(
                "shared EASY baseline producer failed; see {}".format(
                    failure_marker))
        if time.monotonic() >= deadline:
            raise TimeoutError(
                "timed out waiting for shared EASY baseline {}".format(
                    directory))
        time.sleep(min(5.0, max(0.0, deadline - time.monotonic())))


def numeric_metrics(metrics):
    return {key: value for key, value in metrics.items()
            if isinstance(value, (int, float)) and not isinstance(value, bool)}


def percent_change(value, baseline):
    if baseline is None or value is None or baseline == 0:
        return None
    return 100.0 * (value - baseline) / abs(baseline)


def write_wide_csv(path, records):
    metric_names = sorted({key for record in records
                           for key in numeric_metrics(record["metrics"])})
    fields = ["trace", "algorithm", "candidate_jobs",
              "candidate_time_window_s"] + metric_names
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for record in records:
            row = {key: record.get(key, "") for key in fields}
            row.update(numeric_metrics(record["metrics"]))
            writer.writerow(row)


def write_long_csv(path, baselines, records, comparisons):
    fields = ["trace", "candidate_jobs", "candidate_time_window_s", "metric",
              "easy", "easypower", "absolute_change", "percent_change",
              "comparison_value"]
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for record in records:
            if record["algorithm"] != "EASYPower":
                continue
            baseline = numeric_metrics(baselines[record["trace"]]["metrics"])
            for metric, value in sorted(numeric_metrics(record["metrics"]).items()):
                if metric not in baseline:
                    continue
                base = baseline[metric]
                writer.writerow({
                    "trace": record["trace"],
                    "candidate_jobs": record["candidate_jobs"],
                    "candidate_time_window_s": record["candidate_time_window_s"],
                    "metric": metric,
                    "easy": base,
                    "easypower": value,
                    "absolute_change": value - base,
                    "percent_change": percent_change(value, base),
                    "comparison_value": "",
                })
        for result in comparisons:
            for metric, value in sorted(
                    numeric_metrics(result["comparison"]).items()):
                writer.writerow({
                    "trace": result["trace"],
                    "candidate_jobs": result["candidate_jobs"],
                    "candidate_time_window_s": (
                        result["candidate_time_window_s"]),
                    "metric": metric,
                    "easy": "",
                    "easypower": "",
                    "absolute_change": "",
                    "percent_change": "",
                    "comparison_value": value,
                })


def format_value(value):
    if value is None:
        return "n/a"
    if isinstance(value, int):
        return str(value)
    return "{:.6g}".format(value)


def write_markdown(path, configuration, baselines, records):
    lines = [
        "# EASYPower candidate-window sweep", "",
        "## Terminology", "",
    ]
    for name, definition in DEFINITIONS.items():
        lines.extend(["- `{}`: {}".format(name, definition), ""])
    lines.extend([
        "Endpoint results are included because they describe physical power "
        "changes over the requested timescales. They should not be interpreted "
        "as candidate-window measurements.", "",
        "## Configuration", "",
        "```json", json.dumps(configuration, indent=2, sort_keys=True), "```", "",
    ])
    for trace in sorted(baselines):
        baseline = baselines[trace]["metrics"]
        lines.extend([
            "## {}".format(trace), "",
            "| Candidate jobs | Candidate time (s) | Metric | EASY | "
            "EASYPower | Change |",
            "|---:|---:|---|---:|---:|---:|",
        ])
        for record in records:
            if record["trace"] != trace or record["algorithm"] != "EASYPower":
                continue
            endpoint_metrics = sorted(
                key for key in record["metrics"]
                if key.startswith("endpoint_") and not key.endswith("_samples"))
            for metric in KEY_METRICS + tuple(endpoint_metrics):
                if metric not in record["metrics"]:
                    continue
                value = record["metrics"][metric]
                base = baseline.get(metric)
                change = percent_change(value, base)
                lines.append("| {} | {} | `{}` | {} | {} | {} |".format(
                    record["candidate_jobs"],
                    format_value(record["candidate_time_window_s"]), metric,
                    format_value(base), format_value(value),
                    "n/a" if change is None else "{:+.2f}%".format(change)))
        lines.append("")
    path.write_text("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "traces", nargs="*", type=Path,
        help="independent single-file workloads")
    parser.add_argument(
        "--infile-list", type=Path,
        help=("ordered progressive-batch list; the whole list is one workload "
              "and cannot be combined with positional traces"))
    parser.add_argument("--total-nodes", type=int, required=True)
    parser.add_argument("--candidate-job-windows", type=comma_ints,
                        default=comma_ints("16,32,64,128,256"))
    parser.add_argument("--candidate-time-windows", type=parse_duration_list,
                        default=parse_duration_list("unlimited,1h,6h"),
                        help="comma-separated arrival spans, e.g. unlimited,1h,6h")
    parser.add_argument("--maximum-power", type=float, default=12e6)
    parser.add_argument("--initial-target", type=float)
    parser.add_argument(
        "--capacity-schedule", type=Path,
        help=("time,total_nodes CSV; --total-nodes remains the physical "
              "maximum"))
    parser.add_argument("--slowdown-bound", type=float, default=10.0)
    parser.add_argument("--endpoint-windows", type=parse_endpoint_windows,
                        default=parse_endpoint_windows("1m,5m,15m"),
                        help="comma-separated endpoint intervals; empty disables")
    parser.add_argument("--endpoint-sample-seconds", type=float, default=1.0)
    parser.add_argument(
        "--parallel-runs", type=int, default=1,
        help="maximum simulator processes to run concurrently (default: 1)")
    parser.add_argument(
        "--shared-baseline-dir", type=Path,
        help="reuse this season-level EASY output directory")
    parser.add_argument(
        "--run-shared-baseline", action="store_true",
        help="produce --shared-baseline-dir in this sweep process")
    parser.add_argument("--baseline-wait-seconds", type=float, default=21600.0)
    parser.add_argument("--driver", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--analysis-only", action="store_true")
    parser.add_argument(
        "--resume", action="store_true",
        help="explicitly reuse an existing output directory and skip complete runs")
    parser.add_argument("--force", action="store_true",
                        help="rerun simulations even when complete outputs exist")
    args = parser.parse_args()
    if args.initial_target is None:
        args.initial_target = args.maximum_power
    if (args.total_nodes <= 0 or args.maximum_power <= 0.0
            or not 0.0 <= args.initial_target <= args.maximum_power):
        parser.error("invalid node count, maximum power, or initial target")
    if (args.slowdown_bound <= 0.0 or args.endpoint_sample_seconds <= 0.0
            or args.parallel_runs <= 0 or args.baseline_wait_seconds <= 0.0):
        parser.error("analysis intervals and --parallel-runs must be positive")
    if args.run_shared_baseline and not args.shared_baseline_dir:
        parser.error("--run-shared-baseline requires --shared-baseline-dir")
    if args.capacity_schedule:
        args.capacity_schedule = args.capacity_schedule.resolve()
        if not args.capacity_schedule.is_file():
            parser.error("capacity schedule not found: {}".format(
                args.capacity_schedule))
    if bool(args.traces) == bool(args.infile_list):
        parser.error("provide positional traces or --infile-list, but not both")

    workloads = []
    if args.infile_list:
        infile_list = args.infile_list.resolve()
        if not infile_list.is_file():
            parser.error("infile list not found: {}".format(infile_list))
        try:
            batch_files = read_infile_list(infile_list)
        except (OSError, ValueError) as error:
            parser.error(str(error))
        workloads.append({
            "input": infile_list,
            "label": slug(infile_list.stem),
            "analysis_inputs": batch_files,
            "progressive": True,
        })
    else:
        traces = [path.resolve() for path in args.traces]
        missing = [path for path in traces if not path.is_file()]
        if missing:
            parser.error("trace not found: {}".format(missing[0]))
        workloads.extend({
            "input": path,
            "label": slug(path.stem),
            "analysis_inputs": [path],
            "progressive": False,
        } for path in traces)

    labels = [workload["label"] for workload in workloads]
    if len(labels) != len(set(labels)):
        parser.error("trace file stems must be unique")
    if args.shared_baseline_dir and len(workloads) != 1:
        parser.error("--shared-baseline-dir requires exactly one workload")

    args.output_dir = args.output_dir.resolve()
    if args.shared_baseline_dir:
        args.shared_baseline_dir = args.shared_baseline_dir.resolve()
    driver = None if args.analysis_only else find_driver(args.driver)
    if (args.output_dir.exists() and any(args.output_dir.iterdir())
            and not (args.analysis_only or args.resume or args.force)):
        parser.error(
            "refusing to overwrite non-empty output directory {}; choose a "
            "new directory or explicitly pass --resume".format(args.output_dir))
    args.output_dir.mkdir(parents=True, exist_ok=True)
    configuration = {
        "traces": [str(workload["input"]) for workload in workloads
                   if not workload["progressive"]],
        "infile_list": (str(workloads[0]["input"])
                        if workloads[0]["progressive"] else None),
        "progressive_batch_files": (
            [str(path) for path in workloads[0]["analysis_inputs"]]
            if workloads[0]["progressive"] else []),
        "total_nodes": args.total_nodes,
        "candidate_job_windows": args.candidate_job_windows,
        "candidate_time_windows_s": args.candidate_time_windows,
        "maximum_power_w": args.maximum_power,
        "initial_target_w": args.initial_target,
        "capacity_schedule": (str(args.capacity_schedule)
                              if args.capacity_schedule else None),
        "slowdown_bound_s": args.slowdown_bound,
        "endpoint_windows_s": args.endpoint_windows,
        "endpoint_sample_seconds": args.endpoint_sample_seconds,
        "parallel_runs": args.parallel_runs,
        "shared_baseline_dir": (
            str(args.shared_baseline_dir) if args.shared_baseline_dir else None),
        "run_shared_baseline": args.run_shared_baseline,
        "driver": str(driver) if driver else None,
    }
    (args.output_dir / "configuration.json").write_text(
        json.dumps(configuration, indent=2, sort_keys=True) + "\n")

    records = []
    baselines = {}
    comparisons = []
    for workload in workloads:
        simulation_input = workload["input"]
        analysis_inputs = workload["analysis_inputs"]
        progressive = workload["progressive"]
        label = workload["label"]
        trace_dir = args.output_dir / label
        baseline_dir = args.shared_baseline_dir or trace_dir / "easy"
        simulation_runs = []
        if not args.shared_baseline_dir or args.run_shared_baseline:
            simulation_runs.append(("easy", baseline_dir, 1, 0.0, False))
        for jobs, time_window in itertools.product(
                args.candidate_job_windows, args.candidate_time_windows):
            run_name = "easypower_n{}_t{}".format(
                jobs, duration_slug(time_window))
            simulation_runs.append((
                "easypower", trace_dir / run_name, jobs, time_window, True))

        if not args.analysis_only:
            pending_runs = [
                run for run in simulation_runs
                if args.force or not outputs_ready(run[1], run[4])
            ]
            try:
                with concurrent.futures.ThreadPoolExecutor(
                        max_workers=args.parallel_runs) as executor:
                    futures = [
                        executor.submit(
                            run_simulation, driver, mode, simulation_input,
                            run_dir, args, jobs, time_window, progressive)
                        for mode, run_dir, jobs, time_window, _ in pending_runs
                    ]
                    for future in futures:
                        future.result()
            except BaseException as error:
                if args.shared_baseline_dir and args.run_shared_baseline:
                    baseline_dir.mkdir(parents=True, exist_ok=True)
                    (baseline_dir / ".failed").write_text(
                        "{}\n".format(error))
                raise

            if args.shared_baseline_dir and args.run_shared_baseline:
                if not outputs_ready(baseline_dir):
                    raise FileNotFoundError(
                        "missing shared EASY outputs in {}".format(baseline_dir))
                complete_marker = baseline_dir / ".complete"
                if not complete_marker.exists():
                    complete_marker.touch()

        if args.shared_baseline_dir and not args.analysis_only:
            wait_for_shared_baseline(
                baseline_dir, args.baseline_wait_seconds)

        if not outputs_ready(baseline_dir):
            raise FileNotFoundError("missing EASY outputs in {}".format(baseline_dir))
        print("ANALYZE {} EASY".format(label), flush=True)
        baseline = summarize(
            "EASY", baseline_dir / "jobs.csv", baseline_dir / "resources.csv",
            args.total_nodes, args.maximum_power, args.endpoint_windows,
            args.endpoint_sample_seconds, args.slowdown_bound, analysis_inputs)
        baseline_record = {
            "trace": label, "algorithm": "EASY", "candidate_jobs": "",
            "candidate_time_window_s": "", "metrics": baseline[0],
        }
        baselines[label] = baseline_record
        records.append(baseline_record)

        for jobs, time_window in itertools.product(
                args.candidate_job_windows, args.candidate_time_windows):
            run_name = "easypower_n{}_t{}".format(jobs, duration_slug(time_window))
            run_dir = trace_dir / run_name
            if not outputs_ready(run_dir, True):
                raise FileNotFoundError(
                    "missing EASYPower outputs in {}".format(run_dir))
            print("ANALYZE {} N={} time={}".format(
                label, jobs, duration_slug(time_window)), flush=True)
            alternative = summarize(
                "EASYPower", run_dir / "jobs.csv", run_dir / "resources.csv",
                args.total_nodes, args.maximum_power, args.endpoint_windows,
                args.endpoint_sample_seconds, args.slowdown_bound, None,
                run_dir / "target_horizon.csv")
            alternative[0].update({
                key: value for key, value in baseline[0].items()
                if key == "job_power_density_denominator"
                or "pcon_per_" in key
            })
            record = {
                "trace": label, "algorithm": "EASYPower",
                "candidate_jobs": jobs,
                "candidate_time_window_s": time_window,
                "metrics": alternative[0],
            }
            records.append(record)
            comparison = compare_results(
                baseline[0], baseline[1], baseline[2], baseline[3],
                alternative[0], alternative[1], alternative[2], alternative[3],
                baseline_dir / "resources.csv", run_dir / "resources.csv")
            comparison.update({
                "trace": label, "candidate_jobs": jobs,
                "candidate_time_window_s": time_window,
            })
            comparisons.append(comparison)
            (run_dir / "comparison.json").write_text(
                json.dumps(comparison, indent=2, sort_keys=True) + "\n")

    report = {
        "configuration": configuration,
        "definitions": DEFINITIONS,
        "baselines": baselines,
        "runs": [record for record in records if record["algorithm"] == "EASYPower"],
        "comparisons": comparisons,
    }
    (args.output_dir / "results.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n")
    write_wide_csv(args.output_dir / "metrics.csv", records)
    write_long_csv(args.output_dir / "comparisons.csv", baselines, records,
                   comparisons)
    write_markdown(args.output_dir / "summary.md", configuration,
                   baselines, records)
    print("Wrote {}".format(args.output_dir / "summary.md"))


if __name__ == "__main__":
    main()
