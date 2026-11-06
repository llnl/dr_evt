# Realism and Known Gaps

[Scheduling Loops and DR_EVT Configurations](SCHEDULER_LOOPS.md) explains the
core behavior and current option mappings. This page lists only the confirmed
scheduling differences that affect those mappings, plus the built-in model's
excluded scope. Different defaults are configuration choices, not gaps.

Here, **backfill evaluation budget** means the number of waiting jobs a cycle
may examine: `--backfill_queue_depth` for separate backfill, or
`--dispatch_queue_depth` for the whole unified placement prefix. Reservation depth is a
separate limit on the number of protected waiting jobs.

## Remaining differences

### Trigger and evaluation scope

`--backfill_interval` delays backfill only. Fitting queue-head jobs still
start after state events and do not consume `--backfill_queue_depth`.

That split approximates Slurm's main-dispatch/backfill structure. Unified
fixed-periodic dispatch is available separately through `--dispatch_interval`
and `--dispatch_queue_depth`: immediate starts and waiting candidates share
one examined prefix per tick. With four idle nodes and four admitted one-node
jobs, depth 2 starts at most two at one tick. The next tick can start the rest
without a new arrival or completion.

`--dispatch_on_events` adds immediate arrival-batch notifications to that
unified loop without changing the aligned timer phase. This represents Spur's
ordinary-submission and periodic-polling paths: completions, releases, and
capacity changes update state but do not request a pass between ticks.
Administrative requeue, health-check recovery/submission, and burst-buffer
readiness notifications are outside the model. DR_EVT coalesces simultaneous
arrival/timer wakes into one pass, whereas Spur can consume them separately;
its timer also has an immediate initial tick. These wake details are simulation
abstractions, not claims of identical asynchronous execution.

For LSF, fixed periodic dispatch represents timing under the assumption that
sessions occur at every aligned interval boundary while jobs wait, without
additional delay. LSF's minimum interval alone does not guarantee that cadence;
variable session timing remains unimplemented. The available IBM evidence does
not establish that LSF's job-decision limit equals DR_EVT's candidate count,
what failed checks or reservation attempts charge, or whether a separate scan
limit exists. It also does not establish a general reservation-depth or horizon
control, or whether reservations persist between sessions.

### Backfill evaluation accounting

In event-driven mode, hybrid and conservative share Q across all scheduling
passes at one timestamp. The separate EASY, SJF, LJF, and custom-selector
paths reset Q each pass. If one pass starts a job, another pass at the same
time can therefore receive another full backfill evaluation budget. Use hybrid with reservation
depth 1 when head protection and cycle-wide accounting are both required.
Periodic EASY runs one scan per tick; periodic hybrid shares its count across
calls. They also differ with a finite horizon: EASY exempts an out-of-horizon
head, while periodic hybrid counts it.

Equal numeric depths also need not mean equal candidates across engines:
Slurm charges Q after preliminary checks; Spur caps an admitted candidate
prefix; Fluxion refunds allocation-or-reservation `EBUSY` failures. DR_EVT
counts its backfill examinations. In particular, periodic hybrid/conservative
charges out-of-horizon jobs, while Slurm charges them only if they reached
placement testing. Event-driven hybrid/conservative exempts them but leaves them in the queue, rather
than reproducing Fluxion's blocked-list lifecycle. These distinctions matter
for finite Q when skipped jobs occur ahead of runnable candidates.

These Fluxion comparisons apply to `flux-sched`. The inspected `flux-core`
modules (`sched-simple`, `sched-fifo`, and `sched-backfill`) instead use
`queue-depth` for outstanding allocation requests, so their depth cannot be
substituted for Fluxion's examination cap.

In the separate `--backfill_interval` mode, the scheduling-state flag is cleared by an evaluation and
is not set just because backfill started a job. Consequently, a later tick
without an intervening state event does not necessarily examine candidates
exposed by that start. This is a limitation for reproducing a controller
that evaluates its new queue prefix every cycle. The unified
`--dispatch_interval` mode retries every tick and does not use this flag to
suppress evaluations.

### PBS timing and cycle limits

DR_EVT can approximate `sched_cycle_length` with queue depth Q equal to an
assumed evaluation throughput (candidates/second) times the cycle limit,
rounded down to at least one and capped by any explicit `max_job_check`.
This approximates candidate effort, including starts, failed checks, and future
planning, without modeling scheduler CPU time.

The [event-driven PBS approximation](SCHEDULER_LOOPS.md#openpbs-unified-event-driven-scheduling)
omits `scheduler_iteration` fallback cycles, assuming studied cycles are not
truncated by Q and time alone does not change eligibility. Independently of
failure recovery, PBS's fallback can retry candidates left by a truncated cycle;
its deadline moves with scheduling requests. DR_EVT's event-driven Q limits
backfill examinations, while unified dispatch counts all candidates with an
assumed aligned cadence; neither reproduces both PBS semantics together.

### LSF admission on reserved slots

The supplied IBM backfill documentation requires a job borrowing reserved
slots to finish before the protected job's predicted start. DR_EVT's default
resource-time rule can instead allow an overlapping execution if it preserves
the protected resource window. The build option
`DR_EVT_LEGACY_RESERVATION_BARRIER=ON` selects strict completion-before-start
admission and is the chosen setting for the documented simplified LSF model.
This provides a homogeneous-pool approximation with head protection;
it does not reproduce LSF's queue-specific backfill eligibility and reserved
slot ownership. EASY can mimic the example in IBM's documentation, but it does not
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

The experimental [multi-system scheduler](../user-guide/multi-cluster-backfill.md)
shares a queue across several homogeneous systems. The external EASY selector
ranks feasible backfill candidates; it does not control preceding FCFS
dispatch or the whole queue. Studies requiring those excluded mechanisms
need an extension or another tool.

## Validation scope

The existing tests establish agreement with independent implementations of
DR_EVT's model, not complete fidelity to a production scheduler. Previously
recorded validation includes:

| Model behavior | Recorded evidence |
|---|---|
| Event-driven policies | 34 scheduler-correctness workloads match the independent Python reference |
| Unified dispatch | Fixed ticks and timer/arrival wakes checked against hand-computed cases and independent randomized FCFS references; streaming, warm-start, and checkpoint continuation checks |
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
paired OpenPBS/DR_EVT runtime comparison. The unified-dispatch checks used the
default resource-time rule; the selected strict LSF configuration needs
separate validation.

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
