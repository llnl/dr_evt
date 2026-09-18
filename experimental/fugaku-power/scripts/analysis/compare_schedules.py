#!/usr/bin/env python3
"""Compare EASY/EASYPower schedules using the study's complete metric set.

An endpoint power metric compares the instantaneous system power at the two
ends of a physical interval: abs(P(t + delta) - P(t)) / delta. It does not
average fixed bins, and its delta is unrelated to an EASYPower candidate
window. Endpoint statistics are optional because they add analysis time and
do not measure the scheduler's candidate search directly.
"""

import argparse
import csv
import heapq
import itertools
import json
import math
from pathlib import Path


def percentile(values, fraction):
    if not values:
        return 0.0
    ordered = sorted(values)
    return ordered[round((len(ordered) - 1) * fraction)]


def tail_mean(values, fraction=0.05):
    if not values:
        return 0.0
    ordered = sorted(values)
    count = max(1, int(math.ceil(len(ordered) * fraction)))
    return sum(ordered[-count:]) / count


def population_cv(values):
    if not values:
        return 0.0
    mean = sum(values) / len(values)
    if mean == 0.0:
        return 0.0
    variance = sum((value - mean) ** 2 for value in values) / len(values)
    return math.sqrt(variance) / mean


def weighted_percentile(samples, fraction):
    if not samples:
        return 0.0
    total_weight = sum(weight for _, weight in samples)
    if total_weight <= 0.0:
        return 0.0
    threshold = fraction * total_weight
    accumulated = 0.0
    for value, weight in sorted(samples):
        accumulated += weight
        if accumulated >= threshold:
            return value
    return samples[-1][0]


def read_job_columns(path):
    rows = []
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        required = {"job_submit_time", "begin_time", "end_time", "num_nodes"}
        missing = sorted(required - set(reader.fieldnames or ()))
        if missing:
            raise ValueError("{} is missing columns: {}".format(
                path, ", ".join(missing)))
        for row in reader:
            rows.append({
                "submit": float(row["job_submit_time"]),
                "start": float(row["begin_time"]),
                "end": float(row["end_time"]),
                "nodes": float(row["num_nodes"]),
            })
    if not rows:
        raise ValueError("{} contains no jobs".format(path))
    return rows


def classify_dispatches(rows):
    """Reconstruct FCFS-prefix versus EASY-backfill starts."""
    ordered_ids = sorted(range(len(rows)),
                         key=lambda index: (rows[index]["submit"], index))
    starts = sorted((row["start"], index) for index, row in enumerate(rows))
    started = [False] * len(rows)
    head = 0
    fcfs = []
    backfill = []
    for start_time, group in itertools.groupby(starts, key=lambda item: item[0]):
        group_ids = [index for _, index in group]
        at_time = set(group_ids)
        while True:
            while head < len(ordered_ids) and started[ordered_ids[head]]:
                head += 1
            if head == len(ordered_ids):
                break
            job_id = ordered_ids[head]
            if rows[job_id]["submit"] > start_time or job_id not in at_time:
                break
            fcfs.append(job_id)
            started[job_id] = True
            at_time.remove(job_id)
            head += 1
        for job_id in group_ids:
            if job_id in at_time:
                backfill.append(job_id)
                started[job_id] = True
    return fcfs, backfill


