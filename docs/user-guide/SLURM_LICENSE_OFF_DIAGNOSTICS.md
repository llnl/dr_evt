# Slurm license-off diagnostics

These experiments supplement the [license-on validation](SCHEDULER_VALIDATION.md).
Each job has a private dummy physical node and consumes a share of the same
756-resource license pool. With `bf_licenses=off`, Slurm enforces current
capacity but does not reserve future capacity in that pool. The private nodes
cannot protect an earlier job's planned start, so this is a diagnostic
control rather than ordinary Slurm backfill on shared compute nodes.

## Comparison with protected DR_EVT

### Lassen results with bf_licenses off versus protected DR_EVT

This **experimental control** keeps the same abstract resource pool but removes
protection of earlier jobs' planned starts. It does not measure ordinary Slurm
backfill on shared compute nodes.

This comparison uses the completed, acknowledged license-off native run
and the current protected DR_EVT reference. The native Slurm and adapter
binaries are identical to those in the current license-on run. All **2,000
jobs** and **6,779 native snapshots** pass the audits.
There are **1,943 differing start times**, with a largest absolute difference
of **110,851 seconds**. The area between curves is **12,260,066 node-seconds**,
or **11.0889%** of the shared capacity-time window.

| Metric | DR_EVT | Slurm, bf_licenses off |
|---|---:|---:|
| Mean waiting time | 10.3973 h | 3.3511 h |
| Makespan | 40.6236 h | 33.3878 h |
| Area under resource curve | 81,309,892 node-seconds | 81,309,892 node-seconds |
| Utilization | 73.5429% | 89.4812% |
| Maximum waiting time | 18.4183 h | 30.8072 h |

Disabling reservation of future shared capacity lets more jobs start early, reducing
aggregate waiting and makespan in this workload. It also permits later jobs
to delay earlier jobs: maximum waiting time increases. These results exercise
a different reservation policy for the pooled resource.

:::{figure} ../_static/scheduler-validation/slurm/bf-licenses-off/protected-comparison/comparison-metrics.svg
:alt: Six-panel comparison with bf_licenses disabled, showing lower mean wait and makespan, higher utilization, and a longer maximum wait than DR_EVT.
:width: 100%

Experimental control: shared capacity limits apply, but earlier jobs' planned starts are not protected.
:::

[Six-panel PDF](../_static/scheduler-validation/slurm/bf-licenses-off/protected-comparison/comparison-metrics.pdf)
This figure uses the acknowledged native run and current protected reference.

### Four-job scenarios: matching and different reservations

These examples explain the protected-DR_EVT comparison: license-on Slurm
protects the same planned starts, while license-off Slurm can delay them.
The additional diagnostic panels show the behavior without reservations.

Both scenarios use four node equivalents, FCFS order, simultaneous arrivals
at zero, and actual runtimes equal to time limits. A requests two nodes for
120 seconds, B two for 180 seconds, and D one for 120 seconds.

In the **original scenario**, C requests **three nodes for 60 seconds**.
Both Slurm settings and both DR_EVT references start A/B at zero, D at **120**, and C at
**180 seconds**. D can continue through C's reservation: their combined
requirement is four nodes. The original scenario therefore matches with
license planning either enabled or disabled.

:::{figure} ../_static/scheduler-validation/slurm/controls/four-job-node-assignments.svg
:alt: Original four-job workload with C requesting three nodes: Slurm license planning on, Slurm off, and DR_EVT all place D at 120 and C at 180 seconds.
:width: 100%

Original scenario: D and C coexist within the four-node capacity.
:::

The **reservation-protection scenario** changes only C to request **all four
nodes**, still for 60 seconds. With `bf_licenses` on, Slurm and protected DR_EVT protect
C's **180-second** start and postpone D until **240 seconds**. With planning
off, Slurm and diagnostic DR_EVT start D at **120 seconds**, consuming a license across C's planned
start and delaying C to **240 seconds**.

:::{figure} ../_static/scheduler-validation/slurm/controls/reservation-protection-node-assignments.svg
:alt: Changing C to four nodes makes Slurm with bf_licenses on match DR_EVT: C starts at 180 and D at 240; with planning off, D starts at 120 and delays C to 240.
:width: 100%

Changing C's size isolates the protection supplied by future license reservations.
:::

