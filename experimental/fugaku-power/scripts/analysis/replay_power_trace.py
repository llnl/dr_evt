#!/usr/bin/env python3
"""Replay real-log job intervals into a node and power resource trace.

This is an event replay of an operational log, not a scheduling simulation:
begin_time and end_time are consumed exactly as recorded in the input CSV.
"""

import argparse
import csv
from pathlib import Path


REQUIRED_COLUMNS = (
    "begin_time",
    "end_time",
    "num_nodes",
    "avgpcon",
    "minpcon",
    "maxpcon",
)


def parse_time(value, column, row_number):
    try:
        return int(float(value))
    except ValueError as error:
        raise ValueError(
            f"row {row_number}: invalid {column} value {value!r}") from error


def load_events(path):
    events = []
    jobs = 0

    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        missing = [name for name in REQUIRED_COLUMNS
                   if name not in (reader.fieldnames or [])]
        if missing:
            raise ValueError(
                f"{path} is missing required columns: {', '.join(missing)}")

        for row_number, row in enumerate(reader, start=2):
            begin = parse_time(row["begin_time"], "begin_time", row_number)
            end = parse_time(row["end_time"], "end_time", row_number)
            if end < begin:
                raise ValueError(
                    f"row {row_number}: end_time {end} precedes begin_time "
                    f"{begin}")

            try:
                nodes = int(float(row["num_nodes"]))
                avg = float(row["avgpcon"])
                minimum = float(row["minpcon"])
                maximum = float(row["maxpcon"])
            except ValueError as error:
                raise ValueError(
                    f"row {row_number}: invalid node or power value") from error

            if nodes < 0:
                raise ValueError(
                    f"row {row_number}: num_nodes must be nonnegative")

            events.append((begin, nodes, avg, minimum, maximum))
            events.append((end, -nodes, -avg, -minimum, -maximum))
            jobs += 1

    if not events:
        raise ValueError(f"{path} contains no jobs")

    events.sort(key=lambda event: event[0])
    return jobs, events


def write_resource_trace(events, path, total_nodes):
    allocated = 0
    avg = 0.0
    minimum = 0.0
    maximum = 0.0
    peak_allocated = 0
    peak_time = 0
    sample_count = 0

    with path.open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("time", "free_nodes", "allocated_nodes",
                         "avgpcon", "minpcon", "maxpcon"))
        writer.writerow((0, total_nodes, 0, 0.0, 0.0, 0.0))

        index = 0
        while index < len(events):
            timestamp = events[index][0]
            while index < len(events) and events[index][0] == timestamp:
                _, node_delta, avg_delta, min_delta, max_delta = events[index]
                allocated += node_delta
                avg += avg_delta
                minimum += min_delta
                maximum += max_delta
                index += 1

            # When no jobs remain, all aggregate power values are exactly
            # zero by definition. Reset them explicitly instead of exposing
            # floating-point subtraction residue in the output.
            if allocated == 0:
                avg = 0.0
                minimum = 0.0
                maximum = 0.0
            else:
                if abs(avg) < 1e-9:
                    avg = 0.0
                if abs(minimum) < 1e-9:
                    minimum = 0.0
                if abs(maximum) < 1e-9:
                    maximum = 0.0

            if allocated < 0:
                raise ValueError(
                    f"resource replay became negative at time {timestamp}")
            if allocated > peak_allocated:
                peak_allocated = allocated
                peak_time = timestamp

            writer.writerow((timestamp, total_nodes - allocated, allocated,
                             avg, minimum, maximum))
            sample_count += 1

    return peak_allocated, peak_time, sample_count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input_trace", type=Path)
    parser.add_argument("output_trace", type=Path)
    parser.add_argument("--total-nodes", type=int, default=158976)
    args = parser.parse_args()

    if args.total_nodes <= 0:
        parser.error("--total-nodes must be positive")

    jobs, events = load_events(args.input_trace)
    args.output_trace.parent.mkdir(parents=True, exist_ok=True)
    peak, peak_time, sample_count = write_resource_trace(
        events, args.output_trace, args.total_nodes)

    print(f"jobs={jobs}")
    print(f"resource_samples={sample_count}")
    print(f"peak_allocated_nodes={peak}")
    print(f"peak_time={peak_time}")
    print(f"peak_utilization_percent={100.0 * peak / args.total_nodes:.3f}")
    print(f"output={args.output_trace}")


if __name__ == "__main__":
    main()
