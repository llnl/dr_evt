# Individual monthly warm-start jobs with capped FCFS power

This suite mirrors `individual-monthly-warm-start-jobs`, but every EASYPower
run enables `--cap_fcfs_power` and leaves `--cap_backfill_power` disabled.
Resource-feasible FCFS-prefix jobs wait when their projected aggregate power
would exceed `MAXIMUM_POWER`; backfill candidates retain the original
EASYPower ranking behavior and may exceed that value.

The inherited defaults are `MAXIMUM_POWER=12000000` (12 MW) and
`INITIAL_TARGET=MAXIMUM_POWER`. The 12 MW value is an earlier study parameter,
not a documented physical Fugaku limit. The FCFS cap does not preempt jobs or
reduce power already present at the warm-start boundary. Every EASYPower run
drops an input job whose `maxpcon` exceeds `MAXIMUM_POWER` before
assigning simulation job IDs and reports it in the log. This per-job admission
rule is independent of the aggregate FCFS cap.

Results default to:

```text
experimental/fugaku-power/results/individual-monthly-warm-start-fcfs-cap-sweep
```

Prepared monthly input, capacity schedules, and EASY baselines are reused from
`individual-monthly-warm-start-sweep`. This suite generates only capped
EASYPower jobs. Set `SHARED_OUTPUT_ROOT` if the original results are elsewhere.

Generate and submit jobs as follows:

```bash
./generate_job_set.py
sbatch jobs/easypower/easypower-fcfs-cap-2023-06-n64-t1h.slurm
./run_n_left.sh 12
```

The original suite's environment overrides remain available. Use a distinct
`OUTPUT_ROOT` for every parameter set.
