#!/usr/bin/env python3
"""Drive one independent DR_EVT gRPC simulation session per server.

The jobs CSV is read only by this client.  Its rows are partitioned among
the supplied servers (round-robin by default); each server receives a
separate Simulation and therefore has no shared scheduler state with the
others.  This is useful for modeling independent sites or queues.

Install the client dependencies first:

    python3 -m pip install grpcio grpcio-tools protobuf

The script generates Python stubs from src/proto/dr_evt_service.proto into a
temporary directory at runtime, so generated files are never checked in.
Set DR_EVT_LEGACY_QUEUE_INPUT=1 when connecting to a server built with the
legacy queue-name input option; otherwise numeric q_id values are used.
"""

import argparse
import csv
import importlib
import itertools
import os
import pathlib
import queue
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed

LEGACY_QUEUE_INPUT = os.environ.get("DR_EVT_LEGACY_QUEUE_INPUT", "").lower() \
    in {"1", "on", "true", "yes"}
QUEUE_FIELD = "queue" if LEGACY_QUEUE_INPUT else "q_id"
DEFAULT_QUEUE_INPUT = "pbatch" if LEGACY_QUEUE_INPUT else "1"


def load_stubs(repo_root):
    """Generate and import Python gRPC stubs in an isolated temporary path."""
    try:
        import grpc
        import grpc_tools.protoc
    except ImportError as error:
        raise RuntimeError(
            "Python gRPC dependencies are missing. Install with: "
            "python3 -m pip install grpcio grpcio-tools protobuf"
        ) from error

    proto_dir = repo_root / "src" / "proto"
    proto = proto_dir / "dr_evt_service.proto"
    generated_dir = tempfile.TemporaryDirectory(prefix="dr_evt_grpc_")
    result = grpc_tools.protoc.main([
        "grpc_tools.protoc",
        f"--proto_path={proto_dir}",
        f"--python_out={generated_dir.name}",
        f"--grpc_python_out={generated_dir.name}",
        str(proto),
    ])
    if result:
        generated_dir.cleanup()
        raise RuntimeError(f"protoc failed while generating stubs from {proto}")

    sys.path.insert(0, generated_dir.name)
    return grpc, importlib.import_module("dr_evt_service_pb2"), \
        importlib.import_module("dr_evt_service_pb2_grpc"), generated_dir


class ServerSession:
    """A synchronous request/response wrapper over one Session stream."""

    def __init__(self, address, grpc, messages, service):
        self.address = address
        self.messages = messages
        self._outgoing = queue.Queue()
        self._request_id = 1
        self._channel = grpc.insecure_channel(address)
        self._responses = service.SimulationServiceStub(self._channel).Session(
            self._request_iterator())

    def _request_iterator(self):
        while True:
            request = self._outgoing.get()
            if request is None:
                return
            yield request

    def call(self, request):
        request.request_id = self._request_id
        self._request_id += 1
        self._outgoing.put(request)
        try:
            response = next(self._responses)
        except StopIteration as error:
            raise RuntimeError(f"{self.address}: server closed the stream") from error
        if response.request_id != request.request_id:
            raise RuntimeError(
                f"{self.address}: response id {response.request_id} does not match "
                f"request id {request.request_id}"
            )
        if response.HasField("error"):
            raise RuntimeError(f"{self.address}: {response.error.message}")
        return response

    def close(self):
        self._outgoing.put(None)
        self._channel.close()


def read_jobs(path):
    required = {"job_submit_time", "num_nodes", "time_limit"}
    with path.open(newline="") as jobs_file:
        reader = csv.DictReader(jobs_file)
        if not reader.fieldnames or not required.issubset(reader.fieldnames):
            raise ValueError(
                f"{path} must have columns: {', '.join(sorted(required))}"
            )
        jobs = [
            {
                "submit_time": float(row["job_submit_time"]),
                "num_nodes": int(row["num_nodes"]),
                "queue": (row.get(QUEUE_FIELD) or DEFAULT_QUEUE_INPUT).strip(),
                "limit_time": float(row["time_limit"]),
            }
            for row in reader
        ]
    return jobs


