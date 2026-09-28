#!/usr/bin/env python3
"""Generate Slurm jobs for capped-backfill monthly warm-start experiments."""

import argparse
from pathlib import Path
import sys


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent / "scripts"))
from generate_monthly_warm_start_cap_jobs import generate as generate_variant


def generate(trace_dir: Path, output_dir: Path):
    return generate_variant(
        trace_dir, output_dir, runner=HERE / "run_one.sh",
        manifest_kind="easypower-cap", filename_prefix="easypower-cap",
        job_prefix="epcap")


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
