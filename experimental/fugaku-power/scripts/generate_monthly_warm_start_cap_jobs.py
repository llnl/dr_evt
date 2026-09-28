#!/usr/bin/env python3
"""Shared generator for monthly warm-start power-cap experiment suites."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
JOB_WINDOWS = (16, 32, 64, 128, 256)
TIME_WINDOWS = (("unlimited", 0, "unlimited"), ("1h", 3600, "3600s"),
                ("6h", 21600, "21600s"))


def month_label(trace: Path) -> str:
    year, month = trace.name[:5].split("_")
    return f"20{year}-{month}"


def job_text(output_dir: Path, runner: Path, name: str, arguments) -> str:
    quoted = " ".join(f"'{argument}'" for argument in arguments)
    return f"""#!/usr/bin/env bash
#SBATCH --job-name={name}
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --exclusive
#SBATCH --time=06:00:00
#SBATCH --output={output_dir}/logs/%x-%j.out
#SBATCH --error={output_dir}/logs/%x-%j.err
set -euo pipefail
exec '{runner}' {quoted}
"""


def write_job(output_dir: Path, runner: Path, directory: Path, filename: str,
              name: str, arguments) -> Path:
    path = directory / filename
    path.write_text(job_text(output_dir, runner, name, arguments))
    path.chmod(0o700)
    return path


def generate(trace_dir: Path, output_dir: Path, *, runner: Path,
             manifest_kind: str, filename_prefix: str, job_prefix: str):
    traces = sorted(trace_dir.glob("[0-9][0-9]_[0-9][0-9]_scheduling_trace.csv"))
    if not traces:
        raise SystemExit(f"no monthly traces found in {trace_dir}")

    power_dir = output_dir / "jobs" / "easypower"
    for directory in (power_dir, output_dir / "logs"):
        directory.mkdir(parents=True, exist_ok=True)

    manifest = ["kind\tmonth\tjob_window\ttime_window\tscript\tprerequisite"]
    for trace in traces:
        month = month_label(trace)
        for window in JOB_WINDOWS:
            for time_label, seconds, slug in TIME_WINDOWS:
                candidate = write_job(
                    output_dir, runner, power_dir,
                    f"{filename_prefix}-{month}-n{window}-t{time_label}.slurm",
                    f"{job_prefix}-{month}-n{window}-t{time_label}",
                    ("easypower", month, str(window), str(seconds), slug),
                )
                manifest.append(
                    f"{manifest_kind}\t{month}\t{window}\t{time_label}\t"
                    f"{candidate.relative_to(output_dir)}\t"
                    "shared capacity and baseline")

    (output_dir / "manifest.tsv").write_text("\n".join(manifest) + "\n")
    return len(traces), len(manifest) - 1
