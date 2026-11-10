#!/usr/bin/env python3
"""Dispatch applications using platform compatibility, wait, and rank.

This is the simple baseline for the multi-cluster study. Applications declare
whether they are GPU-portable, GPU-only, or CPU-only. Systems declare whether
they are GPU-enabled or CPU-only and provide an ordinal performance rank
within that platform type; rank 1 is fastest.

The controller first removes incompatible or undersized systems. It then keeps
systems whose estimated waits are within ``--wait-tolerance`` seconds of the
shortest wait. GPU-portable applications prefer a GPU-enabled system within
that set, falling back to CPU-only when no GPU system is close enough. The
best performance rank wins within the remaining systems.
"""

import argparse
import csv
import enum
import math
import pathlib
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "python"))

from grpc_multi_server import (DEFAULT_QUEUE_INPUT, QUEUE_FIELD, ServerSession,
                               load_stubs)


class ApplicationType(str, enum.Enum):
    GPU_PORTABLE = "gpu-portable"
    GPU_ONLY = "gpu-only"
    CPU_ONLY = "cpu-only"

    def __str__(self):
        return self.value


APPLICATION_TYPE_CODES = {
    "0": ApplicationType.CPU_ONLY,
    "1": ApplicationType.GPU_ONLY,
    "2": ApplicationType.GPU_PORTABLE,
}


class PlatformType(str, enum.Enum):
    GPU_ENABLED = "gpu-enabled"
    CPU_ONLY = "cpu-only"

    def __str__(self):
        return self.value


REQUIRED_JOB_FIELDS = {
    "job_submit_time", "num_nodes", "time_limit", "application_type"
}


def read_arrivals(path):
    """Read a chronological trace with an application type on every row."""
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames or not REQUIRED_JOB_FIELDS.issubset(reader.fieldnames):
            raise ValueError(
                f"{path} must have columns: {', '.join(sorted(REQUIRED_JOB_FIELDS))}"
            )
        jobs = []
        for index, row in enumerate(reader):
            application_value = row["application_type"].strip()
            try:
                application_type = (
                    APPLICATION_TYPE_CODES[application_value]
                    if application_value in APPLICATION_TYPE_CODES
                    else ApplicationType(application_value)
                )
            except ValueError as error:
                allowed = ", ".join(
                    f"{code} ({application_type.value})"
                    for code, application_type in APPLICATION_TYPE_CODES.items()
                )
                raise ValueError(
                    f"{path}: job {row.get('job_id') or index} has invalid "
                    f"application_type; expected one of: {allowed}"
                ) from error
            job = {
                "job_id": (row.get("job_id") or str(index)).strip(),
                "submit_time": float(row["job_submit_time"]),
                "num_nodes": int(row["num_nodes"]),
                "queue": (row.get(QUEUE_FIELD) or DEFAULT_QUEUE_INPUT).strip(),
                "limit_time": float(row["time_limit"]),
                "application_type": application_type,
            }
            if job["num_nodes"] <= 0 or job["limit_time"] <= 0:
                raise ValueError(f"{path}: job {job['job_id']} has non-positive size")
            jobs.append(job)
    if any(left["submit_time"] > right["submit_time"]
           for left, right in zip(jobs, jobs[1:])):
        raise ValueError(f"{path}: jobs must be sorted by job_submit_time")
    return jobs


def build_systems(args):
    """Combine repeated system arguments in server order."""
    count = len(args.server)
    system_ids = args.system_id or [f"system-{index + 1}" for index in range(count)]
    capacities = args.system_nodes or [args.total_nodes] * count
    if len(system_ids) != count or len(set(system_ids)) != count:
        raise ValueError("--system-id must be unique and repeated once per --server")
    if len(capacities) != count or any(value <= 0 for value in capacities):
        raise ValueError("--system-nodes must be positive and repeated once per --server")
    if not args.system_type or len(args.system_type) != count:
        raise ValueError("--system-type must be repeated once per --server")
    if not args.performance_rank or len(args.performance_rank) != count:
        raise ValueError("--performance-rank must be repeated once per --server")
    if any(rank <= 0 for rank in args.performance_rank):
        raise ValueError("--performance-rank values must be positive")
    return [
        {
            "index": index,
            "system_id": system_id,
            "capacity": capacity,
            "platform_type": platform_type,
            "performance_rank": rank,
        }
        for index, (system_id, capacity, platform_type, rank) in enumerate(
            zip(system_ids, capacities, args.system_type, args.performance_rank)
        )
    ]


