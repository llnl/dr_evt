# Individual monthly warm-start jobs with capped backfill power

This suite mirrors `individual-monthly-warm-start-jobs`, but every EASYPower
run enables `--cap_backfill_power`. A backfill candidate is therefore not
started when its projected system power would exceed `MAXIMUM_POWER`. FCFS
admission is unchanged and remains uncapped.

The suite delegates warm-start handling and cutoff behavior to the original
monthly runner. It reuses the prepared input, capacity schedule, and EASY
baseline under the original uncapped sweep root; it does not generate or run
duplicate capacity or baseline jobs. Its capped EASYPower results remain under
a separate default root:

```text
experimental/fugaku-power/results/individual-monthly-warm-start-backfill-cap-sweep
```

## Power setup

The inherited defaults are:

```text
MAXIMUM_POWER=12000000   # 12 MW
INITIAL_TARGET=12000000  # defaults to MAXIMUM_POWER
```

The 12 MW value originated as a parameter of the earlier EASYPower study. It
is not documented as a measured or published physical power limit for Fugaku.
Override `MAXIMUM_POWER` when the experiment requires a different,
scientifically justified limit; use the same value for every comparable run.
Every EASYPower run drops an input job whose `maxpcon` exceeds
`MAXIMUM_POWER`, before assigning simulation job IDs. Each dropped job is
reported in the log. This per-job admission rule is independent of the
backfill and FCFS aggregate-power switches.

EASYPower periodically calculates a desired power target from estimated queued
and running-job energy over its prediction horizon, then clamps that target to
the interval from zero through `MAXIMUM_POWER`. Target clamping alone only
ranks candidates: an uncapped scheduler can still start the least-overshooting
candidate when every candidate exceeds the maximum.

This suite additionally enables the hard backfill admission check. It rejects
each backfill candidate for which

```text
current running power + candidate predicted power > MAXIMUM_POWER
```

If every candidate would exceed the limit, no job is backfilled during that
decision. The cap does not preempt running jobs, reduce existing warm-start
power, or restrict FCFS-prefix jobs; `--cap_fcfs_power` is intentionally not
enabled by this suite.

Generate the Slurm scripts after changing the monthly trace inventory:

```bash
./generate_job_set.py
```

First complete the original suite's capacity and baseline jobs for a month,
then submit only the capped EASYPower job from this suite:

```bash
sbatch jobs/easypower/easypower-cap-2023-06-n64-t1h.slurm
```

To submit up to 12 unfinished capped EASYPower jobs from the manifest, use:

```bash
./run_n_left.sh 12
```

The environment variables supported by the original suite remain available,
including `OUTPUT_ROOT`, `TOTAL_NODES`, `MAXIMUM_POWER`, `INITIAL_TARGET`,
`CAPACITY_SCENARIO`, `HISTORICAL_TRACE_DIR`, `WORKLOAD_TRACE_DIR`,
`TRACE_TIMEZONE`, `SIM_START_TIME`, and `SIM_END_TIME`. Do not reuse an output
root containing uncapped results. `SHARED_OUTPUT_ROOT` can override the
original result root from which input, capacity, and baseline artifacts are
read.

Run the focused script test with:

```bash
python3 tests/test_monthly_backfill_cap_scripts.py
```
