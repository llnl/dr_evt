# Slurm Historical Diagnostics

These experiments explain earlier Slurm capacity results. Each job uses a
private dummy node and draws from a shared 756-license pool. Because private
nodes cannot protect shared-license capacity, `bf_licenses=off` is a diagnostic
control, not ordinary Slurm backfill on shared compute nodes.

## Result History

| Experiment | Differing starts out of 2,000 | Explanation |
|---|---:|---|
| License off vs protected DR_EVT | 1,943 | Different future-capacity protection |
| License off vs no-reservation DR_EVT | 0 | Same current-capacity policy and event batching |
| First gated license-off attempt | 972 | Initial main dispatch was not acknowledged |
| Historical license-on run | 0 | Native main dispatch happened before bounded backfill at the relevant tick |
| Current license-on run | 2 | Native backfill happened first at one relevant tick |

The current result is reported on [Slurm Validation](SLURM_VALIDATION.md). The
zero- and two-difference license-on runs use the same scheduling configuration;
the adapter batches equal-time events but does not order Slurm's independent
main-dispatch and backfill triggers.

## Reservation Protection

Comparing license-off Slurm with protected DR_EVT isolates the effect of future
capacity reservations.

| Metric | Protected DR_EVT | Slurm, `bf_licenses` off |
|---|---:|---:|
| Mean waiting time | 10.3973 h | 3.3511 h |
| Makespan | 40.6236 h | 33.3878 h |
| Resource area | 81,309,892 node-seconds | 81,309,892 node-seconds |
| Utilization | 73.5429% | 89.4812% |
| Maximum waiting time | 18.4183 h | 30.8072 h |
| Process runtime | 0.036436 s | 739.2877 s |

Disabling future protection lets more jobs start early, but later jobs can then
delay an earlier waiting job and increase its maximum wait.

:::{figure} ../_static/scheduler-validation/slurm/bf-licenses-off/protected-comparison/comparison-metrics.svg
:alt: Historical Slurm license-off comparison showing lower mean wait and makespan but a longer maximum wait than protected DR_EVT
:width: 100%

License-off Slurm compared with protected DR_EVT: 1,943 starts differ.
:::

[PDF](../_static/scheduler-validation/slurm/bf-licenses-off/protected-comparison/comparison-metrics.pdf)

### Four-Job Controls

Both controls have four capacity units and simultaneous arrivals. A and B use
two units for 120 and 180 seconds; D uses one for 120 seconds.

When C requests three units for 60 seconds, all configurations start D at 120
and C at 180 because they can coexist.

:::{figure} ../_static/scheduler-validation/slurm/controls/four-job-node-assignments.svg
:alt: Four-job capacity control where license planning does not change any start
:width: 100%

Three-unit C: all schedules match.
:::

When C instead requests all four units, protected Slurm and DR_EVT preserve C's
start at 180 and delay D until 240. License-off Slurm and no-reservation DR_EVT
start D at 120 and delay C until 240.

:::{figure} ../_static/scheduler-validation/slurm/controls/reservation-protection-node-assignments.svg
:alt: Four-job capacity control where future license reservations protect C from D
:width: 100%

Four-unit C: future license reservations change the schedule.
:::

These rows are feasible drawings of interchangeable capacity, not recorded node
bindings. Node-bound reservation effects are separate and are illustrated in
the [Fiction comparison](FLUX_VALIDATION.md#minimal-reproducer-for-backfill-start-time-differences).

## No-Reservation Reference

`--diagnostic_no_reservations` scans eligible jobs in FCFS order, starts every
job that fits current capacity, and creates no future reservation. It matches
the acknowledged license-off Slurm run exactly.

| Metric | No-reservation DR_EVT | Slurm, `bf_licenses` off |
|---|---:|---:|
| Mean waiting time | 3.3511 h | 3.3511 h |
| Makespan | 33.3878 h | 33.3878 h |
| Resource area | 81,309,892 node-seconds | 81,309,892 node-seconds |
| Utilization | 89.4812% | 89.4812% |
| Process runtime | 0.012205 s | 739.2877 s |

:::{figure} ../_static/scheduler-validation/slurm/bf-licenses-off/comparison-metrics.svg
:alt: Historical comparison showing identical license-off Slurm and no-reservation DR_EVT schedules
:width: 100%

Current-capacity dispatch without future reservations: zero differences.
:::

[PDF](../_static/scheduler-validation/slurm/bf-licenses-off/comparison-metrics.pdf)
· [Resource curves](../_static/scheduler-validation/slurm/bf-licenses-off/comparison.pdf)
· [Combined metrics](../_static/scheduler-validation/slurm/license-planning-metrics.csv)

At 13 seconds, protected DR_EVT stops at a 512-unit head job when only 504
units are free. License-off Slurm and the diagnostic continue to later fitting
jobs. At 798 seconds, batching two simultaneous one-unit completions lets an
earlier two-unit job start before later one-unit jobs consume the releases.

:::{figure} ../_static/scheduler-validation/slurm/divergence/gated-diagnosis.svg
:alt: Historical diagnosis of reservation policy and simultaneous-completion batching
:width: 100%

The diagnostic separates reservation policy from dispatch between completion
messages.
:::

[Diagnosis PDF](../_static/scheduler-validation/slurm/divergence/gated-diagnosis.pdf)
· [Plot data](../_static/scheduler-validation/slurm/divergence/gated-diagnosis-data.json)
· [Plot script](../_static/scheduler-validation/slurm/divergence/plot_gated_diagnosis.py)

## Adapter And Scheduler Order

The first gated license-off attempt accepted unchanged snapshots before main
dispatch ran and reset a subsecond clock counter at repeated timestamps. Its two
time-zero jobs started at one second, producing 972 downstream differences. The
corrected adapter preserves the counter and waits for a completed main-dispatch
acknowledgement after every event batch.

[Controlled startup replay](../_static/scheduler-validation/slurm/divergence/startup-dispatch/startup-diagnosis.json)

The remaining scheduler-order effect occurs at 23,460 seconds. Slurm starts
head job 469 in backfill, consuming one of Q=64 examination slots; job 614 is
then 65th and waits one tick. DR_EVT runs main dispatch first, removes job 469
from the queue, and examines job 614 in the same tick. Job 614's later Slurm
completion subsequently delays job 560 by 30 seconds. Charging job 469 one
backfill slot in the independent reference reproduces both differences.

[Controlled ordering data](../_static/scheduler-validation/slurm/divergence/gated-ordering/ordering-diagnosis.json)

## Reproduce

Follow the source and workload setup in `validation/slurm/README.md`, then run:

```sh
SLURM_LICENSE_RESULTS="$PWD/validation/slurm/results/licenses/reproduction" \
  bash validation/slurm/run.sh
```

The common timing methodology is documented on
[Simulation Validation](SCHEDULER_VALIDATION.md#processing-time-and-speedup).
Generated result folders contain the exact configurations, audits, plots,
source and binary hashes, and timing records used above.
