# Realism and Known Gaps

[Scheduling Loops and DR_EVT Configurations](SCHEDULER_LOOPS.md) explains the
supported policy combinations. This page records the model's scope and the
limits of relating those combinations to publicly documented production
approaches. Production schedulers can differ in triggers, accounting,
admission, and placement. Similar controls do not guarantee identical decisions.
Different defaults
alone are configuration choices, not model gaps. Workload-specific evidence is
in [Scheduling Validation](SCHEDULER_VALIDATION.md).

**Examination depth Q** limits counted waiting-job evaluations:
`--backfill_queue_depth` for backfill, or `--dispatch_queue_depth` for the
whole fixed-periodic placement prefix. **Reservation depth R** separately
limits protected waiting jobs. A reservation can consume both counts;
per-pass versus per-timestamp counter resets are described below.

## Supported accounting policies

Default event-driven hybrid/conservative scheduling counts backfill
examinations and shares Q across calls at one simulation timestamp.
`--per_pass_accounting` selects whole-placement accounting: immediate starts
and successful reservations count toward Q, and each pass resets Q/R.
Reservation-only passes can repeat; a tentative guard stops unchanged retries.
The [per-pass loop](SCHEDULER_LOOPS.md#per-pass-placement-accounting) defines
failure counting and the guard's assumptions.

## Remaining differences

### Trigger and evaluation scope

`--backfill_interval` delays backfill only. Fitting queue-head jobs still
start after state events and do not consume `--backfill_queue_depth`.

This split represents Slurm's main-dispatch/backfill structure. Fixed periodic
dispatch instead uses `--dispatch_interval` and `--dispatch_queue_depth`:
immediate starts and waiting candidates share one examined prefix per tick.
With four idle nodes and four runnable one-node jobs, Q=2 starts at most two
per tick; the next tick can start the rest without a state event.

`--dispatch_on_events` adds arrival-batch wakes without changing the timer phase,
representing Spur's timer-plus-submission loop. Completions, releases, and
capacity changes update DR_EVT state but do not request passes between ticks.
Administrative requeue, health-check recovery/submission, and burst-buffer
readiness notifications are outside the model. DR_EVT coalesces simultaneous
arrival/timer wakes into one pass, whereas Spur can consume them separately;
its timer also has an immediate initial tick. DR_EVT does not reproduce this
asynchronous wake ordering.

Representing LSF with fixed periodic dispatch assumes sessions occur at every
aligned interval boundary while jobs wait. LSF's minimum interval does not
guarantee that cadence; variable session timing is unimplemented. The IBM
evidence does not establish how its job-decision limit counts failed checks or
reservations, whether a separate scan limit exists, or whether reservations
persist between sessions. General reservation-depth and horizon controls are
also unverified.

### Backfill evaluation accounting

With default event-driven accounting, hybrid and conservative share Q across
all scheduling passes at one timestamp. The separate EASY, SJF, LJF, and custom-selector
paths reset Q each pass. If one pass starts a job, another pass at the same
time can therefore receive a fresh backfill examination count. Use hybrid with reservation
depth 1 when head protection and cycle-wide accounting are both required.
Periodic EASY runs one scan per tick; periodic hybrid shares its count across
calls. They also differ with a finite horizon: EASY exempts an out-of-horizon
head, while periodic hybrid counts it.

Separate periodic backfill evaluates a tick only when scheduling state has
changed. Arrivals, releases, capacity changes, and job starts set that flag;
starts can expose new candidates in the bounded prefix. Fixed periodic
dispatch retries every tick while jobs wait, regardless of the flag.

Periodic hybrid/conservative backfill also checks capacity at each candidate's
end timestamp, representing Slurm's endpoint rule. Event-driven and fixed
periodic placement retain half-open fits, as needed for Fluxion and Spur.
The [endpoint explanation](SCHEDULER_LOOPS.md#shared-terms) defines both rules;
the [Slurm validation](SCHEDULER_VALIDATION.md#sources-of-divergence-and-corrections)
measures their effect.

### PBS timing and cycle limits

OpenPBS has both a candidate cap (`max_job_check`) and a computation-time
limit (`sched_cycle_length`). The default `max_job_check=ALL_JOBS` removes the
count cap, but the time limit can still truncate a cycle.

DR_EVT can approximate `sched_cycle_length` with queue depth Q equal to an
assumed evaluation throughput (candidates/second) times the cycle limit,
rounded down to at least one and capped by any explicit `max_job_check`.
This estimates candidate effort, including starts, failed checks, and future
planning, without simulating scheduler CPU time.

The [event-driven policy set related to OpenPBS](SCHEDULER_LOOPS.md#openpbs-event-driven-dispatch-and-backfill)
omits `scheduler_iteration` fallback cycles. This assumes cycles are not
truncated by Q and time alone does not change eligibility. Otherwise a fallback
can change starts without a state event: with four idle nodes, four runnable
jobs, and Q=1, it can start another job before the first completes. With
unlimited examination and unchanged eligibility, it repeats the same decisions.

PBS schedules on arrivals and resource releases and resets its relative
fallback deadline when the server attempts scheduling requests. DR_EVT's
event-driven Q limits backfill examinations; fixed periodic dispatch counts
all candidates but uses aligned ticks and optional arrival wakes. Combining
PBS's event triggers, relative fallback, and full-candidate cap remains future
work.

### LSF admission on reserved slots

The cited IBM backfill guide requires a job borrowing reserved
slots to finish before the protected job's predicted start. DR_EVT's default
resource-time rule can instead allow an overlapping execution if it preserves
the protected resource window. The build option
`DR_EVT_LEGACY_RESERVATION_BARRIER=ON` selects strict completion-before-start
admission and can be combined with EASY head protection in a homogeneous pool.
Queue-specific eligibility and reserved-slot ownership remain unmodeled.
The guide does not establish a universal EASY policy.
See the [LSF loop](SCHEDULER_LOOPS.md#lsf-scheduling-sessions-with-a-minimum-interval)
for the configuration. The legacy barrier applies throughout the homogeneous
pool; IBM's rule concerns borrowed reserved slots.

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

## Sources

Core loop and policy sources are listed in
[Scheduling Loops and DR_EVT Configurations](SCHEDULER_LOOPS.md#sources).
The following references support the differences and evidence limits above:

- **Slurm accounting:** at `17abb848e20054009d136a2c504a23e5462f26c8`,
  [initial horizon check](https://github.com/SchedMD/slurm/blob/17abb848e20054009d136a2c504a23e5462f26c8/src/plugins/sched/backfill/backfill.c#L3201),
  [test counter](https://github.com/SchedMD/slurm/blob/17abb848e20054009d136a2c504a23e5462f26c8/src/plugins/sched/backfill/backfill.c#L3359),
  and [post-test horizon check](https://github.com/SchedMD/slurm/blob/17abb848e20054009d136a2c504a23e5462f26c8/src/plugins/sched/backfill/backfill.c#L3836).
- **Flux core:** inspected source revision `d0309fcf0e672d90e65b0797168e56a430b9fb57`,
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
