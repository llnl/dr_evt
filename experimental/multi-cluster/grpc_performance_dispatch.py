#!/usr/bin/env python3
"""Run the sampled, performance-aware multi-cluster policy over gRPC.

Use this script as the rank-0 client of ``grpc_mpi_launcher.py``. Each other
MPI rank hosts one independent DR_EVT gRPC server. The policy matches the
native C++/MPI experiment, although Python and C++ do not produce the same
sample sequence for a given seed because their random-number engines differ.
"""

import argparse
import csv
import math
import pathlib
import random
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "python"))

from grpc_multi_server import (  # noqa: E402
    DEFAULT_QUEUE_INPUT,
    QUEUE_FIELD,
    ServerSession,
    load_stubs,
)


REQUIREMENTS = {"CPU-only", "GPU-only", "GPU-portable"}


def csv_reader(path):
    """Open a CSV and normalize an optional ``#`` on its first header."""
    stream = path.open(newline="")
    reader = csv.DictReader(stream)
    if reader.fieldnames and reader.fieldnames[0].startswith("#"):
        reader.fieldnames[0] = reader.fieldnames[0][1:]
    return stream, reader


def require_fields(path, reader, fields):
    """Reject a CSV that does not contain every requested field."""
    if not reader.fieldnames or not set(fields).issubset(reader.fieldnames):
        raise ValueError(f"{path} must have columns: {', '.join(fields)}")


def read_arrivals(path):
    """Read and validate a chronologically ordered streaming job trace."""
    stream, reader = csv_reader(path)
    with stream:
        submit_field = "submit_time" if "submit_time" in reader.fieldnames else "job_submit_time"
        require_fields(path, reader, (submit_field, "num_nodes", "time_limit"))
        duration_field = (
            "actual_run_time"
            if "actual_run_time" in reader.fieldnames
            else "duration"
        )
        if duration_field not in reader.fieldnames:
            raise ValueError(f"{path} must have actual_run_time or duration")
        jobs = []
        for index, row in enumerate(reader):
            job = {
                "job_id": (row.get("job_id") or str(index)).strip(),
                "submit_time": float(row[submit_field]),
                "num_nodes": int(row["num_nodes"]),
                "queue": (row.get(QUEUE_FIELD) or DEFAULT_QUEUE_INPUT).strip(),
                "duration": float(row[duration_field]),
                "limit_time": float(row["time_limit"]),
            }
            if (
                job["num_nodes"] <= 0
                or not math.isfinite(job["submit_time"])
                or not math.isfinite(job["duration"])
                or not math.isfinite(job["limit_time"])
                or job["duration"] <= 0
                or job["limit_time"] <= 0
                or job["duration"] > job["limit_time"]
            ):
                raise ValueError(f"{path}: job {job['job_id']} has invalid data")
            jobs.append(job)
    if any(a["submit_time"] > b["submit_time"] for a, b in zip(jobs, jobs[1:])):
        raise ValueError(f"{path}: jobs must be sorted by submit_time")
    return jobs


def read_applications(path):
    """Read the application-to-system-requirement mapping."""
    stream, reader = csv_reader(path)
    with stream:
        require_fields(path, reader, ("app", "sys_requirement"))
        applications = {}
        for row in reader:
            app = row["app"].strip()
            requirement = row["sys_requirement"].strip()
            if not app:
                raise ValueError(f"{path}: app must not be empty")
            if requirement not in REQUIREMENTS:
                raise ValueError(
                    f"{path}: invalid sys_requirement {requirement!r} for {app}"
                )
            if app in applications:
                raise ValueError(f"{path}: duplicate app {app}")
            applications[app] = requirement
    if not applications:
        raise ValueError(f"{path}: applications table is empty")
    return applications


def read_systems(path):
    """Read machine capacities and infer their performance-column names."""
    stream, reader = csv_reader(path)
    with stream:
        require_fields(path, reader, ("machine", "size", "GPU"))
        systems = []
        seen = set()
        for row in reader:
            system_id = row["machine"].strip()
            size = int(row["size"])
            machine_type = row["GPU"].strip()
            if not system_id or size <= 0:
                raise ValueError(f"{path}: invalid machine row")
            if system_id in seen:
                raise ValueError(f"{path}: duplicate machine {system_id}")
            if machine_type not in {"CPU-only", "GPU-enabled"}:
                raise ValueError(f"{path}: invalid GPU value {machine_type!r}")
            seen.add(system_id)
            gpu_enabled = machine_type == "GPU-enabled"
            systems.append(
                {
                    "system_id": system_id,
                    "capacity": size,
                    "gpu_enabled": gpu_enabled,
                    "cpu_column": f"{system_id}-cpu" if gpu_enabled else system_id,
                    "gpu_column": f"{system_id}-gpu" if gpu_enabled else None,
                }
            )
    if not systems:
        raise ValueError(f"{path}: machines table is empty")
    return systems