def job_metrics(path, slowdown_bound=10.0):
    rows = read_job_columns(path)
    waits = [row["start"] - row["submit"] for row in rows]
    runtimes = [row["end"] - row["start"] for row in rows]
    turnarounds = [row["end"] - row["submit"] for row in rows]
    slowdowns = [turnaround / max(runtime, slowdown_bound)
                 for turnaround, runtime in zip(turnarounds, runtimes)]
    first_submit = min(row["submit"] for row in rows)
    last_end = max(row["end"] for row in rows)
    horizon = last_end - first_submit

    waiting_starts = []
    arrival_queue_sum = 0
    submit_start_pairs = sorted((row["submit"], row["start"]) for row in rows)
    for submit, group_iterator in itertools.groupby(
            submit_start_pairs, key=lambda pair: pair[0]):
        while waiting_starts and waiting_starts[0] < submit:
            heapq.heappop(waiting_starts)
        group = list(group_iterator)
        count = len(group)
        arrival_queue_sum += count * len(waiting_starts)
        arrival_queue_sum += count * (count - 1) // 2
        for _, start in group:
            heapq.heappush(waiting_starts, start)

    fcfs, backfill = classify_dispatches(rows)
    fcfs_nodes = sum(rows[index]["nodes"] for index in fcfs)
    backfill_nodes = sum(rows[index]["nodes"] for index in backfill)
    fcfs_node_seconds = sum(rows[index]["nodes"] * runtimes[index]
                            for index in fcfs)
    backfill_node_seconds = sum(rows[index]["nodes"] * runtimes[index]
                                for index in backfill)
    total_nodes = fcfs_nodes + backfill_nodes
    total_node_seconds = fcfs_node_seconds + backfill_node_seconds

    result = {
        "jobs": len(rows),
        "mean_wait_s": sum(waits) / len(waits),
        "p95_wait_s": percentile(waits, 0.95),
        "p99_wait_s": percentile(waits, 0.99),
        "top5_mean_wait_s": tail_mean(waits),
        "max_wait_s": max(waits),
        "mean_turnaround_s": sum(turnarounds) / len(turnarounds),
        "p95_turnaround_s": percentile(turnarounds, 0.95),
        "p99_turnaround_s": percentile(turnarounds, 0.99),
        "top5_mean_turnaround_s": tail_mean(turnarounds),
        "max_turnaround_s": max(turnarounds),
        "mean_bounded_slowdown": sum(slowdowns) / len(slowdowns),
        "p95_bounded_slowdown": percentile(slowdowns, 0.95),
        "p99_bounded_slowdown": percentile(slowdowns, 0.99),
        "top5_mean_bounded_slowdown": tail_mean(slowdowns),
        "max_bounded_slowdown": max(slowdowns),
        "bounded_slowdown_floor_s": slowdown_bound,
        "arrival_sampled_mean_queue_length": arrival_queue_sum / len(rows),
        "time_weighted_mean_queue_length": sum(waits) / horizon,
        "makespan_s": horizon,
        "fcfs_jobs": len(fcfs),
        "backfill_jobs": len(backfill),
        "fcfs_to_backfill_job_ratio": (
            len(fcfs) / len(backfill) if backfill else None),
        "fcfs_job_fraction": len(fcfs) / len(rows),
        "backfill_job_fraction": len(backfill) / len(rows),
        "fcfs_requested_node_fraction": fcfs_nodes / total_nodes,
        "backfill_requested_node_fraction": backfill_nodes / total_nodes,
        "fcfs_actual_node_time_fraction": fcfs_node_seconds / total_node_seconds,
        "backfill_actual_node_time_fraction": (
            backfill_node_seconds / total_node_seconds),
    }
    return result, [row["start"] for row in rows], first_submit, last_end


def input_power_density_metrics(paths):
    """Aggregate per-job power density across one trace or an ordered batch."""
    if isinstance(paths, (str, Path)):
        paths = [Path(paths)]
    else:
        paths = [Path(path) for path in paths]
    if not paths:
        raise ValueError("no input traces supplied for power-density metrics")

    values = {name: [] for name in ("avgpcon", "minpcon", "maxpcon")}
    denominator = None
    fields_by_path = []
    for path in paths:
        with path.open(newline="") as stream:
            fields_by_path.append(set(csv.DictReader(stream).fieldnames or ()))
    for candidate in ("used_cpu", "num_cpus", "num_nodes"):
        if all(candidate in fields for fields in fields_by_path):
            denominator = candidate
            break
    if denominator is None:
        raise ValueError(
            "input traces have no common used_cpu, num_cpus, or num_nodes "
            "column")
    available = [name for name in values
                 if all(name in fields for fields in fields_by_path)]
    if not available:
        raise ValueError("input traces have no common Pcon power columns")

    for path in paths:
        with path.open(newline="") as stream:
            reader = csv.DictReader(stream)
            for row in reader:
                divisor = float(row[denominator])
                if divisor <= 0.0:
                    continue
                for name in available:
                    values[name].append(float(row[name]) / divisor)
    result = {"job_power_density_denominator": denominator}
    denominator_label = {
        "used_cpu": "used_cpu",
        "num_cpus": "cpu",
        "num_nodes": "node",
    }[denominator]
    for name, samples in values.items():
        if samples:
            result[name + "_per_" + denominator_label + "_mean_w"] = (
                sum(samples) / len(samples))
            # A constant nodes-to-CPU scale changes the mean but not the CV.
            result[name + "_per_used_cpu_cv"] = population_cv(samples)
    return result


