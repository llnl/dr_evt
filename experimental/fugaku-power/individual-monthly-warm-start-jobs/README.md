# Individual monthly warm-start jobs

This is the monthly counterpart to `individual-fiscal-jobs`.  Each monthly
simulation starts at midnight on the first day of the month (default timezone:
`JST`). Before that month's ordinary arrivals are submitted, every job from
the historical traces that satisfies `begin_time <= month_start < end_time` is
materialized as its own initialization job. Its requested and actual duration
are both its remaining historical runtime. Jobs ending exactly at the boundary
are excluded; jobs beginning there remain ordinary arrivals.

The initialization records retain the historical PCON fields, are marked
`initialization=1`, and are visible in normal simulation output. They are an
explicit-job warm start, rather than the simulator's replay warm-start mode.

## Generate and run

Run this once after changing the monthly trace inventory:

```bash
./generate_job_set.py
```

For a month, run its capacity job first, then the baseline and any candidate:

```bash
sbatch jobs/capacity/capacity-2023-06.slurm
sbatch jobs/baseline/baseline-2023-06.slurm
sbatch jobs/easypower/easypower-2023-06-n64-t1h.slurm
```

The monthly trace is created lazily at
`OUTPUT_ROOT/YYYY-MM/input/warm-start-scheduling_trace.csv`; its log reports
the initialization-job and initialization-node counts. The default result root
is `experimental/fugaku-power/results/individual-monthly-warm-start-sweep`.
Set the same `OUTPUT_ROOT`, `TOTAL_NODES`, `MAXIMUM_POWER`, `INITIAL_TARGET`,
`CAPACITY_SCENARIO`, `HISTORICAL_TRACE_DIR`, `WORKLOAD_TRACE_DIR`, and
`TRACE_TIMEZONE` for all jobs in one experiment.

`CAP_BACKFILL_POWER=1` passes `--cap_backfill_power` to EASYPower runs. The
dedicated `individual-monthly-warm-start-backfill-cap-jobs` suite sets this
option automatically and uses a separate default result root.
`CAP_FCFS_POWER=1` similarly passes `--cap_fcfs_power`; the dedicated FCFS-only
and both-caps suites select the intended combination explicitly. All three cap
suites generate only EASYPower jobs and reuse this suite's prepared input,
capacity schedules, and EASY baselines. Their `SHARED_OUTPUT_ROOT` override
selects a non-default source root when necessary.

All EASYPower modes drop an input job during loading when its requested nodes
exceed `TOTAL_NODES` or its `maxpcon` exceeds `MAXIMUM_POWER`.
The run log identifies every dropped source row and its relevant values. The
per-job power rule is independent of the optional aggregate backfill and FCFS
caps; EASY baselines apply only the node limit.

Capacity detection uses a generated historical boundary-context trace: the
original month’s records plus only historical jobs still running at the month
boundary. This lets it recognize opening occupancy without allowing synthetic
initialization records to distort the inferred capacity schedule. The
simulation input scans historical files up to and including the selected month,
so jobs lasting longer than one month are also seeded.

Every baseline and EASYPower run has an inclusive cutoff at midnight on the
first day of the following month. Jobs still running then remain live in the
resource output's final state, but no later completion events are processed.

## Custom boundaries

Set `SIM_START_TIME` and/or `SIM_END_TIME` when submitting a job to override
the default monthly start and end boundaries. Each accepts epoch seconds or an
ISO timestamp; an ISO time without an offset is interpreted in
`TRACE_TIMEZONE`. The warm jobs are selected at `SIM_START_TIME`, ordinary
arrivals before it (or at/after `SIM_END_TIME`) are excluded, and the simulator
stops inclusively at `SIM_END_TIME`. Capacity detection uses the same range
(`SIM_START_TIME` inclusive, `SIM_END_TIME` exclusive) while retaining the
opening allocations of jobs that began before the start boundary.

```bash
sbatch --export=ALL,SIM_START_TIME=2023-06-05T00:00:00,SIM_END_TIME=2023-06-19T00:00:00 \
  jobs/baseline/baseline-2023-06.slurm
```

Use a distinct `OUTPUT_ROOT` for each distinct boundary pair; completed-result
checks are intentionally keyed by the month label within that root.

## TODO: native replay warm start

Replace the current synthetic initialization jobs with native replay warm
start. The replay implementation must reconstruct both (1) jobs that were
already running at `SIM_START_TIME` and (2) jobs submitted before that boundary
that were still waiting then. It must preserve their resource and power state,
respect the capacity schedule and requested simulation range during
reconstruction, and only then continue normal scheduling. Until that work is
implemented, the explicit synthetic warm jobs remain the supported approach.

## Aggregate metrics and plots

Collect the completed monthly EASY baselines and the EASYPower N=64,
unlimited-time-window runs into one wide CSV, then create plots grouped by
related units:

```bash
python3 experimental/fugaku-power/results/individual-monthly-warm-start-sweep/scripts/collect_monthly_metrics.py
python3 experimental/fugaku-power/results/individual-monthly-warm-start-sweep/scripts/plot_monthly_metrics.py
```

The defaults write `monthly_metrics.csv`, `monthly_metrics_catalog.csv`, and
the `monthly-metric-plots/` directory under the monthly result root. The main
CSV has one row per month and algorithm. Metrics that apply to both schedulers
are populated for both; horizon and target telemetry are EASYPower-only and
are blank on EASY rows. The catalog labels every metric as `common` or
`easypower`. Use `--candidate-jobs` and `--candidate-time-window-s` to select a
different EASYPower configuration. Ramp-rate columns use physical endpoint
windows of 60, 300, and 900 seconds and one-second sampling by default. Use
`--workers` for concurrent month analysis and a larger
`--endpoint-sample-seconds` for a faster exploratory plot.

See `results/individual-monthly-warm-start-sweep/METRIC_DEFINITIONS.md` for
the units and exact calculation of every CSV column.