def parse_performance(path, row_number, row, column):
    """Return one optional positive relative-performance value."""
    if not row[column].strip():
        return None
    value = float(row[column])
    if not math.isfinite(value) or value <= 0:
        raise ValueError(
            f"{path}: row {row_number}: {column} must be empty or positive"
        )
    return value


def read_performance_table(path, columns):
    """Read one performance table indexed by its workload identity."""
    stream, reader = csv_reader(path)
    with stream:
        names = set(reader.fieldnames or ())
        identity_fields = tuple(
            upper if upper in names else lower
            for upper, lower in (("App", "app"), ("Args", "args"), ("Ranks", "ranks"))
        )
        require_fields(path, reader, (*identity_fields, *columns))
        rows = {}
        for row_number, row in enumerate(reader, start=2):
            identity = (
                row[identity_fields[0]].strip(),
                row[identity_fields[1]],
                int(row[identity_fields[2]]),
            )
            if not identity[0] or identity[2] <= 0:
                raise ValueError(f"{path}: invalid workload at row {row_number}")
            if identity in rows:
                raise ValueError(f"{path}: duplicate workload {identity}")
            rows[identity] = {
                column: parse_performance(path, row_number, row, column)
                for column in columns
            }
    return rows


def read_workloads(ground_truth_path, prediction_path, systems, requirements):
    """Read runnable workloads whose applications are in the requirements map."""
    columns = []
    for system in systems:
        columns.append(system["cpu_column"])
        if system["gpu_column"]:
            columns.append(system["gpu_column"])
    ground_truth = read_performance_table(ground_truth_path, columns)
    prediction = read_performance_table(prediction_path, columns)
    ground_truth = {
        identity: values
        for identity, values in ground_truth.items()
        if identity[0] in requirements
    }
    prediction = {
        identity: values
        for identity, values in prediction.items()
        if identity[0] in requirements
    }
    extra_prediction = prediction.keys() - ground_truth.keys()
    if extra_prediction:
        raise ValueError(
            "prediction contains workload identities absent from ground truth "
            f"(extra prediction: {len(extra_prediction)})"
        )

    workloads = {}
    unavailable_rows = 0
    for identity, truth_values in ground_truth.items():
        app, workload_args, ranks = identity
        prediction_values = prediction.get(identity, {})
        workload = {
            "app": app,
            "args": workload_args,
            "ranks": ranks,
            "sys_requirement": requirements[app],
            "performance": [],
        }
        runnable = False
        for system in systems:
            performance = {}
            for mode, column in (
                ("CPU", system["cpu_column"]),
                ("GPU", system["gpu_column"]),
            ):
                if column is None:
                    performance[mode] = None
                    continue
                actual = truth_values[column]
                predicted = prediction_values.get(column)
                performance[mode] = (
                    None
                    if actual is None
                    else {"ground_truth": actual, "predicted": predicted}
                )
            if workload["sys_requirement"] == "CPU-only":
                runnable = runnable or performance["CPU"] is not None
            elif workload["sys_requirement"] == "GPU-only":
                runnable = runnable or performance["GPU"] is not None
            else:
                runnable = runnable or any(
                    value is not None for value in performance.values()
                )
            workload["performance"].append(performance)
        if not runnable:
            unavailable_rows += 1
            continue

        workloads.setdefault(app, []).append(workload)

    if not workloads:
        raise ValueError("performance tables have no runnable rows")
    if unavailable_rows:
        print(
            f"Skipped {unavailable_rows} workload rows with no measurement "
            "for a compatible configured system",
            file=sys.stderr,
        )
    return workloads


def sample_workload(workloads, generator):
    """Uniformly sample an application, then one workload within it."""
    app = generator.choice(tuple(workloads))
    return generator.choice(workloads[app])


def estimate_release_wait(window, required_nodes):
    """Return the first projected release with sufficient capacity."""
    available = window.available_nodes
    if available >= required_nodes:
        return 0.0
    for release in window.releases:
        available += release.nodes_released
        if available >= required_nodes:
            return max(0.0, release.time - window.current_time)
    return math.inf


