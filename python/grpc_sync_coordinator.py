#!/usr/bin/env python3
"""Minimal synchronized multi-system DR_EVT gRPC experiment coordinator.

This deliberately does not implement distributed reservation or rollback.
It coordinates independent DR_EVT servers at composite-job event times and
records whether each composite fragment starts immediately or remains queued.
That makes partial starts observable for experiments with future approaches.
"""

import argparse
import csv
import json
import pathlib
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed

from grpc_multi_server import ServerSession, load_stubs, read_jobs


SYSTEM_FIELDS = {"system_id", "address", "trace"}
COMPOSITE_FIELDS = {
    "composite_id", "submit_time", "system_id", "num_nodes", "queue",
    "time_limit",
}


def read_systems(path):
    """Read one endpoint and ordinary trace per independent system."""
    with path.open(newline="") as systems_file:
        reader = csv.DictReader(systems_file)
        if not reader.fieldnames or not SYSTEM_FIELDS.issubset(reader.fieldnames):
            raise ValueError(
                f"{path} must have columns: {', '.join(sorted(SYSTEM_FIELDS))}"
            )
        systems = []
        seen_ids = set()
        for row in reader:
            system_id = row["system_id"].strip()
            if not system_id or system_id in seen_ids:
                raise ValueError(f"{path}: system_id values must be non-empty and unique")
            seen_ids.add(system_id)
            trace = pathlib.Path(row["trace"])
            systems.append({
                "system_id": system_id,
                "address": row["address"].strip(),
                "trace": trace,
                # The server needs a readable same-format path to validate the
                # header during Init. It can differ from the client's trace
                # path when the systems do not share a filesystem.
                "server_infile": row.get("server_infile", "").strip() or str(trace),
                "total_nodes": int(row.get("total_nodes", "") or 100),
            })
    return systems


def read_composites(path, systems):
    """Read long-form composite fragments grouped by composite_id."""
    known_systems = {system["system_id"] for system in systems}
    groups = {}
    with path.open(newline="") as composite_file:
        reader = csv.DictReader(composite_file)
        if not reader.fieldnames or not COMPOSITE_FIELDS.issubset(reader.fieldnames):
            raise ValueError(
                f"{path} must have columns: {', '.join(sorted(COMPOSITE_FIELDS))}"
            )
        for row in reader:
            composite_id = row["composite_id"].strip()
            system_id = row["system_id"].strip()
            submit_time = float(row["submit_time"])
            if not composite_id or system_id not in known_systems:
                raise ValueError(f"{path}: unknown system or empty composite_id")
            group = groups.setdefault(composite_id, {
                "composite_id": composite_id,
                "submit_time": submit_time,
                "fragments": [],
            })
            if group["submit_time"] != submit_time:
                raise ValueError(f"{path}: {composite_id} has inconsistent submit times")
            if any(fragment["system_id"] == system_id for fragment in group["fragments"]):
                raise ValueError(f"{path}: {composite_id} has duplicate system {system_id}")
            group["fragments"].append({
                "system_id": system_id,
                "submit_time": submit_time,
                "num_nodes": int(row["num_nodes"]),
                "queue": row["queue"],
                "limit_time": float(row["time_limit"]),
            })

    events = sorted(groups.values(), key=lambda group: group["submit_time"])
    for event in events:
        if len(event["fragments"]) < 2:
            raise ValueError(
                f"{path}: composite {event['composite_id']} needs at least two systems"
            )
    return events


def call_parallel(calls):
    """Make one blocking request per independent server concurrently."""
    with ThreadPoolExecutor(max_workers=len(calls)) as executor:
        futures = {executor.submit(call): key for key, call in calls.items()}
        results = {}
        for future in as_completed(futures):
            results[futures[future]] = future.result()
    return results


def initialize_system(system, session, pb):
    session.call(pb.ClientMessage(init=pb.InitRequest(
        total_nodes=system["total_nodes"],
        trace_format="simple",
        timestamp_format="epoch",
        backfill_policy="easy",
        priority_policy="fcfs",
        run_time_mode="limit",
        infile=system["server_infile"],
        queue_impl="circular",
        session_name=system["system_id"],
    )))

    # Ordinary jobs are deliberately submitted before the synchronization
    # watermark reaches all their submit times. The server retains them as
    # future arrivals while advance_to() moves the simulation forward.
    jobs = read_jobs(system["trace"])
    if any(left["submit_time"] > right["submit_time"]
           for left, right in zip(jobs, jobs[1:])):
        raise ValueError(f"{system['trace']}: ordinary jobs must be time-sorted")
    if not jobs:
        return 0
    response = session.call(pb.ClientMessage(append_jobs=pb.AppendJobsRequest(
        requests=[pb.JobAppendData(**job) for job in jobs])))
    return len(jobs)