def read_states(path):
    states = []
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        required = {"time", "allocated_nodes", "avgpcon"}
        missing = sorted(required - set(reader.fieldnames or ()))
        if missing:
            raise ValueError("{} is missing columns: {}".format(
                path, ", ".join(missing)))
        for row in reader:
            state = (float(row["time"]), float(row["allocated_nodes"]),
                     float(row["avgpcon"]))
            if states and state[0] == states[-1][0]:
                states[-1] = state
            else:
                states.append(state)
    if not states:
        raise ValueError("{} contains no resource states".format(path))
    return states


def effective_capacity_area(path, start, end):
    """Integrate allocated_nodes + free_nodes from the resource trace."""
    states = []
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        required = {"time", "free_nodes", "allocated_nodes"}
        missing = sorted(required - set(reader.fieldnames or ()))
        if missing:
            raise ValueError("{} is missing columns: {}".format(
                path, ", ".join(missing)))
        for row in reader:
            state = (float(row["time"]),
                     float(row["free_nodes"]) + float(row["allocated_nodes"]))
            if states and states[-1][0] == state[0]:
                states[-1] = state
            else:
                states.append(state)

    area = 0.0
    for index, (time, capacity) in enumerate(states):
        next_time = states[index + 1][0] if index + 1 < len(states) else end
        duration = max(0.0, min(next_time, end) - max(time, start))
        area += capacity * duration
    return area


def endpoint_metrics(states, start, end, windows, sample_seconds=1.0):
    """Evaluate abs(P(t+window)-P(t))/window at physical endpoints."""
    results = {}
    first_sample = math.ceil(start / sample_seconds) * sample_seconds
    for window in windows:
        values = []
        left_index = right_index = 0
        time = first_sample
        while time + window <= end:
            while (left_index + 1 < len(states)
                   and states[left_index + 1][0] <= time):
                left_index += 1
            endpoint = time + window
            while (right_index + 1 < len(states)
                   and states[right_index + 1][0] <= endpoint):
                right_index += 1
            magnitude = abs(states[right_index][2] - states[left_index][2])
            values.append(magnitude / 1e6 / (window / 60.0))
            time += sample_seconds
        prefix = "endpoint_{}s".format(format(window, "g"))
        results[prefix + "_samples"] = len(values)
        results[prefix + "_mean_mw_per_min"] = (
            sum(values) / len(values) if values else 0.0)
        results[prefix + "_p95_mw_per_min"] = percentile(values, 0.95)
        results[prefix + "_p99_mw_per_min"] = percentile(values, 0.99)
        results[prefix + "_max_mw_per_min"] = max(values, default=0.0)
    return results


