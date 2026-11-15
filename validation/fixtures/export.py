#!/usr/bin/env python3
"""Export compact validation fixtures from audited native result reports."""

import argparse
import csv
import json
from pathlib import Path


def write_csv(path, header, rows):
    with path.open("w", newline="\n") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(header)
        writer.writerows(rows)


def expand_slurm_nodes(expression):
    nodes = []
    for part in expression.replace("n[", "").replace("]", "").replace("n", "").split(","):
        if "-" in part:
            first, last = map(int, part.split("-"))
            nodes.extend(range(first, last + 1))
        elif part:
            nodes.append(int(part))
    return nodes


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--slurm-capacity-report", type=Path, required=True)
    parser.add_argument("--slurm-placement-report", type=Path, required=True)
    parser.add_argument("--openpbs-capacity-report", type=Path, required=True)
    parser.add_argument("--openpbs-placement-report", type=Path, required=True)
    parser.add_argument("--outdir", type=Path, default=Path(__file__).parent)
    args = parser.parse_args()

    reports = {name: json.loads(path.read_text()) for name, path in {
        "slurm_capacity": args.slurm_capacity_report,
        "slurm_placement": args.slurm_placement_report,
        "openpbs_capacity": args.openpbs_capacity_report,
        "openpbs_placement": args.openpbs_placement_report,
    }.items()}
    jobs = reports["slurm_placement"]["jobs"]
    if reports["slurm_capacity"]["jobs"] != jobs:
        raise ValueError("Slurm reports do not contain the same workload")
    for name, path in (("openpbs_capacity", args.openpbs_capacity_report),
                       ("openpbs_placement", args.openpbs_placement_report)):
        report = reports[name]
        if report["jobs"] != len(jobs):
            raise ValueError(f"{name} job count differs")
        with (path.parent / "jobs.csv").open(newline="") as stream:
            pbs_jobs = [[int(row[field]) for field in
                         ("job_submit_time", "num_nodes", "time_limit", "duration")]
                        for row in csv.DictReader(stream)]
        if pbs_jobs != jobs:
            raise ValueError(f"{name} workload differs")
    args.outdir.mkdir(parents=True, exist_ok=True)
    write_csv(args.outdir / "jobs.csv",
              ("job_id", "submit_time", "nodes", "time_limit", "actual_runtime"),
              ((index + 1, *job) for index, job in enumerate(jobs)))

    slurm = reports["slurm_placement"]
    write_csv(args.outdir / "slurm-placement.csv",
              ("job_id", "start", "end", "node_ids"),
              ((index + 1, row["start"], row["end"],
                ";".join(map(str, expand_slurm_nodes(row["assignment"]))))
               for index, row in enumerate(slurm["slurm_schedule"])))

    for model in ("capacity", "placement"):
        report = reports[f"openpbs_{model}"]
        nodes = report.get("openpbs_node_ids", [[] for _ in jobs])
        write_csv(args.outdir / f"openpbs-{model}.csv",
                  ("job_id", "start", "end", "node_ids"),
                  ((index + 1, row[0], row[1], ";".join(map(str, nodes[index])))
                   for index, row in enumerate(report["openpbs_schedule"])))

    capacity = reports["slurm_capacity"]
    write_csv(args.outdir / "slurm-capacity.csv",
              ("job_id", "slurm_start", "slurm_end", "dr_evt_start", "dr_evt_end"),
              ((index + 1, native["start"], native["end"],
                drevt["start"], drevt["end"])
               for index, (native, drevt) in enumerate(zip(
                   capacity["slurm_schedule"], capacity["dr_evt_schedule"]))))


if __name__ == "__main__":
    main()