def estimate_wait(window, required_nodes, runtime, prediction_horizon):
    """Estimate wait from immediate EASY fit or queued resource-time."""
    has_waiting_head = window.shadow_time > window.current_time
    can_start_now = window.available_nodes >= required_nodes
    can_backfill_now = can_start_now and (
        not has_waiting_head or window.current_time + runtime <= window.shadow_time
    )
    if can_backfill_now:
        return 0.0
    if not has_waiting_head:
        return estimate_release_wait(window, required_nodes)
    return window.shadow_time - window.current_time + prediction_horizon


def execution_performance(
    workload,
    system,
    index,
    duration=0.0,
    max_time_limit=math.inf,
    prefer_gpu=False,
    require_prediction=True,
):
    """Return a compatible mode, optionally requiring a prediction."""
    performance = workload["performance"][index]
    requirement = workload["sys_requirement"]

    def eligible(value):
        return (
            value is not None
            and duration / value["ground_truth"] <= max_time_limit
            and (not require_prediction or value["predicted"] is not None)
        )

    if requirement == "CPU-only":
        value = performance["CPU"]
        return ("CPU", value) if eligible(value) else None
    if requirement == "GPU-only":
        value = performance["GPU"] if system["gpu_enabled"] else None
        return ("GPU", value) if eligible(value) else None
    if not system["gpu_enabled"]:
        value = performance["CPU"]
        return ("CPU", value) if eligible(value) else None
    candidates = [
        (mode, performance[mode])
        for mode in ("CPU", "GPU")
        if eligible(performance[mode])
    ]
    if prefer_gpu:
        return next(
            (candidate for candidate in reversed(candidates)),
            None,
        )
    return max(candidates, key=lambda item: item[1]["predicted"], default=None)


def adjusted_time_limit(predicted_limit, actual_duration, maximum_limit):
    """Double a predicted limit until it admits the known runtime or hits the cap."""
    limit = min(predicted_limit, maximum_limit)
    doublings = 0
    while limit < actual_duration and limit < maximum_limit:
        limit = min(limit * 2, maximum_limit)
        doublings += 1
    return limit, doublings


def choose_system(
    job,
    workload,
    systems,
    windows,
    horizons,
    max_time_limit=math.inf,
    dispatch_policy="turnaround",
    wall_time_policy="adapted-limit",
):
    """Choose a system, falling back to wait when no prediction is available."""
    if dispatch_policy not in {"turnaround", "RelPerfOnly", "WaitTimeOnly"}:
        raise ValueError(f"unknown dispatch policy: {dispatch_policy}")
    if wall_time_policy not in {"adapted-limit", "actual-duration"}:
        raise ValueError(f"unknown wall-time policy: {wall_time_policy}")
    prediction_available = dispatch_policy == "WaitTimeOnly" or any(
        job["num_nodes"] <= system["capacity"]
        and execution_performance(
            workload,
            system,
            index,
            job["duration"],
            max_time_limit,
            require_prediction=True,
        )
        is not None
        for index, system in enumerate(systems)
    )
    fallback_used = dispatch_policy != "WaitTimeOnly" and not prediction_available
    effective_policy = "WaitTimeOnly" if fallback_used else dispatch_policy
    candidates = []
    for index, (system, window, horizon) in enumerate(
        zip(systems, windows, horizons)
    ):
        if job["num_nodes"] > system["capacity"]:
            continue
        execution = execution_performance(
            workload,
            system,
            index,
            job["duration"],
            max_time_limit,
            prefer_gpu=effective_policy == "WaitTimeOnly",
            require_prediction=effective_policy != "WaitTimeOnly",
        )
        if execution is None:
            continue
        mode, performance = execution
        predicted_speedup = (
            1.0
            if effective_policy == "WaitTimeOnly"
            else performance["predicted"]
        )
        ground_truth_speedup = performance["ground_truth"]
        predicted_duration = job["duration"] / predicted_speedup
        actual_duration = job["duration"] / ground_truth_speedup
        if (
            effective_policy != "WaitTimeOnly"
            and predicted_duration > max_time_limit
        ) or actual_duration > max_time_limit:
            continue
        predicted_limit = job["limit_time"] / predicted_speedup
        actual_limit = job["limit_time"] / ground_truth_speedup
        if wall_time_policy == "actual-duration":
            submitted_limit, doublings = actual_duration, 0
        else:
            submitted_limit, doublings = adjusted_time_limit(
                predicted_limit, actual_duration, max_time_limit
            )
        submitted_limit = math.ceil(submitted_limit)
        if submitted_limit > max_time_limit:
            continue
        wait = estimate_wait(window, job["num_nodes"], submitted_limit, horizon)
        if math.isfinite(wait) or effective_policy == "RelPerfOnly":
            candidates.append(
                {
                    "index": index,
                    "system_id": system["system_id"],
                    "execution_mode": mode,
                    "ground_truth_relative_performance": ground_truth_speedup,
                    "predicted_relative_performance": predicted_speedup,
                    "estimated_wait": wait,
                    "estimated_duration": predicted_duration,
                    "actual_duration": actual_duration,
                    "predicted_time_limit": predicted_limit,
                    "actual_time_limit": actual_limit,
                    "submitted_time_limit": submitted_limit,
                    "time_limit_doublings": doublings,
                    "predicted_turnaround": wait + predicted_duration,
                    "prediction_fallback": fallback_used,
                    "available_now": window.available_nodes >= job["num_nodes"],
                }
            )
    if effective_policy == "RelPerfOnly":
        available = [candidate for candidate in candidates if candidate["available_now"]]
        pool = available or candidates
        return max(
            pool,
            key=lambda item: (
                item["predicted_relative_performance"],
                -item["index"],
            ),
            default=None,
        )
    if effective_policy == "WaitTimeOnly":
        return min(
            candidates,
            key=lambda item: (item["estimated_wait"], item["index"]),
            default=None,
        )
    return min(
        candidates,
        key=lambda item: (
            item["predicted_turnaround"],
            item["estimated_wait"],
            item["index"],
        ),
        default=None,
    )