def partition_jobs(jobs, server_count, distribution):
    if distribution == "round-robin":
        partitions = [[] for _ in range(server_count)]
        for index, job in enumerate(jobs):
            partitions[index % server_count].append(job)
        return partitions
    chunk_size = (len(jobs) + server_count - 1) // server_count
    return [jobs[index * chunk_size:(index + 1) * chunk_size]
            for index in range(server_count)]


def drive_server(address, jobs, args, grpc, pb, service, server_index):
    session = ServerSession(address, grpc, pb, service)
    try:
        session.call(pb.ClientMessage(init=pb.InitRequest(
            total_nodes=args.total_nodes,
            trace_format="simple",
            timestamp_format="epoch",
            backfill_policy=args.backfill_policy,
            priority_policy=args.priority_policy,
            run_time_mode="limit",
            infile=str(args.server_infile),
            queue_impl=args.queue_impl,
            session_name=f"{args.session_name}-{server_index}",
        )))

        if jobs:
            # AppendJobsRequest expects non-decreasing submit times.  A CSV used
            # for streaming must therefore be chronologically ordered.
            if any(left["submit_time"] > right["submit_time"]
                   for left, right in itertools.pairwise(jobs)):
                raise ValueError(f"{address}: assigned jobs are not sorted by submit time")
            append = pb.AppendJobsRequest(requests=[pb.JobAppendData(**job) for job in jobs])
            response = session.call(pb.ClientMessage(append_jobs=append))

        session.call(pb.ClientMessage(advance_to=pb.AdvanceToRequest(
            target_time=args.advance_to)))
        finish = session.call(pb.ClientMessage(
            finish_simulation=pb.FinishSimulationRequest())).finish_simulation
        stats = finish.statistics
        return {
            "server": address,
            "session_id": finish.session_id,
            "assigned_jobs": len(jobs),
            "jobs_submitted": stats.jobs_submitted,
            "jobs_completed": stats.jobs_completed,
            "makespan": stats.makespan,
            "utilization": stats.utilization,
        }
    finally:
        session.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", action="append", required=True,
                        help="DR_EVT server address; repeat once per server")
    parser.add_argument("--jobs", type=pathlib.Path, required=True,
                        help="client-side simple-format job CSV")
    parser.add_argument("--server-infile", type=pathlib.Path,
                        help="same-format file visible to every server; defaults to --jobs")
    parser.add_argument("--distribution", choices=("round-robin", "contiguous"),
                        default="round-robin")
    parser.add_argument("--total-nodes", type=int, default=100)
    parser.add_argument("--backfill-policy", default="easy")
    parser.add_argument("--priority-policy", default="fcfs")
    parser.add_argument("--queue-impl", default="circular")
    parser.add_argument("--advance-to", type=float, default=1e9)
    parser.add_argument("--session-name", default="multi-server",
                        help="prefix for server-generated session report names")
    args = parser.parse_args()
    args.server_infile = args.server_infile or args.jobs

    repo_root = pathlib.Path(__file__).resolve().parents[1]
    grpc, pb, service, generated_dir = load_stubs(repo_root)
    try:
        jobs = read_jobs(args.jobs)
        partitions = partition_jobs(jobs, len(args.server), args.distribution)
        results = []
        with ThreadPoolExecutor(max_workers=len(args.server)) as executor:
            futures = {
                executor.submit(drive_server, address, partition, args,
                                grpc, pb, service, index): index
                for index, (address, partition) in enumerate(
                    zip(args.server, partitions))
            }
            for future in as_completed(futures):
                results.append((futures[future], future.result()))
        results = [result for _, result in sorted(results)]

        print("server,assigned_jobs,jobs_submitted,jobs_completed,makespan,utilization")
        for result in results:
            print("{server},{assigned_jobs},{jobs_submitted},{jobs_completed},"
                  "{makespan:.6g},{utilization:.6g}".format(**result))
        print(f"total assigned jobs: {sum(item['assigned_jobs'] for item in results)}")
    finally:
        generated_dir.cleanup()


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
