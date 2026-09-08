#!/usr/bin/env python3
"""Single-client/two-server composite-stream integration test.

The coordinator reads both ordinary traces and the composite stream.  It
exercises the intended ordering boundaries without MPI:

* initial ``tn = ta = tc``;
* a post-composite ordinary arrival ``t0 = tc`` followed by a second
  ``AdvanceTo(tc)``;
* ``tn = ta < tc`` for the next composite event; and
* a later ordinary batch with ``tc < t0``.
"""

import argparse
import pathlib
import socket
import subprocess
import sys
import tempfile
import time

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "python"))

from grpc_multi_client import ServerSession, load_stubs, read_jobs
from grpc_sync_coordinator import read_composites


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def append_and_submit(session, pb, jobs):
    if not jobs:
        return
    response = session.call(pb.ClientMessage(append_jobs=pb.AppendJobsRequest(
        requests=[pb.JobAppendData(
            submit_time=job["submit_time"], num_nodes=job["num_nodes"],
            queue=job["queue"], limit_time=job["limit_time"])
            for job in jobs])))
    if len(response.append_jobs.job_idx) != len(jobs):
        raise RuntimeError("AppendJobs returned an unexpected number of job indexes")
    for index, job in zip(response.append_jobs.job_idx, jobs):
        session.call(pb.ClientMessage(submit_job=pb.SubmitJobRequest(
            job_idx=index, submit_time=job["submit_time"])))


def advance(session, pb, timestamp):
    session.call(pb.ClientMessage(advance_to=pb.AdvanceToRequest(
        target_time=timestamp)))


def fragment_for(event, system_id):
    for fragment in event["fragments"]:
        if fragment["system_id"] == system_id:
            return fragment
    raise RuntimeError(f"composite event is missing a fragment for {system_id}")


def initialize(session, pb, system):
    session.call(pb.ClientMessage(init=pb.InitRequest(
        total_nodes=100,
        trace_format="simple",
        timestamp_format="epoch",
        backfill_policy="easy",
        priority_policy="fcfs",
        run_time_mode="limit",
        infile=str(system["trace"]),
        queue_impl="circular",
        session_name=system["system_id"],
    )))


def start_servers(binary, ports, workdir):
    processes = []
    for port in ports:
        processes.append(subprocess.Popen(
            [str(binary), f"127.0.0.1:{port}"], cwd=workdir,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    return processes


def stop_servers(processes):
    for process in processes:
        process.terminate()
    for process in processes:
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def wait_for_servers(grpc, addresses):
    for address in addresses:
        channel = grpc.insecure_channel(address)
        try:
            grpc.channel_ready_future(channel).result(timeout=15)
        finally:
            channel.close()


def run_test(server_binary):
    fixture_dir = REPO_ROOT / "tests" / "test_traces" / "grpc"
    systems = [
        {"system_id": "server1", "trace": fixture_dir / "coordinator_server1.csv"},
        {"system_id": "server2", "trace": fixture_dir / "coordinator_server2.csv"},
    ]
    events = read_composites(fixture_dir / "coordinator_composite_jobs.csv", systems)
    if [event["submit_time"] for event in events] != [10.0, 25.0]:
        raise RuntimeError("fixture must contain composite events at t=10 and t=25")

    ordinary = {system["system_id"]: read_jobs(system["trace"]) for system in systems}
    ports = [free_port(), free_port()]
    addresses = [f"127.0.0.1:{port}" for port in ports]
    grpc, pb, service, generated_dir = load_stubs(REPO_ROOT)
    with tempfile.TemporaryDirectory(prefix="dr-evt-single-coordinator-") as workdir:
        processes = start_servers(server_binary, ports, workdir)
        sessions = []
        try:
            wait_for_servers(grpc, addresses)
            sessions = [ServerSession(address, grpc, pb, service) for address in addresses]
            for session, system in zip(sessions, systems):
                initialize(session, pb, system)

            # Initial batch: tn = ta = tc = 10.  Keep the second t=10 row
            # for the next batch, where it is intentionally post-composite.
            cursors = {}
            for system, session in zip(systems, sessions):
                jobs = ordinary[system["system_id"]]
                initial = jobs[:2]
                if [job["submit_time"] for job in initial] != [0.0, 10.0]:
                    raise RuntimeError("fixture initial batch must end at t=10")
                append_and_submit(session, pb, initial)
                cursors[system["system_id"]] = 2
                advance(session, pb, 10.0)

            for system, session in zip(systems, sessions):
                append_and_submit(session, pb, [fragment_for(events[0], system["system_id"])])
                advance(session, pb, 10.0)

            # t0 = tc: append only after the composite was evaluated, then
            # re-advance at the same time so this newly queued job is seen.
            for system, session in zip(systems, sessions):
                jobs = ordinary[system["system_id"]]
                equal_time = jobs[cursors[system["system_id"]]:cursors[system["system_id"]] + 1]
                if len(equal_time) != 1 or equal_time[0]["submit_time"] != 10.0:
                    raise RuntimeError("fixture must provide post-composite t0 = tc")
                append_and_submit(session, pb, equal_time)
                cursors[system["system_id"]] += 1
                advance(session, pb, 10.0)

            # tn = ta = 20 < tc = 25.
            for system, session in zip(systems, sessions):
                jobs = ordinary[system["system_id"]]
                pre_second = jobs[cursors[system["system_id"]]:cursors[system["system_id"]] + 1]
                if len(pre_second) != 1 or pre_second[0]["submit_time"] != 20.0:
                    raise RuntimeError("fixture must provide tn = ta = 20")
                append_and_submit(session, pb, pre_second)
                cursors[system["system_id"]] += 1
                advance(session, pb, 20.0)

            for system, session in zip(systems, sessions):
                append_and_submit(session, pb, [fragment_for(events[1], system["system_id"])])
                advance(session, pb, 25.0)

            # tc < t0: append and evaluate the final batch after tc = 25.
            for system, session in zip(systems, sessions):
                final = ordinary[system["system_id"]][cursors[system["system_id"]]:]
                if len(final) != 1 or final[0]["submit_time"] != 30.0:
                    raise RuntimeError("fixture must provide tc < t0 = 30")
                append_and_submit(session, pb, final)
                advance(session, pb, 30.0)
                finish = session.call(pb.ClientMessage(
                    finish_simulation=pb.FinishSimulationRequest())).finish_simulation
                if finish.statistics.jobs_submitted != 7 or finish.statistics.jobs_completed != 7:
                    raise RuntimeError("server did not complete every ordinary and composite job")
        finally:
            for session in sessions:
                session.close()
            stop_servers(processes)
            generated_dir.cleanup()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server_binary", type=pathlib.Path)
    args = parser.parse_args()
    run_test(args.server_binary.resolve())


if __name__ == "__main__":
    main()
