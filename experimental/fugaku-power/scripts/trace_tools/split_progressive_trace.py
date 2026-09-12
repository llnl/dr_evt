#!/usr/bin/env python3
"""Split a sorted simulation trace into files for --infile_list loading."""

import argparse
import csv
from decimal import Decimal, InvalidOperation
from pathlib import Path


def submit_time(row, row_number):
    try:
        return Decimal(row["job_submit_time"])
    except KeyError as error:
        raise ValueError("input has no job_submit_time column") from error
    except InvalidOperation as error:
        raise ValueError(
            f"row {row_number} has invalid job_submit_time "
            f"{row.get('job_submit_time')!r}") from error


def split_trace(input_path, output_dir, rows_per_file, list_file):
    output_dir.mkdir(parents=True, exist_ok=True)
    part_paths = []
    output_stream = None
    writer = None
    rows_in_part = 0
    previous_time = None

    try:
        with input_path.open(newline="") as input_stream:
            reader = csv.DictReader(input_stream)
            if not reader.fieldnames or "job_submit_time" not in reader.fieldnames:
                raise ValueError("input has no job_submit_time column")

            for row_number, row in enumerate(reader, start=2):
                current_time = submit_time(row, row_number)
                if previous_time is not None and current_time < previous_time:
                    raise ValueError(
                        f"input is not sorted: row {row_number} has submit time "
                        f"{current_time}, earlier than {previous_time}")

                # Do not divide jobs submitted at the same instant between
                # files: progressive loading advances after each file, so
                # such a split could change same-time scheduling decisions.
                if (output_stream is None or
                        (rows_in_part >= rows_per_file and
                         current_time != previous_time)):
                    if output_stream is not None:
                        output_stream.close()
                    part_path = (output_dir /
                                 f"{input_path.stem}.part-{len(part_paths):04d}.csv")
                    output_stream = part_path.open("w", newline="")
                    writer = csv.DictWriter(output_stream,
                                            fieldnames=reader.fieldnames)
                    writer.writeheader()
                    part_paths.append(part_path.resolve())
                    rows_in_part = 0

                writer.writerow(row)
                rows_in_part += 1
                previous_time = current_time
    finally:
        if output_stream is not None:
            output_stream.close()

    if not part_paths:
        raise ValueError("input contains no job rows")

    list_file.parent.mkdir(parents=True, exist_ok=True)
    with list_file.open("w") as stream:
        for part_path in part_paths:
            stream.write(f"{part_path}\n")

    print(f"created {len(part_paths)} files in {output_dir}")
    print(f"infile_list={list_file.resolve()}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--rows-per-file", type=int, default=50000)
    parser.add_argument("--list-file", type=Path)
    args = parser.parse_args()

    if args.rows_per_file <= 0:
        parser.error("--rows-per-file must be positive")
    list_file = args.list_file or args.output_dir / "file_list.txt"
    split_trace(args.input, args.output_dir, args.rows_per_file, list_file)


if __name__ == "__main__":
    main()
