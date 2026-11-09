# OpenPBS Simulation Validation

The grouped `fcfs_1s_evt_group` policy matches all 2,000 recorded job starts
and completions. Default `fcfs` differs for 1,433 jobs on the same input.

## Workload and configuration

The native OpenPBS 23.06.06 scheduler library was compared with DR_EVT using
private vnodes and a server-wide consumable `capacity` resource (`flag=q`).
Each job requests its own vnode and its original node count from a pool of
756 interchangeable units. Vnode identities do not compete; native calendar
checks protect future shared capacity. The scheduler library is unmodified.
An adapter supplies server snapshots, launch acknowledgements, actual
completions, and virtual time; no database or MOM daemons are used.

The comparison uses FCFS order, strict ordering, R=32, unlimited candidate
examination, and `opt_backfill_fuzzy=off`. It runs a native scheduling cycle
after each equal-time batch of arrivals and completions. DR_EVT uses
event-driven hybrid backfill with `--per_pass_accounting`. Computation-time
cycle limits, relative fallback triggers, deployment latency, and ordinary
node-bound placement are outside this experiment.

OpenPBS calendars use integer seconds. Relative arrivals, actual runtimes,
and requested walltimes are rounded upward; duration is at least one second,
and shorter limits are extended to duration. Both schedulers receive the same
2,000 Lassen jobs submitted from **2020-08-19 04:29:39 PDT** through
**2020-08-19 15:38:34 PDT**, converted as above, on an initially empty pool.

## Results

**All 2,000 starts and completions match native OpenPBS when DR_EVT uses
`--priority_policy fcfs_1s_evt_group`.** Default `fcfs` differs for 1,433 jobs.
Both comparisons use the same converted input and actual event timestamps.

| DR_EVT priority policy | Jobs with different starts or completions (out of 2,000) |
|---|---:|
| `fcfs_1s_evt_group` | 0 |
| `fcfs` (default) | 1,433 |