def query_stats(session, pb):
    return session.call(pb.ClientMessage(
        get_statistics=pb.GetStatisticsRequest())).get_statistics


def submit_fragment(session, fragment, pb):
    response = session.call(pb.ClientMessage(append_job=pb.AppendJobRequest(
        submit_time=fragment["submit_time"],
        num_nodes=fragment["num_nodes"],
        queue=fragment["queue"],
        limit_time=fragment["limit_time"],
    )))


def run_experiment(systems, events, grpc, pb, service, output):
    sessions = {
        system["system_id"]: ServerSession(system["address"], grpc, pb, service)
        for system in systems
    }
    try:
        ordinary_counts = call_parallel({
            system["system_id"]: lambda system=system: initialize_system(
                system, sessions[system["system_id"]], pb)
            for system in systems
        })
        print("ordinary jobs pre-submitted: " + ", ".join(
            f"{system_id}={count}" for system_id, count in ordinary_counts.items()),
            file=sys.stderr)

        for event in events:
            event_time = event["submit_time"]
            # A barrier at the same logical time makes each server process
            # previously submitted ordinary arrivals before the composite is
            # introduced. The watermark need not be the last ordinary job's
            # submit time because future ordinary arrivals were pre-submitted.
            call_parallel({
                system_id: lambda system_id=system_id: sessions[system_id].call(
                    pb.ClientMessage(advance_to=pb.AdvanceToRequest(
                        target_time=event_time)))
                for system_id in sessions
            })
            before = call_parallel({
                fragment["system_id"]: lambda fragment=fragment: query_stats(
                    sessions[fragment["system_id"]], pb)
                for fragment in event["fragments"]
            })
            call_parallel({
                fragment["system_id"]: lambda fragment=fragment: submit_fragment(
                    sessions[fragment["system_id"]], fragment, pb)
                for fragment in event["fragments"]
            })
            # submit_job() only queues a streaming arrival. Advance to the
            # same watermark once more so each server evaluates the newly
            # submitted composite fragment without moving beyond the barrier.
            call_parallel({
                fragment["system_id"]: lambda fragment=fragment: sessions[
                    fragment["system_id"]].call(pb.ClientMessage(
                        advance_to=pb.AdvanceToRequest(target_time=event_time)))
                for fragment in event["fragments"]
            })
            after = call_parallel({
                fragment["system_id"]: lambda fragment=fragment: query_stats(
                    sessions[fragment["system_id"]], pb)
                for fragment in event["fragments"]
            })

            fragments = []
            for fragment in event["fragments"]:
                system_id = fragment["system_id"]
                node_delta = after[system_id].nodes_in_use - before[system_id].nodes_in_use
                # No other client request is issued between the two snapshots.
                # With one fragment per system in this event, this exposes the
                # current independent-scheduler behavior without claiming a
                # reservation guarantee.
                fragments.append({
                    "system_id": system_id,
                    "requested_nodes": fragment["num_nodes"],
                    "nodes_in_use_before": before[system_id].nodes_in_use,
                    "nodes_in_use_after": after[system_id].nodes_in_use,
                    "started_immediately": node_delta >= fragment["num_nodes"],
                })
            record = {
                "composite_id": event["composite_id"],
                "submit_time": event_time,
                "partial_start": any(item["started_immediately"] for item in fragments)
                and not all(item["started_immediately"] for item in fragments),
                "fragments": fragments,
            }
            output.write(json.dumps(record) + "\n")
            output.flush()
            state = "PARTIAL_START" if record["partial_start"] else "uniform"
            print(f"{event['composite_id']} at t={event_time:g}: {state}",
                  file=sys.stderr)
    finally:
        for session in sessions.values():
            session.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--systems", required=True, type=pathlib.Path,
                        help="CSV: system_id,address,trace[,server_infile,total_nodes]")
    parser.add_argument("--composites", required=True, type=pathlib.Path,
                        help="long-form CSV: composite_id,submit_time,system_id,num_nodes,queue,time_limit")
    parser.add_argument("--output", type=argparse.FileType("w"), default=sys.stdout,
                        help="JSON Lines experiment output (default: stdout)")
    args = parser.parse_args()

    systems = read_systems(args.systems)
    events = read_composites(args.composites, systems)
    repo_root = pathlib.Path(__file__).resolve().parents[1]
    grpc, pb, service, generated_dir = load_stubs(repo_root)
    try:
        run_experiment(systems, events, grpc, pb, service, args.output)
    finally:
        generated_dir.cleanup()
        if args.output is not sys.stdout:
            args.output.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