def is_compatible(application_type, platform_type):
    if application_type == ApplicationType.GPU_PORTABLE:
        return True
    if application_type == ApplicationType.GPU_ONLY:
        return platform_type == PlatformType.GPU_ENABLED
    return platform_type == PlatformType.CPU_ONLY


def estimate_release_wait(window, required_nodes):
    available = window.available_nodes
    if available >= required_nodes:
        return 0.0
    for release in window.releases:
        available += release.nodes_released
        if available >= required_nodes:
            return max(0.0, release.time - window.current_time)
    return math.inf


def estimate_wait(window, required_nodes, runtime, prediction_horizon):
    has_waiting_head = window.shadow_time > window.current_time
    can_start_now = window.available_nodes >= required_nodes
    can_backfill_now = can_start_now and (
        not has_waiting_head
        or window.current_time + runtime <= window.shadow_time
    )
    if can_backfill_now:
        return 0.0
    if not has_waiting_head:
        return estimate_release_wait(window, required_nodes)
    return window.shadow_time - window.current_time + prediction_horizon


def choose_system(job, systems, windows, horizons, wait_tolerance):
    """Choose by compatibility, wait band, platform preference, then rank."""
    candidates = []
    for system, window, horizon in zip(systems, windows, horizons):
        if (job["num_nodes"] > system["capacity"] or
                not is_compatible(job["application_type"],
                                  system["platform_type"])):
            continue
        wait = estimate_wait(
            window, job["num_nodes"], job["limit_time"], horizon)
        if math.isfinite(wait):
            candidates.append({
                **system,
                "estimated_wait": wait,
                "estimated_runtime": job["limit_time"],
                "predicted_turnaround": wait + job["limit_time"],
            })
    if not candidates:
        raise ValueError(
            f"job {job['job_id']} has no compatible system with sufficient capacity"
        )

    shortest_wait = min(candidate["estimated_wait"] for candidate in candidates)
    near_wait = [
        candidate for candidate in candidates
        if candidate["estimated_wait"] <= shortest_wait + wait_tolerance
    ]
    if job["application_type"] == ApplicationType.GPU_PORTABLE:
        gpu_candidates = [
            candidate for candidate in near_wait
            if candidate["platform_type"] == PlatformType.GPU_ENABLED
        ]
        if gpu_candidates:
            near_wait = gpu_candidates

    return min(near_wait, key=lambda candidate: (
        candidate["performance_rank"], candidate["estimated_wait"],
        candidate["index"]))


def call_all(executor, sessions, make_request):
    futures = [executor.submit(session.call, make_request(session.messages))
               for session in sessions]
    return [future.result() for future in futures]


def run_experiment(args, grpc, pb, service):
    systems = build_systems(args)
    jobs = read_arrivals(args.jobs)
    sessions = [ServerSession(address, grpc, pb, service)
                for address in args.server]
    decisions = []
    try:
        with ThreadPoolExecutor(max_workers=len(sessions)) as executor:
            init_futures = []
            for session, system in zip(sessions, systems):
                request = pb.ClientMessage(init=pb.InitRequest(
                    total_nodes=system["capacity"],
                    trace_format="simple",
                    timestamp_format="epoch",
                    backfill_policy=args.backfill_policy,
                    priority_policy=args.priority_policy,
                    run_time_mode="limit",
                    infile=str(args.server_infile),
                    queue_impl=args.queue_impl,
                    session_name=f"{args.session_name}-{system['system_id']}",
                ))
                init_futures.append(executor.submit(session.call, request))
            for future in init_futures:
                future.result()

            for job in jobs:
                call_all(executor, sessions, lambda messages: messages.ClientMessage(
                    advance_to=messages.AdvanceToRequest(
                        target_time=job["submit_time"])))
                responses = call_all(
                    executor, sessions, lambda messages: messages.ClientMessage(
                        get_backfill_window=messages.GetBackfillWindowRequest()))
                windows = [response.get_backfill_window for response in responses]
                horizon_responses = call_all(
                    executor, sessions, lambda messages: messages.ClientMessage(
                        estimate_queue_horizon=messages.EstimateQueueHorizonRequest(
                            utilization=args.prediction_utilization)))
                horizons = [response.estimate_queue_horizon.horizon
                            for response in horizon_responses]
                choice = choose_system(
                    job, systems, windows, horizons, args.wait_tolerance)
                append = pb.AppendJobsRequest(requests=[pb.JobAppendData(
                    submit_time=job["submit_time"], num_nodes=job["num_nodes"],
                    queue=job["queue"], limit_time=job["limit_time"])])
                response = sessions[choice["index"]].call(
                    pb.ClientMessage(append_jobs=append))
                sessions[choice["index"]].call(pb.ClientMessage(
                    advance_to=pb.AdvanceToRequest(
                        target_time=job["submit_time"])))
                decisions.append({
                    "job_id": job["job_id"],
                    "submit_time": job["submit_time"],
                    "application_type": job["application_type"].value,
                    "system_id": choice["system_id"],
                    "platform_type": choice["platform_type"].value,
                    "performance_rank": choice["performance_rank"],
                    "estimated_wait": choice["estimated_wait"],
                    "estimated_runtime": choice["estimated_runtime"],
                    "predicted_turnaround": choice["predicted_turnaround"],
                    "job_idx": response.append_jobs.job_idx[0],
                })

            finish_responses = call_all(
                executor, sessions, lambda messages: messages.ClientMessage(
                    finish_simulation=messages.FinishSimulationRequest()))
        return decisions, [response.finish_simulation.statistics
                           for response in finish_responses], systems
    finally:
        for session in sessions:
            session.close()