def resource_metrics(path, start, end, total_nodes, p_max, endpoint_windows,
                     endpoint_sample_seconds=1.0):
    states = read_states(path)
    horizon = end - start
    if horizon <= 0.0:
        raise ValueError("schedule makespan must be positive")
    node_area = energy = power_square_area = absolute_deviation_area = 0.0
    power_samples = []
    peak_nodes = peak_power = max_power_step = 0.0
    time_above = {limit: 0.0 for limit in (10e6, 12e6, 15e6, 17e6)}

    for index, (time, nodes, power) in enumerate(states):
        next_time = states[index + 1][0] if index + 1 < len(states) else end
        left = max(time, start)
        right = min(next_time, end)
        duration = max(0.0, right - left)
        if duration:
            node_area += nodes * duration
            energy += power * duration
            power_square_area += power * power * duration
            power_samples.append((power, duration))
            peak_nodes = max(peak_nodes, nodes)
            peak_power = max(peak_power, power)
            for limit in time_above:
                if power > limit:
                    time_above[limit] += duration

    mean_power = energy / horizon
    for index, (time, _, power) in enumerate(states):
        next_time = states[index + 1][0] if index + 1 < len(states) else end
        duration = max(0.0, min(next_time, end) - max(time, start))
        absolute_deviation_area += abs(power - mean_power) * duration
    variance = max(0.0, power_square_area / horizon - mean_power * mean_power)
    stddev = math.sqrt(variance)

    transitions = []
    previous = None
    for time, _, power in states:
        if time < start:
            previous = power
            continue
        if time > end:
            break
        if previous is not None:
            delta = abs(power - previous)
            max_power_step = max(max_power_step, delta)
            transitions.append(delta * (power + previous) / (2.0 * p_max))
        previous = power

    capacity_area = effective_capacity_area(path, start, end)
    result = {
        "system_utilization": node_area / capacity_area if capacity_area else 0.0,
        "time_weighted_mean_effective_capacity_nodes": capacity_area / horizon,
        "mean_allocated_nodes": node_area / horizon,
        "peak_allocated_nodes": peak_nodes,
        "energy_mwh": energy / 3.6e9,
        "time_weighted_mean_power_mw": mean_power / 1e6,
        "time_weighted_p95_power_mw": weighted_percentile(power_samples, 0.95) / 1e6,
        "time_weighted_p99_power_mw": weighted_percentile(power_samples, 0.99) / 1e6,
        "peak_power_mw": peak_power / 1e6,
        "time_weighted_power_stddev_mw": stddev / 1e6,
        "time_weighted_power_cv": stddev / mean_power if mean_power else 0.0,
        "normalized_absolute_power_penalty": (
            absolute_deviation_area / (horizon * mean_power)
            if mean_power else 0.0),
        "max_power_step_mw": max_power_step / 1e6,
        "p95_power_weighted_transition_mw": percentile(transitions, 0.95) / 1e6,
        "p99_power_weighted_transition_mw": percentile(transitions, 0.99) / 1e6,
        "max_power_weighted_transition_mw": max(transitions, default=0.0) / 1e6,
        **{
            "fraction_time_above_{}mw".format(int(limit / 1e6)): duration / horizon
            for limit, duration in time_above.items()
        },
    }
    if endpoint_windows:
        result.update(endpoint_metrics(states, start, end, endpoint_windows,
                                       endpoint_sample_seconds))
    return result


def horizon_metrics(path, start, end):
    states = []
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        required = {"time", "estimated_horizon_s", "actual_duration_horizon_s",
                    "target_w", "actual_duration_target_w"}
        missing = sorted(required - set(reader.fieldnames or ()))
        if missing:
            raise ValueError("{} is missing columns: {}".format(
                path, ", ".join(missing)))
        for row in reader:
            state = (
                float(row["time"]), float(row["estimated_horizon_s"]),
                float(row["actual_duration_horizon_s"]), float(row["target_w"]),
                float(row["actual_duration_target_w"]),
            )
            if states and states[-1][0] == state[0]:
                states[-1] = state
            else:
                states.append(state)
    if not states:
        return {}

    cycle_est = [state[1] for state in states]
    cycle_actual = [state[2] for state in states]
    weighted = [0.0] * 5
    actual_area = 0.0
    for index, state in enumerate(states):
        right = states[index + 1][0] if index + 1 < len(states) else end
        duration = max(0.0, min(right, end) - max(state[0], start))
        if not duration:
            continue
        weighted[0] += state[1] * duration
        weighted[1] += state[2] * duration
        weighted[2] += abs(state[1] - state[2]) * duration
        weighted[3] += state[3] * duration
        weighted[4] += state[4] * duration
        actual_area += state[2] * duration
    horizon = end - start
    return {
        "horizon_cycle_samples": len(states),
        "estimated_horizon_cycle_mean_s": sum(cycle_est) / len(cycle_est),
        "actual_horizon_cycle_mean_s": sum(cycle_actual) / len(cycle_actual),
        "horizon_cycle_mean_signed_error_s": sum(
            estimated - actual
            for estimated, actual in zip(cycle_est, cycle_actual)) / len(states),
        "horizon_cycle_mean_absolute_error_s": sum(
            abs(estimated - actual)
            for estimated, actual in zip(cycle_est, cycle_actual)) / len(states),
        "estimated_horizon_time_weighted_mean_s": weighted[0] / horizon,
        "actual_horizon_time_weighted_mean_s": weighted[1] / horizon,
        "horizon_time_weighted_mean_signed_error_s": (
            (weighted[0] - weighted[1]) / horizon),
        "horizon_time_weighted_mean_absolute_error_s": weighted[2] / horizon,
        "horizon_time_weighted_normalized_absolute_error": (
            weighted[2] / actual_area if actual_area else 0.0),
        "estimated_to_actual_horizon_time_weighted_ratio": (
            weighted[0] / weighted[1] if weighted[1] else None),
        "target_time_weighted_mean_mw": weighted[3] / horizon / 1e6,
        "actual_duration_target_time_weighted_mean_mw": weighted[4] / horizon / 1e6,
    }