def call_all(executor, sessions, make_request):
    """Issue one request concurrently to every simulation server."""
    futures = [
        executor.submit(session.call, make_request(session.messages))
        for session in sessions
    ]
    return [future.result() for future in futures]


def run_experiment(args, grpc, pb, service):
    """Execute the online dispatch loop and return decisions and statistics."""
    systems = read_systems(args.systems)
    if len(systems) != len(args.server):
        raise ValueError("--systems must contain exactly one row per --server")
    jobs = read_arrivals(args.jobs)
    requirements = read_applications(args.applications)
    prediction = (
        args.ground_truth
        if args.dispatch_policy == "WaitTimeOnly"
        else args.prediction
    )
    workloads = read_workloads(args.ground_truth, prediction, systems, requirements)
    generator = random.Random(args.seed)
    largest_system = max(system["capacity"] for system in systems)
    sessions = [ServerSession(address, grpc, pb, service) for address in args.server]
    decisions = []
    dropped_jobs = []
    try:
        with ThreadPoolExecutor(max_workers=len(sessions)) as executor:
            init_futures = []
            for system, session in zip(systems, sessions):
                request = pb.ClientMessage(
                    init=pb.InitRequest(
                        total_nodes=system["capacity"],
                        trace_format="simple",
                        timestamp_format="epoch",
                        backfill_policy=args.backfill_policy,
                        priority_policy=args.priority_policy,
                        run_time_mode="actual",
                        infile=str(args.server_infile),
                        queue_impl=args.queue_impl,
                        session_name=f"{args.session_name}-{system['system_id']}",
                    )
                )
                init_futures.append(executor.submit(session.call, request))
            for future in init_futures:
                future.result()

            for original_job in jobs:
                job = dict(original_job)
                job["num_nodes"] = min(original_job["num_nodes"], largest_system)
                if job["num_nodes"] != original_job["num_nodes"]:
                    print(
                        f"warning: job_id={original_job['job_id']} "
                        f"requested_nodes={original_job['num_nodes']} "
                        f"effective_nodes={job['num_nodes']} "
                        "reason=exceeds_largest_system",
                        file=sys.stderr,
                    )
                call_all(
                    executor,
                    sessions,
                    lambda messages: messages.ClientMessage(
                        advance_to=messages.AdvanceToRequest(
                            target_time=job["submit_time"]
                        )
                    ),
                )
                responses = call_all(
                    executor,
                    sessions,
                    lambda messages: messages.ClientMessage(
                        get_backfill_window=messages.GetBackfillWindowRequest()
                    ),
                )
                windows = [response.get_backfill_window for response in responses]
                horizon_responses = call_all(
                    executor,
                    sessions,
                    lambda messages: messages.ClientMessage(
                        estimate_queue_horizon=messages.EstimateQueueHorizonRequest(
                            utilization=args.prediction_utilization
                        )
                    ),
                )
                horizons = [
                    response.estimate_queue_horizon.horizon
                    for response in horizon_responses
                ]
                workload = sample_workload(workloads, generator)
                choice = choose_system(
                    job,
                    workload,
                    systems,
                    windows,
                    horizons,
                    args.max_time_limit,
                    args.dispatch_policy,
                    args.wall_time_policy,
                )
                if choice is None:
                    dropped_jobs.append(
                        {
                            "job_id": original_job["job_id"],
                            "app": workload["app"],
                            "reason": "no_system_within_max_time_limit",
                            "max_time_limit": args.max_time_limit,
                        }
                    )
                    continue
                append = pb.AppendJobsRequest(
                    requests=[
                        pb.JobAppendData(
                            submit_time=job["submit_time"],
                            num_nodes=job["num_nodes"],
                            queue=job["queue"],
                            limit_time=choice["submitted_time_limit"],
                            actual_run_time=choice["actual_duration"],
                        )
                    ]
                )
                response = sessions[choice["index"]].call(
                    pb.ClientMessage(append_jobs=append)
                )
                sessions[choice["index"]].call(
                    pb.ClientMessage(
                        advance_to=pb.AdvanceToRequest(target_time=job["submit_time"])
                    )
                )
                decisions.append(
                    {
                        "job_id": original_job["job_id"],
                        "submit_time": original_job["submit_time"],
                        "num_nodes": original_job["num_nodes"],
                        "effective_nodes": job["num_nodes"],
                        "duration": original_job["duration"],
                        "time_limit": original_job["limit_time"],
                        "App": workload["app"],
                        "Args": workload["args"],
                        "Ranks": workload["ranks"],
                        "sys_requirement": workload["sys_requirement"],
                        **{
                            key: choice[key]
                            for key in (
                                "system_id",
                                "execution_mode",
                                "ground_truth_relative_performance",
                                "predicted_relative_performance",
                                "prediction_fallback",
                                "estimated_wait",
                                "estimated_duration",
                                "actual_duration",
                                "predicted_time_limit",
                                "actual_time_limit",
                                "submitted_time_limit",
                                "time_limit_doublings",
                                "predicted_turnaround",
                            )
                        },
                        "job_idx": response.append_jobs.job_idx[0],
                    }
                )

            finish_responses = call_all(
                executor,
                sessions,
                lambda messages: messages.ClientMessage(
                    finish_simulation=messages.FinishSimulationRequest()
                ),
            )
        return decisions, [
            response.finish_simulation.statistics for response in finish_responses
        ], [system["system_id"] for system in systems], dropped_jobs
    finally:
        for session in sessions:
            session.close()


