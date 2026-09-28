#!/usr/bin/env python3
"""Create one monthly simulation trace with historical crossing jobs seeded.

The output is an ordinary PCON/simple trace.  Its initialization rows are
separate jobs submitted at the first instant of the selected calendar month;
each has the historical job's remaining run time.  Ordinary arrivals are taken
only from that month's no-times workload trace.
"""

import argparse
import csv
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError


def month_from_name(path: Path) -> tuple[int, int]:
    try:
        year, month, suffix = path.name.split("_", 2)
        if suffix != "scheduling_trace.csv":
            raise ValueError
        return 2000 + int(year), int(month)
    except ValueError as error:
        raise ValueError(f"invalid monthly trace name: {path.name}") from error


def required(row: dict[str, str], *names: str) -> str:
    for name in names:
        if row.get(name, "") != "":
            return row[name]
    raise ValueError("missing required column (one of: {})".format(", ".join(names)))


def parse_time(value: str, timezone: str) -> float:
    try:
        return float(value)
    except ValueError:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=ZoneInfo(timezone))
        return parsed.timestamp()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("label", help="monthly label in YYYY-MM form")
    parser.add_argument("output_trace", type=Path)
    parser.add_argument("--historical-dir", type=Path, required=True)
    parser.add_argument("--workload-dir", type=Path, required=True)
    parser.add_argument("--timezone", default="Asia/Tokyo")
    parser.add_argument("--total-nodes", type=int, required=True)
    parser.add_argument("--start-time",
                        help="override the month-start warm boundary (epoch or ISO time)")
    parser.add_argument("--end-time",
                        help="exclude ordinary arrivals at or after this time (epoch or ISO time)")
    parser.add_argument("--capacity-history", type=Path,
                        help="write historical boundary context for capacity detection")
    args = parser.parse_args()

    try:
        year, month = (int(part) for part in args.label.split("-", 1))
        # Existing Fugaku workflows use the POSIX abbreviation JST, while
        # zoneinfo databases consistently expose the corresponding IANA name.
        timezone = "Asia/Tokyo" if args.timezone == "JST" else args.timezone
        default_boundary = datetime(year, month, 1, tzinfo=ZoneInfo(timezone)).timestamp()
        boundary = parse_time(args.start_time, timezone) if args.start_time else default_boundary
        end_time = parse_time(args.end_time, timezone) if args.end_time else None
    except (ValueError, ZoneInfoNotFoundError) as error:
        raise SystemExit(f"invalid label or timezone: {error}")
    if not 1 <= month <= 12 or args.total_nodes <= 0:
        raise SystemExit("month must be 1..12 and --total-nodes must be positive")
    if end_time is not None and end_time <= boundary:
        raise SystemExit("--end-time must be later than --start-time")

    slug = f"{year % 100:02d}_{month:02d}_scheduling_trace.csv"
    workload_path = args.workload_dir / slug
    if not workload_path.is_file():
        raise SystemExit(f"monthly workload trace is unavailable: {workload_path}")

    output_fields = [
        "job_submit_time", "time_limit", "num_nodes", "duration", "avgpcon",
        "minpcon", "maxpcon", "exit_status", "initialization",
        "source_trace", "source_job_index",
    ]
    seeds: list[dict[str, str]] = []
    capacity_seeds: list[dict[str, str]] = []
    for history_path in sorted(args.historical_dir.glob("*_scheduling_trace.csv")):
        if month_from_name(history_path) > (year, month):
            continue
        with history_path.open(newline="") as stream:
            for index, row in enumerate(csv.DictReader(stream)):
                begin = float(required(row, "begin_time", "start_time"))
                end = float(required(row, "end_time"))
                if not begin <= boundary < end:
                    continue
                nodes = int(required(row, "num_nodes", "nodes", "nodes_requested"))
                seeds.append({
                    "job_submit_time": f"{boundary:.15g}",
                    "time_limit": f"{end - boundary:.15g}",
                    "num_nodes": str(nodes), "duration": f"{end - boundary:.15g}",
                    "avgpcon": required(row, "avgpcon"),
                    "minpcon": row.get("minpcon") or required(row, "avgpcon"),
                    "maxpcon": row.get("maxpcon") or required(row, "avgpcon"),
                    "exit_status": row.get("exit_status", "1"), "initialization": "1",
                    "source_trace": history_path.name, "source_job_index": str(index),
                })
                capacity_seeds.append(row)

    seeded_nodes = sum(int(row["num_nodes"]) for row in seeds)
    if seeded_nodes > args.total_nodes:
        raise SystemExit(f"initial state requires {seeded_nodes} nodes, exceeding "
                         f"--total-nodes={args.total_nodes}")
    future: list[dict[str, str]] = []
    with workload_path.open(newline="") as stream:
        for index, row in enumerate(csv.DictReader(stream)):
            # A monthly workload normally contains only this month's submits,
            # but retain the boundary contract if an input period overlaps it.
            submit = float(required(row, "job_submit_time", "submit_time"))
            if submit < boundary or (end_time is not None and submit >= end_time):
                continue
            future.append({
                "job_submit_time": required(row, "job_submit_time", "submit_time"),
                "time_limit": required(row, "time_limit", "requested_time", "wall_time"),
                "num_nodes": required(row, "num_nodes", "nodes", "nodes_requested"),
                "duration": required(row, "duration", "actual_run_time", "run_time"),
                "avgpcon": required(row, "avgpcon"),
                "minpcon": row.get("minpcon") or required(row, "avgpcon"),
                "maxpcon": row.get("maxpcon") or required(row, "avgpcon"),
                "exit_status": row.get("exit_status", "1"), "initialization": "0",
                "source_trace": workload_path.name, "source_job_index": str(index),
            })

    rows = seeds + future
    # Seeds must precede ordinary jobs submitted exactly at the boundary.
    rows.sort(key=lambda row: (float(row["job_submit_time"]),
                               0 if row["initialization"] == "1" else 1,
                               row["source_trace"], int(row["source_job_index"])))
    args.output_trace.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output_trace.with_suffix(args.output_trace.suffix + ".tmp")
    with temporary.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=output_fields)
        writer.writeheader()
        writer.writerows(rows)
    temporary.replace(args.output_trace)
    if args.capacity_history:
        # Give capacity detection the month itself plus the exact historical
        # allocations crossing its leading boundary.  This avoids treating a
        # month as empty merely because its live jobs were submitted earlier.
        current_history = args.historical_dir / slug
        if not current_history.is_file():
            raise SystemExit(f"monthly historical trace is unavailable: {current_history}")
        with current_history.open(newline="") as stream:
            reader = csv.DictReader(stream)
            history_fields = reader.fieldnames
            current_rows = list(reader)
        if not history_fields:
            raise SystemExit(f"historical trace has no header: {current_history}")
        args.capacity_history.parent.mkdir(parents=True, exist_ok=True)
        capacity_temporary = args.capacity_history.with_suffix(
            args.capacity_history.suffix + ".tmp")
        with capacity_temporary.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=history_fields,
                                    extrasaction="ignore")
            writer.writeheader()
            writer.writerows(capacity_seeds)
            writer.writerows(current_rows)
        capacity_temporary.replace(args.capacity_history)
    print(f"initialization_jobs={len(seeds)}")
    print(f"initialization_nodes={seeded_nodes}")
    print(f"simulation_start_time={boundary:.15g}")
    if end_time is not None:
        print(f"simulation_end_time={end_time:.15g}")
    print(f"future_jobs={len(future)}")
    print(f"output={args.output_trace}")
    if args.capacity_history:
        print(f"capacity_history={args.capacity_history}")


if __name__ == "__main__":
    main()
