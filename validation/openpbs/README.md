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

Put the user-provided `lassen-756-01.csv` from
[LLNL LAST](https://github.com/LLNL/LAST) in `lassen_traces/`, then run:

```sh
bash validation/openpbs/run.sh
```

To compare the separate DR_EVT policy that reproduces OpenPBS's forecast
grouping, run:

```sh
OPENPBS_VALIDATION_DR_PRIORITY=fcfs_1s_evt_group bash validation/openpbs/run.sh
```

The default comparison still uses the original `fcfs` policy and records its
timing differences. With `fcfs_1s_evt_group`, every small case and both Lassen
replays require exact native agreement as well as passing capacity audits.

The image downloads build dependencies. Native source is exported from its
commit into a Docker volume; the source checkout is mounted read-only.
DR_EVT is built from a snapshot of tracked working files in Release mode with
`DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`. The native library is built from the
committed OpenPBS source without scheduling patches.

`OPENPBS_VALIDATION_RESULTS` sets the results directory;
`OPENPBS_VALIDATION_BUILD_VOLUME` selects the reusable build volume;
`OPENPBS_VALIDATION_MAX_JOBS` sets the Lassen prefix size (default 2,000;
0 selects the complete file). The trace is compared both at its converted
timing and after multiplying every arrival time, actual runtime, and requested
time limit by two. Job order and capacity requests stay the same. This second
replay tests whether OpenPBS's treatment of releases one second apart explains
the differences in the first replay. Each native
replay has a 30-minute timeout.
Small controls can be run directly in the prepared container by omitting
`--trace` from `compare.py`.
`OPENPBS_VALIDATION_DR_PRIORITY` selects `fcfs` (default) or `fcfs_1s_evt_group`;
direct comparisons use `compare.py --dr-priority fcfs_1s_evt_group`.

## Resource and policy model

Every job requests one CPU on its own private vnode and its original node count
as the job-wide `capacity` resource. Private vnode identities never compete.
The meaningful constraint is a pool of 756 interchangeable units. Vnodes are
registered with arrival and retired on completion; each can serve only its
associated job. This reduces the snapshot size without changing available
capacity or any other job's placement choices.

The equivalent native administrative settings are:

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
[complete capacity example and native source references](https://github.com/LLNL/dr_evt/blob/main/docs/user-guide/SCHEDULER_VALIDATION.md#openpbs-with-server-wide-capacity).

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

The recorded 2,000-job primary Lassen replay has **1,433 differing schedules**,
with utilization **75.09%** for OpenPBS and **74.91%** for DR_EVT. The first
divergence comes from different predictions for waiting job 229's earliest
start: DR_EVT predicts 13,006 seconds; OpenPBS predicts 13,007 because it skips
the capacity check between releases at those times. That changes which job
may run ahead of job 229. **All 2,000 schedules match when every time is
multiplied by two**, putting distinct releases at least two seconds apart.
Both replays pass the capacity and ownership audits. The
[recorded evidence](evidence/whole-seconds-20261108/README.md) includes both
results and the four-job reproducer.

The runner saves native and DR_EVT build manifests and hashes, adapter sources,
exact commands and configurations, converted inputs, schedules, native
forecast/launch/completion events, and reports. Raw traces and generated results
are ignored. Compact recorded evidence is stored under `evidence/`.

Seven hand-derived controls cover allowed/blocked backfill, early completion,
simultaneous events, half-open endpoints, and protection depth. Twelve seeded
30-job workloads broaden coverage. Two additional four-job controls assert
the adjacent-release divergence and the match when times are doubled. The
Lassen comparisons use R=32. The primary replay records measured differences;
the replay with every time multiplied by two requires an exact match. The
runner also requires all capacity/ownership audits and the expected small-case
outcomes to pass.

`fcfs_1s_evt_group` selects a distinct DR_EVT scheduler with the grouping rule
explained above. It reuses FCFS placement passes while keeping its forecast
events separate from exact capacity intervals. Pending-job status predictions
and checkpoint continuation follow the selected policy. It requires a circular
queue, event-driven dispatch, and unlimited planning horizon. Its native
agreement claims apply to integer-second shared-capacity inputs, not all
OpenPBS scheduling policies or production node placement.
Its [recorded native replay](evidence/openpbs-policy-20261108/README.md)
matches all 21 small cases and every start and completion in both 2,000-job
inputs. All capacity and ownership audits pass. The original input needs no
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
runs all 21 small comparisons, and runs both versions of the selected Lassen
prefix. For a shorter build-and-replay check, run:

```sh
OPENPBS_VALIDATION_MAX_JOBS=50 bash validation/openpbs/run.sh
```

Native replays run separately from ordinary GitHub CI because they require
the pinned external OpenPBS checkout and the supplied Lassen trace. A passing
native run with the default `fcfs` policy requires all audits, exact matches in 20 small cases, the specified
four-job difference, and exact agreement for the Lassen input with all times
multiplied by two. The original Lassen replay records differences rather than
requiring equality.
With `OPENPBS_VALIDATION_DR_PRIORITY=fcfs_1s_evt_group`, all 21 small cases and
both Lassen inputs must match exactly.

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