The grouped policy also matches all 21 small cases. Both policies pass the
capacity, ownership, duration, and resource-area audits. See the
[grouped-policy evidence](https://github.com/LLNL/dr_evt/blob/main/validation/openpbs/evidence/openpbs-policy-20261108/README.md)
and the [default-policy evidence](https://github.com/LLNL/dr_evt/blob/main/validation/openpbs/evidence/whole-seconds-20261108/README.md).

### One-second event grouping: all 2,000 jobs match

| Metric | DR_EVT | OpenPBS |
|---|---:|---:|
| Mean waiting time | 23,620.5925 s | 23,620.5925 s |
| Makespan | 143,232 s | 143,232 s |
| Area under resource curve | 81,309,892 capacity-unit seconds | 81,309,892 capacity-unit seconds |
| Utilization | 75.0899% | 75.0899% |
| Process runtime | 7.257180 s (median of 5 runs) | 325.310732 s |

::::{figure} ../_static/scheduler-validation/openpbs/grouped/resource-curves.svg
:alt: Native OpenPBS and DR_EVT fcfs_1s_evt_group have identical allocated-capacity curves for all 2,000 jobs

OpenPBS versus DR_EVT `fcfs_1s_evt_group`: identical resource use.
::::

::::{figure} ../_static/scheduler-validation/openpbs/grouped/comparison.svg
:alt: Six panels show identical waiting summaries, waiting distributions, utilization, makespan, and resource area, with zero waiting-time difference for all 2,000 jobs

OpenPBS versus DR_EVT `fcfs_1s_evt_group`: six-panel metrics comparison.
All 2,000 waiting-time differences are zero.
::::

[Six-panel PDF](../_static/scheduler-validation/openpbs/grouped/comparison.pdf)
· [plotting script](../_static/scheduler-validation/openpbs/grouped/plot.py)
· [plot data](../_static/scheduler-validation/openpbs/grouped/plot-data.json)
· [replay provenance](../_static/scheduler-validation/openpbs/grouped/provenance.json).

### Default FCFS comparison

With default `fcfs`, **1,433 job schedules differ**. Similar aggregate metrics
do not imply matching job starts.

| Metric | DR_EVT | OpenPBS |
|---|---:|---:|
| Mean waiting time | 23,287.413 s | 23,620.5925 s |
| Makespan | 143,569 s | 143,232 s |
| Area under resource curve | 81,309,892 capacity-unit seconds | 81,309,892 capacity-unit seconds |
| Utilization | 74.9136% | 75.0899% |
| Process runtime | 0.182249 s (median of 5 runs) | 315.908234 s |

::::{figure} ../_static/scheduler-validation/openpbs/default-fcfs/resource-curves.svg
:alt: Native OpenPBS and default DR_EVT fcfs resource-use curves for the original converted 2,000-job input

OpenPBS versus default DR_EVT `fcfs`: similar resource use with different starts.
::::

::::{figure} ../_static/scheduler-validation/openpbs/default-fcfs/comparison.svg
:alt: Six panels compare OpenPBS with default DR_EVT fcfs and show the waiting-time differences from their 1,433 differing job schedules

OpenPBS versus default DR_EVT `fcfs`: six-panel metrics comparison.
This uses the same input as the matching grouped-policy run above.
::::

[Default FCFS six-panel PDF](../_static/scheduler-validation/openpbs/default-fcfs/comparison.pdf)
· [plotting script](../_static/scheduler-validation/openpbs/default-fcfs/plot.py)
· [plot data](../_static/scheduler-validation/openpbs/default-fcfs/plot-data.json)
· [replay provenance](../_static/scheduler-validation/openpbs/default-fcfs/provenance.json).

### Execution time

DR_EVT times are medians of five runs using the same current Release binary
and common Linux Docker environment as the other DR_EVT comparisons. Every
schedule and resource trace matches the published result. Native OpenPBS times
remain single measurements from the recorded native replays. Both run in Docker
on an Apple M3 Max; the native adapter also writes event streams for auditing.

Timing covers process launch, input, simulation, output, and exit. Builds,
Python result parsing, audits, and plotting are excluded. These are execution
times, not simulated makespans.

[Common DR_EVT timing record](../_static/scheduler-validation/performance/performance.json)
· [Current Release build](../_static/scheduler-validation/performance/build.json)
· [Native default report](../_static/scheduler-validation/openpbs/default-fcfs/report.json)
· [Native grouped report](../_static/scheduler-validation/openpbs/grouped/report.json).
The DR_EVT timings inside the native reports describe earlier builds; the
comparison tables use the fresh common-environment measurements.

## Divergence analysis

### How a future prediction affects the current decision

Jobs are considered in arrival order. When an earlier job cannot start, a
later job may use spare capacity only if it will not delay the earlier job's
predicted start. This is backfill. R=32 protects up to 32 waiting jobs; the
four-job example below uses R=1.

The scheduler handles actual arrivals and completions at their timestamps.
During each decision, it also examines a hypothetical future using requested
time limits to predict releases. This calculation does not advance simulation
time or finish any actual jobs. Its predicted starts become capacity commitments
used to decide whether later jobs may start now.

For example, at time zero it can predict an earlier job's start at 10 and
reject a later candidate that would occupy needed capacity then. A candidate
must fit throughout its requested runtime. Capacity released at a time is
available for a job starting at that same time.

### The rule inside OpenPBS's future-start calculation

With `opt_backfill_fuzzy=off`, OpenPBS applies this rule **inside the hypothetical
future-start calculation**. Actual events remain at their original timestamps.

1. Check whether the waiting job can run in the current simulated state.
2. If it cannot, find the earliest remaining predicted event. Call its time T.
   A running job finishing and releasing capacity is one such event.
3. Form a group containing that event and every following predicted event
   whose time is at most **T + 1 second**, including events exactly at T + 1.
4. Apply all events in that group to the hypothetical state. There is no
   capacity-fit check between the events in the group.
5. When the group frees capacity, check whether the waiting job can run in the
   resulting state. The predicted start being checked is the time of the last
   event actually applied, not automatically T + 1.
6. If it still cannot run, form the next group from the earliest unprocessed
   event and repeat. Stop when a valid start is found or no events remain.

Here are two examples of grouping predicted completions:

| Predicted finish times in seconds | First group | First capacity-fit check |
|---|---|---:|
| 10, 11, 100 | Completions at 10 and 11 | At 11, after both completions |
| 10, 12, 100 | Completion at 10 only | At 10, after that completion |

In the first row, the group begins at 10 and includes 11. The state just after
10 is calculated, but the waiting job is not checked against that state.
In the second row, 12 is outside the group's endpoint of 11, so the first
check is at 10. If that state has enough capacity, the predicted start is 10.
**There is no automatic one-second delay:** T + 1 defines the group endpoint,
but the check uses the last event actually processed. The job finishing at
100 remains running at the first check in both rows. This rule groups nearby
events; it does not wait for all running jobs to finish. Events at 10, 11, 12,
and 13 would form groups {10, 11} and {12, 13}, with a check after each group
if the search needs to continue.

The native calendar can also contain planned starts and policy changes.
OpenPBS rechecks eligibility when a group releases resources or a relevant
policy change occurs. The examples here use completions to show how grouping
changes the capacity state presented to that check. In the native source,
[`simulate_events()`](https://github.com/openpbs/openpbs/blob/cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a/src/scheduler/simulate.cpp#L188-L219)
forms and applies the group, and
[`calc_run_time()`](https://github.com/openpbs/openpbs/blob/cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a/src/scheduler/simulate.cpp#L749-L773)
checks the waiting job after that function returns.

OpenPBS [documents event grouping](https://github.com/openpbs/openpbs/blob/cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a/doc/man1/pbs_sched_attributes.7B#L138-L157)
as a tradeoff between forecasting speed and the precision of predicted start
times: one fit check per group takes less work than a check after each event.
However, `off` is documented as "Finest granularity; no speedup." Its value is
zero, and the implementation replaces zero with one before calculating the
group's endpoint. For integer-second events, that endpoint becomes T + 1,
and the comparison includes events exactly at the endpoint. This is the
source of the remaining grouping under `off`; the rule above describes the
measured implementation rather than assuming `off` checks after every event.

The change was introduced by
[OpenPBS PR #2394, "Make opt_backfill_fuzzy less fuzzy"](https://github.com/openpbs/openpbs/pull/2394),
merged on May 26, 2021 as commit `7a2d95abb0f117ab628790baefb8aaa89a58c074`.
The author explains that grouping events speeds forecasting, but group
boundaries that shift between scheduling cycles can also make predicted
starts drift. The change aligned groups to clock boundaries to make those
predictions more stable. Before this change, `off` used an endpoint of T and
processed events at that timestamp only. The same patch changed `off` to use
one second, producing the T + 1 grouping described above. Neither the PR
description nor its discussion explains why that remaining grouping is needed
with `off`; the documented speed/stability rationale concerns fuzzy forecasting.

### A complete four-job example

There are **three interchangeable capacity slots**. All four jobs arrive at
second zero, in order A, B, H, S. Requested time limits equal actual runtimes
in this example, so predicted and actual completions agree for a given start.

| Job | Slots needed | Runtime and requested limit in seconds |
|---|---:|---:|
| A | 1 | 10 |
| B | 1 | 11 |
| H | 2 | 2 |
| S | 1 | 11 |

A and B start at zero and occupy two slots. One slot is free. H is next in
line, but it needs two slots and must wait. **The decision still being made at
time zero is whether S may use the spare slot without delaying H.** To make
that decision, each scheduler first predicts when H could start if S waits.
The hypothetical available capacity before allocating H is:

| Future time in seconds | Predicted completion | Free slots if S waits |
|---|---|---:|
| 0 | None yet; A and B are running | 1 |
| 10 | A finishes and releases one slot | 2 — enough for H |
| 11 | B finishes and releases one more slot | 3 |

Default DR_EVT `fcfs` checks after each distinct predicted release time. It checks the state
at second 10, finds two free slots, and protects H's start at 10. If S started
at zero, it would occupy a slot until 11, leaving only one free slot at 10.
That would delay H, so DR_EVT makes S wait. The actual starts are H at 10 and
S at 11; H uses two slots and S uses the third from 11 to 12.

OpenPBS finds A's predicted completion at 10 as the earliest future event.
Its group therefore runs through 11, so it includes B's predicted completion
at 11. Inside the hypothetical state, A's finish increases free slots from
one to two, and B's finish increases them from two to three. **Only then does
OpenPBS check H**, at second 11. It never checks H against the intermediate
state with two free slots at 10, and therefore protects H's start at 11.
S can run from zero to 11 without delaying that prediction: at 11, both B and
S release their slots before H starts. OpenPBS therefore allows S at zero.
Its actual starts are S at zero and H at 11.

The different forecasts change a decision at time zero. The actual scheduling
loop still handles A's completion at 10 and B's at 11 separately. In the
OpenPBS schedule, H cannot start at 10 because B and S still occupy two slots.
The forecast grouping has already allowed S to start, which is why the later
real completion at 10 does not undo the difference.

### The first different decision in the Lassen replay

The same mechanism appears in the larger replay. At **9,731 seconds**, both
schedulers have the same running jobs, waiting queue, and one free capacity
unit. They must decide which waiting job, if any, can use that unit now while
preserving their commitments to earlier waiting jobs.

Jobs 528 and 536 are predicted to finish at **13,006 seconds**; job 546 is
predicted to finish at **13,007 seconds**. These are hypothetical future
completions being examined during the current decision at 9,731.
Default `fcfs` checks capacity after the releases at 13,006 and predicts that waiting
job 229 can start then. OpenPBS includes the release at 13,007 in the same
forecast group, checks afterward, and predicts job 229's start at 13,007.
The other 31 protected start predictions agree at this decision.

An independent capacity calculation confirms the consequence. Starting job
548 now would leave too little capacity for job 229 at 13,006, but enough at
13,007. OpenPBS therefore starts job 548; DR_EVT rejects it and starts job 740
instead. The mismatch is in their future-start predictions and resulting
current choices. Actual completion times and outer-loop triggers are unchanged.

### Small controls

Seven hand-derived controls and twelve seeded 30-job comparisons cover
allowed/blocked backfill, early completion, simultaneous events, half-open
fit endpoints, and R=1/R=2/R=32 protection. Two four-job cases distinguish
predicted releases one second apart from releases farther apart. With
`fcfs_1s_evt_group`, all 21 cases match native OpenPBS; default `fcfs` has the
expected adjacent-release difference.

## Evidence and reproduction

### DR_EVT policy selection

OpenPBS 23.06.06 has no supported setting that removes this grouping:
`opt_backfill_fuzzy=off` and numeric zero both become a one-second interval in
`simulate_events()`. Changing the native implementation would be required to
check after every distinct event time.

DR_EVT provides a separate `fcfs_1s_evt_group` policy that reproduces the inclusive
one-second forecast groups. Select it explicitly; the default `fcfs` policy
continues to check each distinct predicted release time. For example:

```bash
simulator converted-jobs.csv --total_nodes 756 \
  --trace_format simple --timestamp_format epoch --run_time_mode actual \
  --priority_policy fcfs_1s_evt_group --queue_impl circular \
  --backfill_policy hybrid --reservation_depth 32 --backfill_queue_depth 0
```

The existing FCFS scheduling engine uses a separate forecast planner for this
policy. The planner applies the grouping rule above to reservations,
pending-job status predictions, and queue-drain forecasts.

Use event-driven dispatch, a circular queue, and unlimited planning horizon.
The native comparison uses integer-second converted inputs, a fixed shared
capacity pool, and unlimited candidate examination. Node placement, fairshare,
server/MOM execution, computation-time limits, and other OpenPBS policies are
outside this compatibility model. Python uses `PriorityPolicy.FCFS_1S_EVT_GROUP`;
Protobuf configuration and gRPC requests accept `priority_policy: "fcfs_1s_evt_group"`.

### Native validation replay

The [OpenPBS harness](https://github.com/LLNL/dr_evt/blob/main/validation/openpbs/README.md)
provides setup, clone commands, replay commands, adapter boundaries, and checks.
Run either comparison from the repository root after setup:

```sh
bash validation/openpbs/run.sh --priority_policy fcfs_1s_evt_group
bash validation/openpbs/run.sh --priority_policy fcfs
```

The [common timing folder](../_static/scheduler-validation/performance/README.txt)
provides the measurement script, benchmark inputs, current build details, and
Docker environment. The figure links above include scripts, data, and provenance.
With NumPy and Matplotlib installed, download a complete figure folder and run
`python3 plot.py report.json`. Recorded builds and source snapshots are in the
[grouped-policy evidence](https://github.com/LLNL/dr_evt/blob/main/validation/openpbs/evidence/openpbs-policy-20261108/README.md)
and [default-policy evidence](https://github.com/LLNL/dr_evt/blob/main/validation/openpbs/evidence/whole-seconds-20261108/README.md).
