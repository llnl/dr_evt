#!/usr/bin/env python3
"""Generate independent Slurm jobs for monthly warm-start experiments."""

from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
TRACE_DIR = ROOT / "traces_no_times_nonoverlap"
JOB_WINDOWS = (16, 32, 64, 128, 256)
TIME_WINDOWS = (("unlimited", 0, "unlimited"), ("1h", 3600, "3600s"),
                ("6h", 21600, "21600s"))


def label(trace: Path) -> str:
    year, month = trace.name[:5].split("_")
    return f"20{year}-{month}"


def job_text(name, arguments):
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
exec '{HERE / 'run_one.sh'}' {quoted}
"""


def write_job(directory, filename, name, arguments):
    path = directory / filename
    path.write_text(job_text(name, arguments))
    path.chmod(0o700)
    return path


def main():
    traces = sorted(TRACE_DIR.glob("[0-9][0-9]_[0-9][0-9]_scheduling_trace.csv"))
    if not traces:
        raise SystemExit(f"no monthly traces found in {TRACE_DIR}")
    input_dir = HERE / "historical-infile-lists"
    capacity_dir, baseline_dir, power_dir = (HERE / "jobs" / name for name in
                                              ("capacity", "baseline", "easypower"))
    for directory in (input_dir, capacity_dir, baseline_dir, power_dir, HERE / "logs"):
        directory.mkdir(parents=True, exist_ok=True)
    manifest = ["kind\tmonth\tjob_window\ttime_window\tscript\tprerequisite"]
    for trace in traces:
        month = label(trace)
        # Capacity detection uses the unmodified historical trace.  The
        # simulation input is constructed by run_one.sh in its result root.
        (input_dir / f"{month}.txt").write_text(f"{(ROOT / 'traces_nonoverlap' / trace.name).resolve()}\n")
        capacity = write_job(capacity_dir, f"capacity-{month}.slurm", f"cap-{month}",
                             ("capacity", month))
        baseline = write_job(baseline_dir, f"baseline-{month}.slurm", f"easy-{month}",
                             ("baseline", month))
        manifest += [f"capacity\t{month}\t\t\t{capacity.relative_to(HERE)}\t",
                     f"baseline\t{month}\t\t\t{baseline.relative_to(HERE)}\t{capacity.relative_to(HERE)}"]
        for window in JOB_WINDOWS:
            for time_label, seconds, slug in TIME_WINDOWS:
                candidate = write_job(power_dir, f"easypower-{month}-n{window}-t{time_label}.slurm",
                                      f"ep-{month}-n{window}-t{time_label}",
                                      ("easypower", month, str(window), str(seconds), slug))
                manifest.append(f"easypower\t{month}\t{window}\t{time_label}\t"
                                f"{candidate.relative_to(HERE)}\t{capacity.relative_to(HERE)}")
    (HERE / "manifest.tsv").write_text("\n".join(manifest) + "\n")
    print(f"Generated {len(traces)} months and {len(manifest) - 1} Slurm jobs")


if __name__ == "__main__":
    main()