def profile_difference(baseline_path, alternative_path, start, end):
    baseline = read_states(baseline_path)
    alternative = read_states(alternative_path)
    times = sorted(
        {time for time, _, _ in baseline if start <= time <= end}
        | {time for time, _, _ in alternative if start <= time <= end}
        | {start, end})
    baseline_index = alternative_index = 0
    baseline_power = alternative_power = 0.0
    baseline_nodes = alternative_nodes = 0.0
    absolute_power_area = squared_power_area = absolute_node_area = 0.0
    changed_time = max_power_difference = 0.0
    for index, time in enumerate(times[:-1]):
        while baseline_index < len(baseline) and baseline[baseline_index][0] <= time:
            _, baseline_nodes, baseline_power = baseline[baseline_index]
            baseline_index += 1
        while (alternative_index < len(alternative)
               and alternative[alternative_index][0] <= time):
            _, alternative_nodes, alternative_power = alternative[alternative_index]
            alternative_index += 1
        duration = times[index + 1] - time
        power_difference = abs(alternative_power - baseline_power)
        node_difference = abs(alternative_nodes - baseline_nodes)
        absolute_power_area += power_difference * duration
        squared_power_area += power_difference * power_difference * duration
        absolute_node_area += node_difference * duration
        max_power_difference = max(max_power_difference, power_difference)
        if power_difference > 1e-6:
            changed_time += duration
    horizon = end - start
    return {
        "power_profile_mae_mw": absolute_power_area / horizon / 1e6,
        "power_profile_rmse_mw": math.sqrt(squared_power_area / horizon) / 1e6,
        "maximum_instantaneous_power_difference_mw": max_power_difference / 1e6,
        "mean_absolute_node_difference": absolute_node_area / horizon,
        "fraction_time_power_profile_differs": changed_time / horizon,
    }


def summarize(label, jobs_path, resources_path, total_nodes, p_max,
              endpoint_windows=(), endpoint_sample_seconds=1.0,
              slowdown_bound=10.0, input_trace=None, telemetry_path=None):
    jobs, starts, first_submit, last_end = job_metrics(jobs_path, slowdown_bound)
    resources = resource_metrics(resources_path, first_submit, last_end,
                                 total_nodes, p_max, endpoint_windows,
                                 endpoint_sample_seconds)
    result = {"label": label, **jobs, **resources}
    if input_trace:
        result.update(input_power_density_metrics(input_trace))
    if telemetry_path:
        result.update(horizon_metrics(telemetry_path, first_submit, last_end))
    return result, starts, first_submit, last_end


