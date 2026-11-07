# Realism and Known Gaps

[Scheduling Loops and DR_EVT Configurations](SCHEDULER_LOOPS.md) explains the
supported policy combinations. This page records the model's scope and the
limits of relating those combinations to publicly documented production
approaches. A production scheduler may use a related method while differing
in triggers, accounting, admission, and placement. These comparisons do not
establish equivalent decisions or reproduce a production implementation.
Different defaults are configuration choices, not gaps.
Vendor options cited here establish the presence of a related control;
their detailed behavior must be distinguished from DR_EVT's policy semantics.

**Examination depth** limits counted waiting-job evaluations:
`--backfill_queue_depth` for backfill, or `--dispatch_queue_depth` for the
whole fixed-periodic placement prefix. **Reservation depth** separately
limits protected waiting jobs. A reservation can consume both counts;
per-pass versus per-timestamp counter resets are described below.

## Supported accounting policies

Default event-driven hybrid/conservative scheduling counts backfill
examinations and shares Q across calls at one simulation timestamp.
`--per_pass_accounting` selects whole-placement accounting: immediate starts
and successful reservations count toward Q, and each pass resets Q/R.
Reservation-only passes can repeat, with a tentative guard against unchanged
retries. These are supported policy choices; the
[per-pass loop](SCHEDULER_LOOPS.md#per-pass-placement-accounting) specifies
the failure-counting rules and the guard's assumptions.

## Remaining differences

### Trigger and evaluation scope

`--backfill_interval` delays backfill only. Fitting queue-head jobs still
start after state events and do not consume `--backfill_queue_depth`.

That split belongs to the trigger category illustrated by Slurm's
main-dispatch/backfill structure. Fixed periodic dispatch is available
separately through `--dispatch_interval`
and `--dispatch_queue_depth`: immediate starts and waiting candidates share
one examined prefix per tick. With four idle nodes and four admitted one-node
jobs, depth 2 starts at most two at one tick. The next tick can start the rest
without a new arrival or completion.

`--dispatch_on_events` adds immediate arrival-batch notifications to that
FCFS/backfill loop without changing the aligned timer phase. This policy set
shares the timer-plus-submission category illustrated by Spur, with these
DR_EVT rules: completions, releases, and
capacity changes update state but do not request a pass between ticks.
Administrative requeue, health-check recovery/submission, and burst-buffer
readiness notifications are outside the model. DR_EVT coalesces simultaneous
arrival/timer wakes into one pass, whereas Spur can consume them separately;
its timer also has an immediate initial tick. These wake details are simulation
abstractions, not claims of identical asynchronous execution.

Relating fixed periodic dispatch to LSF requires the assumption that
sessions occur at every aligned interval boundary while jobs wait, without
additional delay. LSF's minimum interval alone does not guarantee that cadence;
variable session timing remains unimplemented. The available IBM evidence does
not establish that LSF's job-decision limit equals DR_EVT's candidate count,
what failed checks or reservation attempts charge, or whether a separate scan
limit exists. It also does not establish a general reservation-depth or horizon
control, or whether reservations persist between sessions.

### Backfill evaluation accounting

With default event-driven accounting, hybrid and conservative share Q across
all scheduling passes at one timestamp. The separate EASY, SJF, LJF, and custom-selector
paths reset Q each pass. If one pass starts a job, another pass at the same
time can therefore receive a fresh backfill examination count. Use hybrid with reservation
depth 1 when head protection and cycle-wide accounting are both required.
Periodic EASY runs one scan per tick; periodic hybrid shares its count across
calls. They also differ with a finite horizon: EASY exempts an out-of-horizon
head, while periodic hybrid counts it.

In the separate `--backfill_interval` mode, the scheduling-state flag is cleared by an evaluation and
is not set just because backfill started a job. Consequently, a later tick
without an intervening state event does not necessarily examine candidates
exposed by that start. This is a limitation for reproducing a controller
that evaluates its new queue prefix every cycle. The fixed periodic
`--dispatch_interval` mode retries every tick and does not use this flag to
suppress evaluations.

### PBS timing and cycle limits

OpenPBS has both a candidate cap (`max_job_check`) and a computation-time
limit (`sched_cycle_length`). The default `max_job_check=ALL_JOBS` removes the
count cap, but the time limit can still truncate a cycle.

DR_EVT can approximate `sched_cycle_length` with queue depth Q equal to an
assumed evaluation throughput (candidates/second) times the cycle limit,
rounded down to at least one and capped by any explicit `max_job_check`.
This approximates candidate effort, including starts, failed checks, and future
planning, without modeling scheduler CPU time.

The [event-driven policy set related to OpenPBS](SCHEDULER_LOOPS.md#openpbs-event-driven-dispatch-and-backfill)
omits `scheduler_iteration` fallback cycles, assuming studied cycles are not
truncated by Q and time alone does not change eligibility. Modeling the fallback
is meaningful when a count cap or timeout leaves runnable candidates unexamined,
or when time alone changes eligibility: another cycle can change starts without
an arrival, resource release, failure, or inconsistent state. For example, with
four idle nodes, four runnable jobs, and Q=1, a fallback cycle can start a second
job before the first completes. If examination is effectively unlimited and
time alone changes nothing, the fallback repeats the same decisions.

Fixed periodic dispatch already retries with a fresh candidate count,
but PBS also schedules on arrivals and resource releases and resets its relative
fallback deadline when the server attempts scheduling requests. DR_EVT's
event-driven Q limits backfill examinations; fixed periodic dispatch counts all
candidates with aligned ticks and optional arrival wakes. Combining PBS's
event triggers, relative fallback, and full-candidate cap remains future work
for studies where these differences affect starts.

### LSF admission on reserved slots

The supplied IBM backfill documentation requires a job borrowing reserved
slots to finish before the protected job's predicted start. DR_EVT's default
resource-time rule can instead allow an overlapping execution if it preserves
the protected resource window. The build option
`DR_EVT_LEGACY_RESERVATION_BARRIER=ON` selects strict completion-before-start
admission and can be combined with EASY head protection in a homogeneous pool.
This policy set agrees with the decisions in IBM's worked example;
it does not reproduce LSF's queue-specific backfill eligibility and reserved
slot ownership. Agreement with that example does not
establish that LSF uses only EASY or always protects exactly one waiting job.
See the [LSF loop](SCHEDULER_LOOPS.md#lsf-scheduling-sessions-with-a-minimum-interval)
for the selected configuration. The legacy barrier applies throughout the
homogeneous pool and build; IBM's rule concerns borrowed reserved slots.

## Out of scope

The built-in model uses one homogeneous node pool with FCFS, SJF, or LJF
ordering. It does not model multifactor priority (age, fair share, QOS),
preemption, dependencies and arrays, per-user/account limits, partitions,
node topology and placement, or node failures. Scheduled capacity changes
alter the pool size; they do not model those mechanisms. Health checks, missed
notifications, state inconsistency, and failure recovery are outside the model;
timers used only for those purposes need no simulation equivalent.

PBS's explicit advance reservations (`pbs_rsub`), per-queue protection-depth
overrides, and prime/dedicated-time eligibility windows are also outside the
chosen global-pool mapping. Its waiting-job reservations use the existing
hybrid protection model.

The experimental [multi-system scheduler](multi-cluster-backfill.md)
shares a queue across several homogeneous systems. The external EASY selector
ranks feasible backfill candidates; it does not control preceding FCFS
dispatch or the whole queue. Studies requiring those excluded mechanisms
need an extension or another tool.

## Validation scope

The existing tests establish agreement with independent references and
fixtures for DR_EVT's policy sets. They do not establish production scheduler
equivalence. Previously
recorded validation includes:

| Model behavior | Recorded evidence |
|---|---|
| Event-driven policies | 34 scheduler-correctness workloads match the independent Python reference |
| Fixed periodic dispatch | Fixed ticks and timer/arrival wakes checked against hand-computed cases and independent randomized FCFS references; streaming, warm-start, and checkpoint continuation checks |
| Fixed periodic FCFS/EASY with strict reservation admission | Head-protection, boundary and runtime cases, and 96 randomized comparisons agree with an independent fixed-session reference; the default-rule overlap control differs as expected |
| Periodic backfill | 136 runs across those workloads, two reservation policies, and 7 s / 30 s intervals match the reference |
| Planning horizon | Horizon workloads and explicit-unlimited runs agree with the references; accounting is also covered by C++ and Python tests |
| Queue depth | EASY implementations agree with their reference; periodic EASY equals hybrid with depth 1 in the tested cases without a finite horizon |
| Node release delay | 34 workloads with constant cleanup delay match the reference |

The scripts are `tests/run_scheduler_correctness_tests.sh`,
`tests/run_periodic_backfill_tests.sh`, `tests/run_planning_horizon_tests.sh`,
`tests/run_backfill_queue_depth_tests.sh`, and
`tests/run_node_release_delay_tests.sh`. Warm-start and option-accounting
checks are in `tests/test_warm_start.cpp`, `tests/test_append_job_api.cpp`,
and `tests/test_python_hybrid_options.py`. The append-job, queue-depth, and
periodic-backfill suites were rerun after rebuilding the native targets
during this documentation check. The planning-horizon suite also passed using
a temporary Bash 3.2 compatibility copy with `set -u` disabled; the original
script fails on an empty-array expansion under that shell. These checks validate DR_EVT's model;
they are not direct comparisons with production scheduler executions.

The [RAPS comparison helper](../../scripts/compare_with_raps.py) is a separate,
manual check of legacy reservation-barrier behavior. It is not validation of
DR_EVT's default resource-time feasibility rule.

OpenPBS behavior was checked against pinned source and manuals, without a
paired OpenPBS/DR_EVT runtime comparison. Fixed periodic FCFS/EASY with
strict reservation admission was
validated in Docker with
[`run_legacy_reservation_validation.py`](../../tests/run_legacy_reservation_validation.py):
106 legacy-rule checks and one default-rule control passed. These establish
the selected homogeneous-pool model, not LSF's internal accounting or session
timing. No LSF daemon was run.

## Sources

Core loop and policy sources are listed in
[Scheduling Loops and DR_EVT Configurations](SCHEDULER_LOOPS.md#sources).
The following references support the differences and evidence limits above:

- **Slurm accounting:** at `17abb848e20054009d136a2c504a23e5462f26c8`,
  [initial horizon check](https://github.com/SchedMD/slurm/blob/17abb848e20054009d136a2c504a23e5462f26c8/src/plugins/sched/backfill/backfill.c#L3201),
  [test counter](https://github.com/SchedMD/slurm/blob/17abb848e20054009d136a2c504a23e5462f26c8/src/plugins/sched/backfill/backfill.c#L3359),
  and [post-test horizon check](https://github.com/SchedMD/slurm/blob/17abb848e20054009d136a2c504a23e5462f26c8/src/plugins/sched/backfill/backfill.c#L3836).
- **Flux core:** copied commit `d0309fcf0e672d90e65b0797168e56a430b9fb57`,
  [job-manager allocation requests](https://github.com/flux-framework/flux-core/blob/d0309fcf0e672d90e65b0797168e56a430b9fb57/src/modules/job-manager/alloc.c#L457),
  [Python scheduler request handling and queue-depth definition](https://github.com/flux-framework/flux-core/blob/d0309fcf0e672d90e65b0797168e56a430b9fb57/src/bindings/python/flux/scheduler.py#L237),
  and its separate [EASY backfill module](https://github.com/flux-framework/flux-core/blob/d0309fcf0e672d90e65b0797168e56a430b9fb57/src/modules/sched-backfill.py).
  These modules do not establish Fluxion's reservation-depth or scan-counter rules.
- **Spur notification scope:** at `693c0b32bf91a0965a63a263206e6e22a3e5a9e9`,
  `crates/spurctld/src/scheduler_loop.rs:3881` (late requeue) and
  `crates/spurctld/src/cluster.rs` notification sites at lines 1378, 2176,
  3448, 3577, and 5980.
- **OpenPBS timing and cycle limits:** at `cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a`,
  [fallback-deadline assignment](https://github.com/openpbs/openpbs/blob/cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a/src/server/pbsd_main.c#L1366),
  [timer-expiry request](https://github.com/openpbs/openpbs/blob/cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a/src/server/pbsd_main.c#L1598),
  and [main cycle and limits](https://github.com/openpbs/openpbs/blob/cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a/src/scheduler/fifo.cpp#L820).
  Additional inspected files: `src/server/svr_jobfunc.c` (new eligibility),
  `req_jobobit.c` (resource-release request), `run_sched.c` (busy handling),
  and `doc/man8/pbs_sched.8B`.
- **LSF evidence limits:** supplied IBM excerpts for
  `LSB_MAX_JOB_DISPATCH_PER_SESSION`, *Manage scheduling performance*, and
  *Enable fast job dispatch*, updated 2026-10-26. The parameter reference gives
  default 15000, with `JOB_DISPATCH_PACK_SIZE=0` selecting the CPU-based formula;
  the performance guide gives that formula without the same qualification.
  These excerpts do not establish a Fix Pack 9 transition. No LSF source was
  inspected. Reserved-slot admission is documented in IBM's
  [backfill guide](https://www.ibm.com/docs/en/SSWRJV_10.1.0/lsf_admin/backfill.html).