def write_results(stream, decisions):
    """Write decisions using the native dispatcher's output schema."""
    fields = (
        "job_id",
        "submit_time",
        "num_nodes",
        "effective_nodes",
        "duration",
        "time_limit",
        "App",
        "Args",
        "Ranks",
        "sys_requirement",
        "system_id",
        "execution_mode",
        "ground_truth_relative_performance",
        "predicted_relative_performance",
        "prediction_fallback",
        "estimated_wait",
        "estimated_duration",
        "actual_duration",
        "predicted_time_limit",
        "actual_time_limit",
        "submitted_time_limit",
        "time_limit_doublings",
        "predicted_turnaround",
        "job_idx",
    )
    writer = csv.DictWriter(stream, fieldnames=fields)
    writer.writeheader()
    for decision in decisions:
        writer.writerow(
            {
                **decision,
                "prediction_fallback": str(decision["prediction_fallback"]).lower(),
            }
        )


def evaluation_metrics(decisions, statistics):
    """Return completion-weighted metrics for the dispatched workload."""
    job_count = len(decisions)
    completed = sum(stats.jobs_completed for stats in statistics)
    if completed != job_count:
        raise ValueError(
            f"completed job count {completed} does not match dispatched "
            f"job count {job_count}"
        )
    if job_count == 0:
        return {
            "average_turnaround_time": 0.0,
            "average_bounded_slowdown": 0.0,
            "average_run_time": 0.0,
            "average_speedup": 0.0,
        }
    return {
        "average_turnaround_time": sum(
            stats.avg_turnaround_time * stats.jobs_completed
            for stats in statistics
        )
        / completed,
        "average_bounded_slowdown": sum(
            stats.avg_bounded_slowdown * stats.jobs_completed
            for stats in statistics
        )
        / completed,
        "average_run_time": sum(
            decision["actual_duration"] for decision in decisions
        )
        / job_count,
        "average_speedup": sum(
            decision["ground_truth_relative_performance"]
            for decision in decisions
        )
        / job_count,
    }


