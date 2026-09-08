#!/usr/bin/env python3
"""Launch one DR_EVT gRPC client and one server on every other MPI rank.

Run this program under ``mpirun``.  Rank 0 runs a Python client script;
every non-root rank starts one ``dr_evt_server`` on its local host.  Each
server's hostname and port are collected over MPI and supplied to the client
as repeated ``--server`` arguments.

For example (four MPI ranks means three servers)::

    mpirun -np 4 python3 python/grpc_mpi_launcher.py \
        --server-binary ./build/dr_evt_server --base-port 50051 -- \
        --jobs /shared/jobs.csv --total-nodes 1000

The default controller is ``grpc_multi_server.py``.  It receives its normal
arguments after ``--``; do not provide ``--server`` yourself, since this
launcher supplies the endpoints discovered from the server ranks.
"""

import argparse
import pathlib
import socket
import subprocess
import sys
import time


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server-binary", required=True, type=pathlib.Path,
                        help="path to the dr_evt_server executable")
    parser.add_argument("--base-port", type=int, default=50051,
                        help="server rank r listens on this port + r - 1")
    parser.add_argument("--client-script", type=pathlib.Path,
                        help="Python controller script for rank 0 "
                             "(default: grpc_multi_server.py beside this file)")
    parser.add_argument("client_args", nargs=argparse.REMAINDER,
                        help="arguments for the client script; precede with --")
    args = parser.parse_args()
    if args.client_args[:1] == ["--"]:
        args.client_args = args.client_args[1:]
    if not args.client_args:
        parser.error("provide arguments for the client script after --")
    if not 1 <= args.base_port <= 65535:
        parser.error("--base-port must be in 1..65535")
    return args


def stop_server(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def wait_for_servers(addresses, timeout=15):
    """Wait until every advertised gRPC TCP listener accepts connections."""
    pending = set(addresses)
    deadline = time.monotonic() + timeout
    while pending and time.monotonic() < deadline:
        for address in tuple(pending):
            host, port = address.rsplit(":", 1)
            try:
                with socket.create_connection((host, int(port)), timeout=0.25):
                    pending.remove(address)
            except OSError:
                pass
        if pending:
            time.sleep(0.1)
    if pending:
        raise RuntimeError("servers did not become reachable: " +
                           ", ".join(sorted(pending)))


def main():
    args = parse_args()
    try:
        from mpi4py import MPI
    except ImportError as error:
        raise RuntimeError(
            "mpi4py is required; install it in the Python environment used by mpirun"
        ) from error

    comm = MPI.COMM_WORLD
    rank = comm.Get_rank()
    size = comm.Get_size()
    if size < 2:
        if rank == 0:
            print("error: launch at least two MPI ranks (one client and one server)",
                  file=sys.stderr)
        return 2

    server = None
    report = {"rank": rank, "address": None, "error": None}
    if rank:
        port = args.base_port + rank - 1
        if port > 65535:
            report["error"] = f"derived port {port} is outside 1..65535"
        elif not args.server_binary.is_file():
            report["error"] = f"server binary does not exist: {args.server_binary}"
        else:
            try:
                # The hostname is intentionally sent instead of loopback: MPI may
                # place rank 0 and this server rank on different hosts.
                host = socket.getfqdn() or socket.gethostname()
                server = subprocess.Popen(
                    [str(args.server_binary), f"0.0.0.0:{port}"])
                report["address"] = f"{host}:{port}"
            except OSError as error:
                report["error"] = str(error)

    reports = comm.gather(report, root=0)
    status = 0
    if rank == 0:
        failures = [item for item in reports if item["error"]]
        if failures:
            for failure in failures:
                print(f"server rank {failure['rank']}: {failure['error']}",
                      file=sys.stderr)
            status = 1
        else:
            script = args.client_script or pathlib.Path(__file__).with_name(
                "grpc_multi_server.py")
            command = [sys.executable, str(script), *args.client_args]
            addresses = []
            for item in reports[1:]:
                addresses.append(item["address"])
                command.extend(("--server", item["address"]))
            try:
                wait_for_servers(addresses)
                status = subprocess.run(command, check=False).returncode
            except RuntimeError as error:
                print(f"error: {error}", file=sys.stderr)
                status = 1

    # A collective shutdown keeps server ranks alive while rank 0 drives the
    # gRPC sessions, and ensures every spawned server is reaped on success or
    # client failure alike.
    status = comm.bcast(status, root=0)
    if rank:
        stop_server(server)
    return status


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
