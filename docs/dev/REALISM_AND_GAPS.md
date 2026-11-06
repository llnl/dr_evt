# Realism and Known Gaps

[Scheduling Loops and DR_EVT Configurations](SCHEDULER_LOOPS.md) explains the
core behavior and current option mappings. This page lists only the confirmed
scheduling differences that affect those mappings, plus the built-in model's
excluded scope. Different defaults are configuration choices, not gaps.

Here, **backfill evaluation budget** means the number of waiting jobs a cycle
may examine, controlled by `--backfill_queue_depth`. Reservation depth is a
separate limit on the number of protected waiting jobs.

## Remaining differences

### Trigger and evaluation scope

`--backfill_interval` delays backfill only. Fitting queue-head jobs still
start after state events and do not consume `--backfill_queue_depth`.

That split approximates Slurm's main-dispatch/backfill structure. It does
not reproduce Spur's single placement pass, where immediate starts and
future reservations share the candidate-prefix limit. For example, with
four idle nodes and four admitted one-node jobs, Spur's placement limit of
2 selects at most two in one real placement pass; DR_EVT's ordinary FCFS
pass can start all four even with backfill queue depth 2.

LSF's minimum interval applies to scheduling sessions. DR_EVT has no option
to delay both dispatch and backfill until the next permitted session.
For example, after an LSF session at 0 s with a 5-second minimum interval,
a fitting arrival at 2.3 s must wait for a permitted session; DR_EVT can
start the head at 2.3 s. The available IBM evidence does not establish that
LSF's job-decision limit equals DR_EVT's queue depth.

These differences matter when the study depends on whole-scheduler timing
or an evaluation limit covering immediate starts. Changing only the backfill interval
or depth cannot remove them.

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

The current periodic scheduling-state flag is cleared by an evaluation and
is not set just because backfill started a job. Consequently, a later tick
without an intervening state event does not necessarily examine candidates
exposed by that start. This is a limitation for reproducing a controller
that evaluates its new queue prefix every cycle.

## Out of scope

The built-in model uses one homogeneous node pool with FCFS, SJF, or LJF
ordering. It does not model multifactor priority (age, fair share, QOS),
preemption, dependencies and arrays, per-user/account limits, partitions,
node topology and placement, or node failures. Scheduled capacity changes
alter the pool size; they do not model those mechanisms.

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

## Sources

Pinned production-source references and the current DR_EVT implementation
files are listed in [Scheduling Loops and DR_EVT Configurations](SCHEDULER_LOOPS.md#sources).
