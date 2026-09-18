# Individual fiscal-half jobs

This directory replaces the nested 35-node `srun` workflow with independent,
single-node Slurm jobs. It does not call or modify the original fiscal sweep
batch script.

The generated set contains:

- 7 capacity-schedule jobs, one per fiscal half;
- 7 EASY baseline simulation jobs, one per fiscal half;
- 105 EASYPower simulation jobs: 7 fiscal halves x 5 job windows x 3 time
  windows;
- 112 independent simulation jobs in total.

All inputs, scripts, logs, and results use absolute paths on the shared
`/p/vast1` NFS filesystem. No result depends on compute-node-local storage.

## Run order

First run and inspect each capacity job:

```bash
sbatch jobs/capacity/capacity-FY2021-H1.slurm
```

The simulations deliberately fail fast until their season has a complete
capacity schedule. Once it does, the baseline and any candidate can be run
individually and in any order:

```bash
sbatch jobs/baseline/baseline-FY2021-H1.slurm
sbatch jobs/easypower/easypower-FY2021-H1-n16-tunlimited.slurm
```

The default result root is
`experimental/fugaku-power/results/individual-fiscal-sweep`. Override it for
every related job with, for example:

```bash
sbatch --export=ALL,OUTPUT_ROOT=/shared/path/run-1 jobs/capacity/capacity-FY2021-H1.slurm
```

Use the same `OUTPUT_ROOT`, `TOTAL_NODES`, `MAXIMUM_POWER`, `INITIAL_TARGET`,
and `CAPACITY_SCENARIO` values for all jobs in one experiment.

Each task stages output in a job-specific directory, validates required CSVs,
and moves the directory to its final name only after success. A completed task
is safe to invoke again; an incomplete final directory is never overwritten.

## Analysis

After the baseline and all three time-window candidates for a season/job-window
pair finish, run:

```bash
./analyze_one.sh FY2021-H1 16
```

After all 112 simulations finish, `./analyze_all.sh` creates the same
per-job-window reports expected by the existing fiscal result collector.

`manifest.tsv` lists every job and its capacity-job prerequisite. Regenerate
the files after changing the trace inventory with `./generate_job_set.py`.