These Slurm scenarios illustrate **protection of an earlier job's planned start**;
the [Fluxion four-job example](SCHEDULER_VALIDATION.md#minimal-reproducer-for-backfill-start-time-differences) illustrates **binding reservations to specific
nodes**. The Slurm node rows are drawn feasible assignments to interchangeable
capacity slots, not recorded node bindings. Workload tables, scripts, and
plotting data accompany both figures.

## Lassen results with bf_licenses off: no-reservation DR_EVT reference

The diagnostic DR_EVT reference uses `--diagnostic_no_reservations`.
It scans eligible jobs in FCFS order and starts those that fit now, continuing
past jobs that cannot fit. It makes no future reservations. This is a separate
template specialization; the protected scheduler retains its behavior and
has no additional per-job mode check.

Both engines batch known same-time arrivals and completions before scheduling.
An arrival queues a job; it does not start it. The Slurm adapter uses `defer`
and `--batch-main-dispatch` to prevent dispatch between the individual RPCs.
Its gates do not force Slurm's main scheduler to run before backfill.

All **2,000 starts and completions match exactly**, with **zero area between
the resource curves**. All **6,779 native snapshots** pass the independent
audit, including the dispatch acknowledgements. Protected DR_EVT remains a
separate control and differs in **1,943 starts**.

| Metric | DR_EVT, no-reservation diagnostic | Slurm, bf_licenses off |
|---|---:|---:|
| Mean waiting time | 3.3511 h | 3.3511 h |
| Makespan | 33.3878 h | 33.3878 h |
| Resource area | 81,309,892 node-seconds | 81,309,892 node-seconds |
| Utilization | 89.4812% | 89.4812% |

:::{figure} ../_static/scheduler-validation/slurm/bf-licenses-off/comparison-metrics.svg
:alt: Six-panel comparison shows identical waits, utilization, makespan, resource area and start times for diagnostic DR_EVT and acknowledged license-off Slurm.
:width: 100%

Current-capacity dispatch without future reservations matches the gated native license-off run.
:::

[Six-panel PDF](../_static/scheduler-validation/slurm/bf-licenses-off/comparison-metrics.pdf)
· [Resource curves](../_static/scheduler-validation/slurm/bf-licenses-off/comparison.pdf)
· [Combined metrics](../_static/scheduler-validation/slurm/license-planning-metrics.csv).

The uninterrupted native controller/adapter run took **739.2877 seconds** in
the Linux container, including RPCs, observation, acknowledgements, and snapshot
writes. Five subsequent local macOS diagnostic runs had a median of
**0.055111 seconds** (range **0.050730–0.063799**), including process launch,
input, simulation, output and exit. These are different timing scopes and
environments; the recorded native duration is not scheduler computation alone.

This exact match is an observation for the recorded run. Main scheduling and
backfill were released together; their relative order was not enforced, and
DR_EVT does not reproduce every order Slurm can use. The match does not
establish agreement for other orderings or ordinary Slurm node binding.

## What the no-reservation reference tests

The original divergence combined a policy difference with an adapter timing
difference. Merely removing future reservations from backfill was insufficient:
main dispatch also had to continue past jobs that could not fit now.

At **13 seconds**, job 56 needs 512 resources but only 504 are free.
Protected DR_EVT stops main dispatch at that head job: jobs 57 and 58 wait
for the backfill tick at 30 seconds, and future protection holds job 59 until
68 seconds. License-off Slurm and the diagnostic start all three at 13 seconds. The four-job
example above separately shows why removing future reservations can delay
the head's planned start.

At **798 seconds**, jobs 135 and 180 each release one resource. In the original
adapter, dispatch between their completion messages lets one-resource jobs
243 and 245 consume the two releases separately. Earlier job 232 needs two
resources and waits until 847 seconds. Applying both completions before
dispatch lets job 232 start at 798 seconds. The adapter gate supplies that
same-time batch; it does not change Slurm's allocation rules.

:::{figure} ../_static/scheduler-validation/slurm/divergence/gated-diagnosis.svg
:alt: Full resource curves and job start differences compare protected and no-reservation DR_EVT with gated license-off Slurm; selected jobs illustrate current-capacity dispatch and simultaneous completion ordering.
:width: 100%

The diagnostic separates reservation policy from ordering between completion messages.
:::

[Diagnosis PDF](../_static/scheduler-validation/slurm/divergence/gated-diagnosis.pdf)
· [Plot data](../_static/scheduler-validation/slurm/divergence/gated-diagnosis-data.json)
· [Plotting script](../_static/scheduler-validation/slurm/divergence/plot_gated_diagnosis.py).

## Adapter acknowledgement: a missed initial dispatch

The first gated license-off rerun still had **972 differing starts**. Both
jobs arriving at zero started at **one second** in Slurm. A controlled replay
that delays only those two jobs reproduces **every native start and completion**;
the rest of that divergence follows from the missed initial dispatch.

The adapter had accepted several unchanged snapshots before main dispatch
ran. It also reset its subsecond counter when `TIME` repeated the same second,
allowing `gettimeofday()` to move backwards during startup. Slurm uses that
clock to decide whether another main pass may run, even with throttling off.
The corrected adapter preserves the counter for repeated timestamps and
waits for a completed main-dispatch acknowledgement after every arrival or
completion batch. The audit checks those acknowledgements and, with licenses
off, verifies that no fitting eligible job remains waiting.

[Controlled startup replay](../_static/scheduler-validation/slurm/divergence/startup-dispatch/startup-diagnosis.json).

## Gated license-on control: main dispatch versus backfill order

The current license-on rerun uses the corrected clock and completed-dispatch
acknowledgements and matches all 2,000 starts and completions. Its results are
on the [validation page](SCHEDULER_VALIDATION.md). The order of main scheduling
and backfill remains uncontrolled; an exact match in this run does not show
that DR_EVT reproduces every order Slurm can use.

An **earlier gated license-on run**, before those adapter corrections, had
**two jobs delayed by one 30-second tick** and **80 node-seconds** between
resource curves. Those observed differences do not bound possible divergence
under other orders. They came from Slurm running backfill before main
scheduling, whereas DR_EVT runs main scheduling first. This earlier run is
retained as diagnostic evidence rather than the current published comparison.

At **23,460 seconds**, Slurm starts fitting head job **469 in backfill**,
using one of its 64 examination slots. Job **614 is 65th** in the queue and
waits until **23,490**. DR_EVT runs main dispatch first: job 469 leaves the
queue before backfill begins, so job 614 fits within the 64 examined jobs and
starts at **23,460**. Its later completion in Slurm then moves job **560** from
**24,420 to 24,450**. No endpoint-rule change is involved.

The gate correctly prevents scheduling between RPCs in one event batch.
It does not reproduce DR_EVT's complete scheduling order: both native scheduler
triggers can proceed after release, whereas DR_EVT dispatches first. Therefore
this is a batching control, with a remaining main-versus-backfill order difference.

An independent reference reproduces protected DR_EVT's full schedule. Charging
job 469 one backfill slot at that single tick reproduces **every start and
completion** in the gated native run. This is a controlled explanation of
these two differences; it does not impose a universal order on Slurm.

[Earlier ordering-control PDF](https://github.com/LLNL/dr_evt/blob/main/validation/slurm/evidence/gated-lassen-20261108/on/comparison-metrics.pdf)
· [Controlled reference data](../_static/scheduler-validation/slurm/divergence/gated-ordering/ordering-diagnosis.json).

## Saved evidence and reproduction

The current archive is in
[validation/slurm/evidence/license-on-acknowledged-20261108](https://github.com/LLNL/dr_evt/tree/main/validation/slurm/evidence/license-on-acknowledged-20261108).
It contains the rerun license-on results, the reused acknowledged license-off
native record with refreshed DR_EVT references, independent audits, lossless
snapshot deltas, configurations, source/binary hashes, current plotting data
and scripts, and fresh local DR_EVT timings. Both comparisons use identical
native/adapter binaries and the same current simulator binary.

The [earlier diagnostic archive](https://github.com/LLNL/dr_evt/tree/main/validation/slurm/evidence/gated-lassen-20261108)
retains the startup and scheduler-order controlled replays and both four-job
scenarios. Earlier results there are historical evidence.

For either Lassen plot folder, run `python3 plot.py plot-input.json`.
For the four-job folder, run `python3 plot_timeline.py . --case pooled` or
`--case protection`. For the diagnosis, run
`python3 plot_gated_diagnosis.py gated-diagnosis-data.json`.