def write_summary(stream, statistics, system_ids, decisions, dropped_jobs=()):
    """Print per-system completion data and overall evaluation metrics."""
    for system_id, stats in zip(system_ids, statistics):
        print(
            f"{system_id}: submitted={stats.jobs_submitted} "
            f"completed={stats.jobs_completed} makespan={stats.makespan:.6g}",
            file=stream,
        )
    for job in dropped_jobs:
        print(
            f"dropped: job_id={job['job_id']} app={job['app']} "
            f"reason={job['reason']} "
            f"max_time_limit={job['max_time_limit']:.8g}",
            file=stream,
        )
    metrics = evaluation_metrics(decisions, statistics)
    print(
        f"overall: jobs={len(decisions)} dropped_jobs={len(dropped_jobs)} "
        f"average_turnaround_time={metrics['average_turnaround_time']:.8g} "
        f"average_bounded_slowdown={metrics['average_bounded_slowdown']:.8g} "
        f"average_run_time={metrics['average_run_time']:.8g} "
        f"average_speedup={metrics['average_speedup']:.8g}",
        file=stream,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", action="append", required=True)
    parser.add_argument("--jobs", required=True, type=pathlib.Path)
    parser.add_argument("--ground-truth", required=True, type=pathlib.Path)
    parser.add_argument("--prediction", required=True, type=pathlib.Path)
    parser.add_argument("--applications", required=True, type=pathlib.Path)
    parser.add_argument("--systems", required=True, type=pathlib.Path)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--server-infile", type=pathlib.Path)
    parser.add_argument(
        "--output", type=pathlib.Path, help="decision CSV (default: stdout)"
    )
    parser.add_argument("--backfill-policy", default="easy")
    parser.add_argument("--priority-policy", default="fcfs")
    parser.add_argument("--queue-impl", default="circular")
    parser.add_argument(
        "--prediction-utilization",
        type=float,
        default=1.0,
        help="usable-capacity factor for queue horizon (0..1)",
    )
    parser.add_argument(
        "--max-time-limit",
        type=float,
        default=math.inf,
        help="maximum submitted wall time (default: unlimited)",
    )
    parser.add_argument(
        "--dispatch-policy",
        choices=("turnaround", "RelPerfOnly", "WaitTimeOnly"),
        default="turnaround",
        help=(
            "system selection policy: minimum predicted turnaround, "
            "relative-performance-only, or waiting-time-only "
            "(default: turnaround)"
        ),
    )
    parser.add_argument(
        "--wall-time-policy",
        choices=("adapted-limit", "actual-duration"),
        default="adapted-limit",
        help=(
            "submit an adapted predicted limit or the ground-truth runtime "
            "(default: adapted-limit)"
        ),
    )
    parser.add_argument("--session-name", default="performance-dispatch")
    args = parser.parse_args()
    if not 0.0 <= args.prediction_utilization <= 1.0:
        parser.error("--prediction-utilization must be in [0, 1]")
    if math.isnan(args.max_time_limit) or args.max_time_limit <= 0:
        parser.error("--max-time-limit must be positive")
    if args.backfill_policy.lower() != "easy":
        parser.error("performance dispatch requires --backfill-policy easy")
    if args.priority_policy.lower() not in {"fcfs", "fcfs_alt"}:
        parser.error("performance dispatch requires an FCFS priority policy")
    args.server_infile = args.server_infile or args.jobs

    repo_root = pathlib.Path(__file__).resolve().parents[2]
    grpc, pb, service, generated_dir = load_stubs(repo_root)
    try:
        decisions, statistics, system_ids, dropped_jobs = run_experiment(
            args, grpc, pb, service
        )
        if args.output:
            with args.output.open("w", newline="") as stream:
                write_results(stream, decisions)
        else:
            write_results(sys.stdout, decisions)
        write_summary(sys.stderr, statistics, system_ids, decisions, dropped_jobs)
    finally:
        generated_dir.cleanup()


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
