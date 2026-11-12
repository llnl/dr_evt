# OpenPBS shared-capacity validation

This harness compares DR_EVT with the **unmodified native OpenPBS scheduler
library**, using private vnodes and a server-wide consumable resource. It checks
job starts/ends, exclusive ownership of each private vnode, capacity bounds,
resource curves, and integrated capacity use. It does not run `pbs_server`, its
database, or compute-node MOM daemons.

## Run

Install Git and Docker, and start Docker. From the DR_EVT repository root,
create the input directories and obtain the inspected native revision:

```sh
mkdir -p external lassen_traces
git clone https://github.com/openpbs/openpbs.git external/openpbs
git -C external/openpbs checkout --detach cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a
```

Prepare the local extraction using the exact submission-time interval in the
[parent page's workload description](../../docs/user-guide/SCHEDULER_VALIDATION.md#lassen-workload),
and save it as `lassen_traces/lassen-756-01.csv`, then run:

```sh
bash validation/openpbs/run.sh
```

The validation defaults to `fcfs_1s_evt_group`, which reproduces the tested
OpenPBS forecast rule. Every small case and the original converted Lassen
input must match exactly and pass all capacity audits. Select either policy
using DR_EVT's existing option:

```sh
bash validation/openpbs/run.sh --priority_policy fcfs_1s_evt_group
bash validation/openpbs/run.sh --priority_policy fcfs
```

The second command compares ordinary `fcfs` and records its timing differences.
The wrapper passes `--priority_policy` through to the DR_EVT invocation.

The image downloads build dependencies. Native source is exported from its
commit into a Docker volume; the source checkout is mounted read-only.
DR_EVT is built from a snapshot of tracked working files in Release mode with
`DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`. The native library is built from the
committed OpenPBS source without scheduling patches.

`OPENPBS_VALIDATION_RESULTS` sets the results directory;
`OPENPBS_VALIDATION_BUILD_VOLUME` selects the reusable build volume;
`OPENPBS_VALIDATION_MAX_JOBS` sets the Lassen prefix size (default 2,000;
0 selects the complete file). Both schedulers receive the same original
converted input. Each native replay has a 30-minute timeout.
Small controls can be run directly in the prepared container by omitting
`--trace` from `compare.py`. The comparator also accepts `--priority_policy`.

## Resource and policy model

Every job requests one CPU on its own private vnode and its original node count
as the job-wide `capacity` resource. Private vnode identities never compete.
The meaningful constraint is a pool of 756 interchangeable units. Vnodes are
registered with arrival and retired on completion; each can serve only its
associated job. This reduces the snapshot size without changing available
capacity or any other job's placement choices.

The default campaign now writes separate `capacity/` and `placement/` result
trees. The capacity tree refreshes the historical shared-resource comparison.
The placement tree gives OpenPBS a shared pool of 756 exclusive one-CPU vnodes,
gives each job a `select=N:ncpus=1` request, and compares native vnode IDs with
DR_EVT `--resource_model placement` allocations. Both trees record native and
DR_EVT process times from the same campaign host.

The equivalent native administrative settings for capacity mode are:

```text
qmgr: create resource capacity type=long,flag=q
qmgr: set server resources_available.capacity=756
qmgr: set server backfill_depth=32
qmgr: set sched default opt_backfill_fuzzy=off
sched_config: strict_ordering: true ALL
sched_config: resources: "ncpus,mem,arch,host,vnode,capacity"
```

A job uses `select=1:ncpus=1:vnode=private<ID>` and requests `capacity=N`
outside the select statement. The adapter supplies the corresponding native
status attributes. Standard resource types and flags are generated from
OpenPBS's own `master_resc_def_all.xml`; only `capacity` is site-defined.
The future-start rule for `opt_backfill_fuzzy=off` is explained below.

Jobs have stable FCFS queue ranks, one execution queue, no fairshare,
preemption, dependencies, administrative reservations, or node-release delay.
Candidate examination is unlimited (`max_job_check=ALL_JOBS`). The adapter
calls native `scheduling_cycle()` once after each equal-time batch of arrivals
and actual completions. It freezes time during a cycle, so computation-time
cycle truncation is outside the experiment. Fallback-only cycles are omitted;
under these unbounded, unchanged-eligibility conditions they would repeat the
same decisions.

DR_EVT uses event-driven hybrid backfill, the corresponding reservation depth,
`--per_pass_accounting`, and enough candidate depth to examine the whole
workload in a pass. Protection-depth checks cover both R=1 and R=2, and the
randomized cases also cover R=32.

## How future predictions affect a current decision

Jobs are considered in arrival order. When an earlier job cannot start, a
later job may use spare capacity only if it will not delay the earlier job's
predicted start. This is backfill. The scheduler must therefore calculate a
future start for the waiting job before deciding whether a later job can run
now. Requested time limits, not actual runtimes, determine predicted releases.
A later job must fit throughout its requested runtime without violating these
future capacity commitments. A completion at a time releases capacity for a
start at that same time.

The **outer loop** handles actual arrivals and completions at their exact
timestamps. The adapter processes all actual events with the same timestamp
before calling the scheduler; events at 10 and 11 cause separate calls.
The **future-start calculation** takes place inside one such call. It examines
a hypothetical future from the current running state while actual replay time
stays fixed. Its event grouping changes predictions, not the outer-loop event
timestamps.

Under the tested `opt_backfill_fuzzy=off` setting, the future-start calculation
first checks whether the waiting job can run in the current simulated state.
If it cannot, it takes the earliest remaining predicted event at time T and
forms a group containing all predicted events through **T + 1 second**,
including the endpoint. It applies that whole group without checking the
waiting job between events. When the group releases capacity, it checks the
resulting state at the time of the **last event actually processed**. If the
job still cannot run, it repeats with the next group. The calendar can also
contain planned starts and policy changes; native eligibility checks account
for relevant policy changes as well as released capacity.

| Predicted completion times | First group | First time checked |
|---|---|---:|
| 10, 11, 100 | 10 and 11 | 11 |
| 10, 12, 100 | 10 only | 10 |

The job finishing at 100 remains running at the first check in both examples.
If there is no other event through T + 1, only the event at T is processed
and the check is at T. **There is no automatic one-second start delay.**
The grouping rule combines nearby hypothetical events; it neither waits for
all running jobs to finish nor rounds actual scheduling-loop triggers.

OpenPBS documents this grouping as a speed/precision tradeoff. Its `off`
setting has value zero, but `simulate_events()` replaces zero with one before
calculating the inclusive group endpoint. `calc_run_time()` checks whether the
waiting job fits after the group returns. This explains the remaining grouping
with `off`. See the
[complete capacity example and native source references](https://github.com/LLNL/dr_evt/blob/main/docs/user-guide/OPENPBS_VALIDATION.md).

## Adapter boundary

The linker wraps native server query APIs to return snapshots, the asynchronous
launch API to record assignments, and attribute updates to record forecasts.
It also supplies `time()` and reports no pending server interruption command.
Native query conversion, ordering, eligibility checks, vnode selection,
future-start searches, reservation insertion, and server-resource calendar
checks run inside the unchanged OpenPBS library. Launches must use the job's
private vnode and cannot exceed the shared pool.

Actual completion times come from the supplied runtimes. OpenPBS estimates
future release using requested walltime; early completion causes a new cycle.
This is scheduler-library validation with emulated server I/O and execution,
not validation of server/MOM RPCs, real launch latency, or production node
placement.

OpenPBS calendar times are integer seconds. Lassen arrivals relative to the
first arrival, durations, and requested limits are rounded upward. Duration is
at least one second, and any shorter limit is extended to duration. Both
schedulers receive exactly the same converted workload. The conversion and
raw source hash are recorded; the comparison does not claim preservation of
the original fractional-second trace.

## Evidence

With `fcfs_1s_evt_group`, **all 2,000 starts and completions match native
OpenPBS on the original converted Lassen input**. Both schedulers have mean
wait 23,620.5925 seconds, makespan 143,232 seconds, and utilization 75.0899%.
All 21 small cases match and all capacity/ownership audits pass. See the
[grouped-policy evidence](evidence/openpbs-policy-20261108/README.md).

With ordinary `fcfs`, the same input has **1,433 differing schedules**. The
first divergence comes from different predictions for waiting job 229:
DR_EVT predicts 13,006 seconds; OpenPBS predicts 13,007 because it groups
predicted releases at those times before checking capacity. This changes
which later job may run ahead of job 229. The
[default-policy evidence](evidence/whole-seconds-20261108/README.md) preserves
that run and the four-job reproducer.

The [OpenPBS validation page](../../docs/user-guide/OPENPBS_VALIDATION.md)
shows separate six-panel comparisons and resource-use curves for both policies.
The normal runner uses the original converted input; it does not scale its
arrival times, runtimes, or limits to obtain a match.

The runner saves native and DR_EVT build manifests and hashes, adapter sources,
exact commands and configurations, converted inputs, schedules, native
forecast/launch/completion events, and reports. Raw traces and generated results
are ignored. Compact recorded evidence is stored under `evidence/`.

Seven hand-derived controls cover allowed/blocked backfill, early completion,
simultaneous events, half-open endpoints, and protection depth. Twelve seeded
30-job workloads broaden coverage. Two additional four-job cases compare
adjacent predicted releases with releases farther apart. The Lassen replay
uses R=32. With `fcfs_1s_evt_group` all cases must match; with `fcfs`, the
adjacent-release difference is expected and the Lassen differences are recorded.

`fcfs_1s_evt_group` selects a distinct DR_EVT scheduler with the grouping rule
explained above. It reuses FCFS placement passes while keeping its forecast
events separate from exact capacity intervals. Pending-job status predictions
and checkpoint continuation follow the selected policy. It requires a circular
queue, event-driven dispatch, and unlimited planning horizon. Its native
agreement claims apply to integer-second shared-capacity inputs, not all
OpenPBS scheduling policies or production node placement.
Its [recorded native replay](evidence/openpbs-policy-20261108/README.md)
matches all 21 small cases and every start and completion in the original
converted 2,000-job input. All capacity and ownership audits pass. The original input needs no
extra time transformation when this policy is selected.

Historical replay evidence records the earlier name `fcfs_openpbs`. The
current CLI, Python enum, Protobuf configuration, and gRPC requests use
`fcfs_1s_evt_group` (`PriorityPolicy.FCFS_1S_EVT_GROUP` in Python).

Pending-job and queue-drain queries select a forecast implementation once
per query. `if constexpr` selects the planner and query lambdas at compile
time, so their job loops contain no checks for the grouping policy. Ordinary
queries construct no grouping event list.

## Tests, formatting, documentation, and CI

Run the five Python audit tests from the repository root, without Docker,
OpenPBS, Matplotlib, or a DR_EVT build:

```sh
python3 -m unittest discover -s validation/openpbs -p test_compare.py -v
```

They check capacity overflow, starts before arrival, incorrect duration,
stable arrival order and rounding during trace conversion, and the candidate's
capacity fit for the two different start predictions in the four-job example.
Quick CI and both compiler jobs in full CI run this command. These tests check
the harness; they do not execute the native scheduler.

`bash validation/openpbs/run.sh` builds and executes the native scheduler,
runs all 21 small comparisons, and runs the original converted Lassen
prefix with the selected policy. For a shorter build-and-replay check, run:

```sh
OPENPBS_VALIDATION_MAX_JOBS=50 bash validation/openpbs/run.sh
```

Native replays run separately from ordinary GitHub CI because they require
the pinned external OpenPBS checkout and the supplied Lassen trace. A passing
native run with `fcfs_1s_evt_group` requires exact agreement in all 21 small
cases and the original converted Lassen input. Selecting `fcfs` requires all
audits, exact matches in 20 small cases, and the specified adjacent-release
difference; it records the Lassen divergence without requiring equality.

The adapter uses clang-format 18's LLVM style. Format or check it with:

```sh
clang-format-18 --style=LLVM -i validation/openpbs/reference.cpp
clang-format-18 --style=LLVM --dry-run --Werror validation/openpbs/reference.cpp
```

Its Doxygen comments describe input fields, server wrappers, ownership,
launch checks, and the replay loop. Generate its reference from the repository
root with:

```sh
mkdir -p validation/openpbs/results
doxygen validation/openpbs/Doxyfile
```

HTML is written to `validation/openpbs/results/doxygen/html/`. The documentation
CI job checks this configuration with warnings treated as errors, alongside
the existing Sphinx and production API documentation builds. Historical source
snapshots under `evidence/` remain unchanged to preserve the recorded build hashes.

`diagnose.py` reconstructs the half-open capacity plan at the first differing
start from the common running state, requested walltimes, FCFS queue and
protection depth. It compares that plan with native forecasts from the same
replay and checks the differing borrower's fit against both plans.

A passing result applies to these inputs and conditions; shared-resource
agreement does not establish equivalence for ordinary node-bound scheduling
or all OpenPBS policies. Recorded wall-clock replay times include snapshot
construction and emulated server I/O; they are not scheduler performance
benchmarks.

To plot a saved report:

```sh
python3 validation/openpbs/plot.py path/to/report.json
```
