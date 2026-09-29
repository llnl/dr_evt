#!/usr/bin/env python3
"""Generate one EASY+PC-Mean and EASY+PC-Max Slurm job per month."""

import argparse
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def month_label(trace: Path) -> str:
    year, month = trace.name[:5].split("_")
    return f"20{year}-{month}"


def generate(trace_dir: Path, output_dir: Path):
    traces = sorted(trace_dir.glob("[0-9][0-9]_[0-9][0-9]_scheduling_trace.csv"))
    if not traces:
        raise SystemExit(f"no monthly traces found in {trace_dir}")
    jobs_dir = output_dir / "jobs"
    logs_dir = output_dir / "logs"
    jobs_dir.mkdir(parents=True, exist_ok=True)
    logs_dir.mkdir(parents=True, exist_ok=True)
    runner = HERE / "run_one.sh"
    manifest = ["kind\tmonth\tpower_mode\tscript\tprerequisite"]
    for trace in traces:
        month = month_label(trace)
        for mode in ("mean", "max"):
            filename = f"easy-pc-{mode}-{month}.slurm"
            job_name = f"epc-{mode}-{month}"
            path = jobs_dir / filename
            path.write_text(f"""#!/usr/bin/env bash
#SBATCH --job-name={job_name}
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --exclusive
#SBATCH --time=06:00:00
#SBATCH --output={logs_dir}/%x-%j.out
#SBATCH --error={logs_dir}/%x-%j.err
set -euo pipefail
exec '{runner}' '{mode}' '{month}'
""")
            path.chmod(0o700)
            manifest.append(
                f"easy-pc\t{month}\t{mode}\tjobs/{filename}\t"
                "shared input, capacity, and baseline")
    (output_dir / "manifest.tsv").write_text("\n".join(manifest) + "\n")
    return len(traces), len(manifest) - 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace-dir", type=Path,
                        default=ROOT / "traces_no_times_nonoverlap")
    parser.add_argument("--output-dir", type=Path, default=HERE)
    args = parser.parse_args()
    months, jobs = generate(args.trace_dir.resolve(), args.output_dir.resolve())
    print(f"Generated {months} months and {jobs} Slurm jobs")


if __name__ == "__main__":
    main()
