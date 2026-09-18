#!/usr/bin/env python3
"""Generate independent fiscal-half Slurm scripts and fixed input lists."""

from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
TRACE_DIR = ROOT / "traces_no_times_nonoverlap"
JOB_WINDOWS = (16, 32, 64, 128, 256)
TIME_WINDOWS = (
    ("unlimited", 0, "unlimited"),
    ("1h", 3600, "3600s"),
    ("6h", 21600, "21600s"),
)


def fiscal_half(path: Path, first_year: int, first_month: int) -> str:
    year, month = (int(value) for value in path.name[:5].split("_"))
    year += 2000
    if year == first_year and first_month <= 3 and month <= 3:
        return f"FY{year}-H1"
    if month <= 3:
        return f"FY{year - 1}-H2"
    if month <= 9:
        return f"FY{year}-H1"
    return f"FY{year}-H2"


def job_text(name, arguments):
    helper = HERE / "run_one.sh"
    quoted = " ".join(f"'{argument}'" for argument in arguments)
    return f"""#!/usr/bin/env bash
#SBATCH --job-name={name}
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --exclusive
#SBATCH --time=06:00:00
#SBATCH --output={HERE}/logs/%x-%j.out
#SBATCH --error={HERE}/logs/%x-%j.err

set -euo pipefail
exec '{helper}' {quoted}
"""


def write_job(directory, filename, name, arguments):
    path = directory / filename
    path.write_text(job_text(name, arguments))
    path.chmod(0o700)
    return path


def main() -> None:
    traces = sorted(TRACE_DIR.glob("[0-9][0-9]_[0-9][0-9]_scheduling_trace.csv"))
    if not traces:
        raise SystemExit(f"no monthly traces found in {TRACE_DIR}")
    first_year = 2000 + int(traces[0].name[:2])
    first_month = int(traces[0].name[3:5])
    grouped = {}
    for trace in traces:
        grouped.setdefault(fiscal_half(trace, first_year, first_month), []).append(trace)

    input_dir = HERE / "infile-lists"
    capacity_dir = HERE / "jobs" / "capacity"
    baseline_dir = HERE / "jobs" / "baseline"
    easypower_dir = HERE / "jobs" / "easypower"
    logs_dir = HERE / "logs"
    for directory in (input_dir, capacity_dir, baseline_dir, easypower_dir, logs_dir):
        directory.mkdir(parents=True, exist_ok=True)

    manifest = ["kind\tseason\tjob_window\ttime_window\tscript\tprerequisite"]
    for label, inputs in sorted(grouped.items()):
        (input_dir / f"{label}.txt").write_text(
            "".join(f"{path.resolve()}\n" for path in inputs))

        capacity = write_job(
            capacity_dir, f"capacity-{label}.slurm", f"cap-{label}",
            ("capacity", label))
        manifest.append(
            f"capacity\t{label}\t\t\t{capacity.relative_to(HERE)}\t")

        baseline = write_job(
            baseline_dir, f"baseline-{label}.slurm", f"easy-{label}",
            ("baseline", label))
        manifest.append(
            f"baseline\t{label}\t\t\t{baseline.relative_to(HERE)}\t"
            f"{capacity.relative_to(HERE)}")

        for job_window in JOB_WINDOWS:
            for time_label, seconds, output_slug in TIME_WINDOWS:
                candidate = write_job(
                    easypower_dir,
                    f"easypower-{label}-n{job_window}-t{time_label}.slurm",
                    f"ep-{label}-n{job_window}-t{time_label}",
                    ("easypower", label, str(job_window), str(seconds), output_slug))
                manifest.append(
                    f"easypower\t{label}\t{job_window}\t{time_label}\t"
                    f"{candidate.relative_to(HERE)}\t{capacity.relative_to(HERE)}")

    (HERE / "manifest.tsv").write_text("\n".join(manifest) + "\n")
    print(f"Generated {len(grouped)} capacity jobs")
    print(f"Generated {len(grouped)} baseline jobs")
    print(f"Generated {len(grouped) * len(JOB_WINDOWS) * len(TIME_WINDOWS)} EASYPower jobs")


if __name__ == "__main__":
    main()