def write_results(stream, decisions):
    fields = (
        "job_id", "submit_time", "application_type", "system_id",
        "platform_type", "performance_rank", "estimated_wait",
        "estimated_runtime", "predicted_turnaround", "job_idx",
    )
    writer = csv.DictWriter(stream, fieldnames=fields)
    writer.writeheader()
    writer.writerows(decisions)


def write_summary(stream, statistics, systems):
    for system, stats in zip(systems, statistics):
        print(f"{system['system_id']}: submitted={stats.jobs_submitted} "
              f"completed={stats.jobs_completed} makespan={stats.makespan:.6g}",
              file=stream)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", action="append", required=True)
    parser.add_argument("--system-id", action="append",
                        help="system name; repeat in --server order")
    parser.add_argument("--system-type", action="append", type=PlatformType,
                        choices=list(PlatformType),
                        help="gpu-enabled or cpu-only; repeat per server")
    parser.add_argument("--performance-rank", action="append", type=int,
                        help="ordinal rank within platform type; 1 is fastest")
    parser.add_argument("--system-nodes", action="append", type=int,
                        help="node capacity; repeat in --server order")
    parser.add_argument("--jobs", required=True, type=pathlib.Path)
    parser.add_argument("--server-infile", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path,
                        help="decision CSV (default: stdout)")
    parser.add_argument("--total-nodes", type=int, default=100)
    parser.add_argument("--wait-tolerance", type=float, default=0.0,
                        help="maximum seconds above the shortest estimated wait "
                             "that counts as similar (default: 0)")
    parser.add_argument("--backfill-policy", default="easy")
    parser.add_argument("--priority-policy", default="fcfs")
    parser.add_argument("--queue-impl", default="circular")
    parser.add_argument("--prediction-utilization", type=float, default=1.0,
                        help="usable-capacity factor for queue horizon (0..1)")
    parser.add_argument("--session-name", default="baseline-dispatch")
    args = parser.parse_args()
    if not math.isfinite(args.wait_tolerance) or args.wait_tolerance < 0.0:
        parser.error("--wait-tolerance must be finite and nonnegative")
    if not 0.0 <= args.prediction_utilization <= 1.0:
        parser.error("--prediction-utilization must be in [0, 1]")
    if args.backfill_policy.lower() != "easy":
        parser.error("baseline dispatch requires --backfill-policy easy")
    if args.priority_policy.lower() not in {"fcfs", "fcfs_alt"}:
        parser.error("baseline dispatch requires an FCFS priority policy")
    args.server_infile = args.server_infile or args.jobs

    repo_root = pathlib.Path(__file__).resolve().parents[2]
    grpc, pb, service, generated_dir = load_stubs(repo_root)
    try:
        decisions, statistics, systems = run_experiment(args, grpc, pb, service)
        if args.output:
            with args.output.open("w", newline="") as stream:
                write_results(stream, decisions)
        else:
            write_results(sys.stdout, decisions)
        write_summary(sys.stderr, statistics, systems)
    finally:
        generated_dir.cleanup()


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