def compare_results(baseline, baseline_starts, baseline_start, baseline_end,
                    alternative, alternative_starts, alternative_start,
                    alternative_end, baseline_resources, alternative_resources):
    if len(baseline_starts) != len(alternative_starts):
        raise ValueError("the schedules contain different job counts")
    start_deltas = [alternative - baseline for baseline, alternative in
                    zip(baseline_starts, alternative_starts)]
    changed = [delta for delta in start_deltas if delta != 0.0]
    return {
        "baseline": baseline,
        "alternative": alternative,
        "comparison": {
            "jobs_with_changed_start": len(changed),
            "jobs_starting_earlier": sum(delta < 0 for delta in start_deltas),
            "jobs_starting_later": sum(delta > 0 for delta in start_deltas),
            "net_wait_change_s": sum(start_deltas),
            "mean_start_change_among_changed_jobs_s": (
                sum(changed) / len(changed) if changed else 0.0),
            "largest_start_advance_s": max(
                (-delta for delta in changed if delta < 0.0), default=0.0),
            "largest_start_delay_s": max(
                (delta for delta in changed if delta > 0.0), default=0.0),
            **profile_difference(
                baseline_resources, alternative_resources,
                min(baseline_start, alternative_start),
                max(baseline_end, alternative_end)),
        },
    }


def parse_windows(text):
    if not text.strip():
        return []
    values = [float(value) for value in text.split(",")]
    if any(value <= 0.0 for value in values):
        raise argparse.ArgumentTypeError("endpoint windows must be positive")
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline_jobs", type=Path)
    parser.add_argument("baseline_resources", type=Path)
    parser.add_argument("alternative_jobs", type=Path)
    parser.add_argument("alternative_resources", type=Path)
    parser.add_argument("--total-nodes", type=int, required=True)
    parser.add_argument("--p-max", type=float, required=True,
                        help="P_max in watts for transition normalization")
    parser.add_argument("--input-trace", type=Path,
                        help="source Pcon trace for per-used-CPU power CV")
    parser.add_argument("--horizon-telemetry", type=Path,
                        help="EASYPower target_horizon.csv")
    parser.add_argument("--slowdown-bound", type=float, default=10.0)
    parser.add_argument("--endpoint-windows", type=parse_windows,
                        default=parse_windows("60,300,900"), metavar="SECONDS",
                        help=("comma-separated physical endpoint intervals; "
                              "empty disables"))
    parser.add_argument("--endpoint-sample-seconds", type=float, default=1.0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.total_nodes <= 0 or args.p_max <= 0.0:
        parser.error("--total-nodes and --p-max must be positive")
    if args.slowdown_bound <= 0.0 or args.endpoint_sample_seconds <= 0.0:
        parser.error("slowdown bound and endpoint sample interval must be positive")

    baseline, baseline_starts, baseline_start, baseline_end = summarize(
        "EASY", args.baseline_jobs, args.baseline_resources, args.total_nodes,
        args.p_max, args.endpoint_windows, args.endpoint_sample_seconds,
        args.slowdown_bound, args.input_trace)
    alternative_result = summarize(
        "EASYPower", args.alternative_jobs, args.alternative_resources,
        args.total_nodes, args.p_max, args.endpoint_windows,
        args.endpoint_sample_seconds, args.slowdown_bound, args.input_trace,
        args.horizon_telemetry)
    alternative, alternative_starts, alternative_start, alternative_end = (
        alternative_result)
    result = compare_results(
        baseline, baseline_starts, baseline_start, baseline_end,
        alternative, alternative_starts, alternative_start, alternative_end,
        args.baseline_resources, args.alternative_resources)
    result["definitions"] = {
        "endpoint": (
            "abs(P(t + delta) - P(t)) / delta at physical interval endpoints; "
            "no bin averaging"),
        "power_weighted_transition": (
            "abs(delta_P) * mean(P_before, P_after) / P_max"),
        "normalized_absolute_power_penalty": (
            "integral(abs(P - time_weighted_mean(P))) / "
            "(makespan * time_weighted_mean(P))"),
        "bounded_slowdown": "turnaround / max(actual_runtime, slowdown_bound)",
        "candidate_window_note": (
            "post-processing endpoint windows are unrelated to scheduler "
            "candidate windows"),
        "capacity_schedule": (
            "utilization uses max(scheduled capacity, live allocation) while "
            "a non-preemptive capacity reduction drains"),
    }
    rendered = json.dumps(result, indent=2, sort_keys=True)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n")
    print(rendered)


if __name__ == "__main__":
    main()
