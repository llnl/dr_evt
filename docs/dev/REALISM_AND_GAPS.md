# Realism and Known Gaps

DR_EVT models the core behavior of a batch scheduler, not a production
scheduler: when scheduling decisions are evaluated, which reservations
backfill protects, how many jobs hold reservations, what the scheduler knows
about run times, and when a finished job's nodes become reusable. This page
states how those parts of the model relate to Slurm, Flux (Fluxion), and IBM
Spectrum LSF, which option reproduces each production setting, and which
differences remain and in which direction they bias results. The policies
themselves are defined in [Scheduling Policies](../BACKFILLING_ALGORITHMS.md),
the execution pipeline in
[Simulation Pipeline and Job Lifecycle](JOB_LIFECYCLE.md), and every option
in [Command-Line Options](../user-guide/command-line.md).

Everything outside that core is out of scope by design: multifactor priority
(age, fair share, QOS), preemption, job dependencies and arrays, per-user and
per-account limits, partitions, node topology and placement, and node
failures. Studies that depend on those mechanisms need another tool or the
experimental hooks listed at the end.

## The model

Each item describes the code on this branch.

- **Evaluation trigger.** By default a scheduling pass runs after every state
  change: a batch of completions, a batch of arrivals, or a capacity change.
  All events sharing a timestamp are applied first, then the pass repeats
  until no further job can start. With
  [`--backfill_interval`](../user-guide/command-line.md#--backfill_interval-seconds)
  FCFS dispatch of the queue head stays event-driven while backfill runs only
  at periodic ticks. Ticks start one interval after the first event (or after
  the warm-start boundary) and keep their cadence; a tick that passes while
  nothing is waiting does nothing.
- **Reservations.** Under EASY the head's reservation is the earliest time
  enough nodes become free, projected from running-job time limits and future
  scheduled-capacity changes; a candidate starts only if it fits now and its
  projected completion is strictly before that reservation. Conservative
  protects every earlier waiting job, hybrid the first
  [`--reservation_depth`](../user-guide/command-line.md#--reservation_depth-n)
  of them, selected with
  [`--backfill_policy`](../user-guide/command-line.md#-b---backfill_policy-policy).
  Reservations are recomputed on every pass rather than carried over, as Slurm
  does; jobs started earlier in the same pass are included when later
  candidates are tested. FCFS implementations use the strict boundary above;
  SJF and LJF retain their historical inclusive boundary at an exactly equal
  projected completion time.
- **Scan and planning bounds.**
  [`--backfill_queue_depth`](../user-guide/command-line.md#--backfill_queue_depth-n)
  caps how many waiting jobs one pass inspects (default unlimited), under
  every policy and ordering, including the custom EASY selector.
  [`--planning_horizon`](../user-guide/command-line.md#--planning_horizon-seconds)
  drops reservations that would start beyond the horizon in event-driven and
  periodic mode. Such a job consumes neither a queue-depth nor a reservation
  slot, remains queued, and is reconsidered on the next pass. Under EASY
  only the head holds a reservation, so only the head can be exempted. This is a
  practical approximation of external resource-status notifications that
  DR_EVT does not simulate.
  [`--planning_time_resolution`](../user-guide/command-line.md#--planning_time_resolution-seconds)
  rounds each projected release up to a grid once its start is known; the
  clock and actual execution stay exact.
- **Run-time knowledge.** The scheduler plans with the requested time limit.
  The observed run time comes from the trace or from a sampled distribution
  ([run-time options](../user-guide/command-line.md#simulation-mode-options))
  and never exceeds the limit, which is equivalent to killing a job at its
  limit. An early completion triggers a new pass but does not revise earlier
  decisions.
- **Node reuse.** By default a finished job's nodes are reusable at once.
  [`--node_release_delay`](../user-guide/command-line.md#--node_release_delay-seconds)
  holds them for a sampled cleanup time
  ([constant, exponential, or uniform per node](../user-guide/command-line.md#--node_release_delay_model-model),
  a job released when its slowest node is), while planning still projects
  releases at the time limit, as production schedulers do. Replayed history is
  never delayed.
- **Priority and resources.** FCFS by submission time, or SJF/LJF by estimated
  run time ([`--priority_policy`](../user-guide/command-line.md#-p---priority_policy-policy)).
  One pool of identical nodes
  ([`--total_nodes`](../user-guide/command-line.md#-n---total_nodes-count)),
  optionally varied over time with
  [`--capacity_schedule`](../user-guide/command-line.md#--capacity_schedule-filename);
  reservations merge the scheduled changes ahead with running-job releases,
  so a maintenance reduction is visible to backfill as it is in production.
  The experimental [multi-system scheduler](../user-guide/multi-cluster-backfill.md)
  shares one FCFS queue across several homogeneous systems.

## Production schedulers

The four named LLNL clusters are El Capitan and Tuolumne (Flux), and Sierra
and Lassen (LSF); Slurm runs on most other LC clusters.

### Slurm

Slurm's main scheduler "runs in a limited fashion every time that any event
happens which could enable a job to start (e.g. job submit, job terminate,
etc.)", rate-limited by `sched_min_interval`, plus a full pass every
`sched_interval`; it considers jobs in strict priority order and "once any
job or job array task in a partition is left pending, no other jobs in that
partition will be scheduled". It does not backfill. The backfill plugin
(`SchedulerType=sched/backfill`, the default) runs periodically and rebuilds
its plan each iteration ([`slurm.conf(5)`](https://slurm.schedmd.com/slurm.conf.html),
[Scheduling Configuration Guide](https://slurm.schedmd.com/sched_config.html)):

| Parameter | Default | Meaning (from `slurm.conf(5)`) |
|---|---|---|
| `bf_interval` | 30 s | "The number of seconds between backfill iterations." |
| `bf_window` | 1440 min | "The number of minutes into the future to look when considering jobs to schedule." |
| `bf_resolution` | 60 s | "The number of seconds in the resolution of data maintained about when jobs begin and end." |
| `bf_max_job_test` | 500 (100 in older releases) | "The maximum number of jobs to attempt backfill scheduling for (i.e. the queue depth)." |
| `bf_job_part_count_reserve` | 0 | Highest-priority jobs per partition that receive reservations; 0 plans every tested job that cannot start. |
| `bf_max_time` | `bf_interval` | Wall time one backfill iteration may spend. |
| `default_queue_depth` | 100 | Jobs the main scheduler tries after a completion or other routine event. |
| `sched_interval` | 60 s | Period of the main scheduler's full pass. |

DR_EVT's event-driven head dispatch corresponds to the main scheduler; its
periodic backfill corresponds to the plugin.

### Flux (Fluxion)

Fluxion is event-driven like DR_EVT: a scheduling-relevant event triggers a
fresh reservation plan
([`queue_policy_bf_base_impl.hpp` at 3b49d19](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/qmanager/policies/queue_policy_bf_base_impl.hpp)).
It examines at most `queue-depth` jobs per cycle and yields after six jobs,
plans in integer seconds, and places jobs on a resource graph.

Fluxion keeps a blocked list for jobs whose allocation-or-reservation attempt
fails, including requests affected by down resources. The
[`MATCH_ALLOCATE_ORELSE_RESERVE`](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/resource/traversers/dfu.cpp#L106-L139)
path searches the planner first and then makes a satisfiability check. On
[`EBUSY`](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/qmanager/policies/queue_policy_bf_base_impl.hpp#L120-L141),
the backfill policy moves a reservation candidate to `m_blocked` and removes
it from `m_scheduled_cnt`; [`reconsider_blocked_jobs()`](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/qmanager/policies/base/queue_policy_base.hpp#L188-L194)
marks it for reconsideration. The base policy also requests that
reconsideration after completion and reprioritization events, so this is not
limited to a DR_EVT-style planning horizon.

| Setting | Default or policy-fixed behavior | Meaning |
|---|---|---|
| `queue-policy` | `fcfs`, `easy`, `conservative`, `hybrid` | Reservation depth 0, 1, all, or N. |
| `queue-params.queue-depth` | 32 | Pending jobs considered per cycle. |
| `policy-params.reservation-depth` | 64 under `hybrid` | Reserved higher-priority jobs per cycle; effectively capped by the queue depth. |

### IBM Spectrum LSF

LSF schedules in periodic sessions of the `mbschd` daemon; LSF 10.1 defaults
to `JOB_SCHEDULING_INTERVAL=5 45` in `lsb.params`, so a session that finishes
early waits until five seconds have elapsed. Each session is a full pass over
the pending queue. Backfilling requires a run limit on the candidate and works
against slot reservations held for the highest-priority pending jobs
(`SLOT_RESERVE`, `MAX_RESERVE_TIME`). When a finishing job's slots become
reusable depends on post-execution processing. The reservation and
post-execution statements are summarized from the IBM documentation and
should be checked against the installed version.

## Reproducing a production configuration

| Target | `--backfill_policy` | `--backfill_interval` | `--reservation_depth` | `--backfill_queue_depth` | `--planning_time_resolution` | `--planning_horizon` |
|---|---|---:|---:|---:|---:|---:|
| Slurm, reserve all tested jobs | `conservative` | `bf_interval` (30) | N/A | `bf_max_job_test` (500) | `bf_resolution` (60) | `bf_window` (86400) |
| Slurm, reserve a fixed prefix | `hybrid` | `bf_interval` | `bf_job_part_count_reserve` | `bf_max_job_test` | `bf_resolution` | `bf_window` |
| Slurm, head reservation only | `hybrid` | `bf_interval` | `1` | `bf_max_job_test` | `bf_resolution` | `bf_window` |
| Fluxion hybrid | `hybrid` | `0` | `reservation-depth` | `queue-depth` (32) | `1` | N/A; DR_EVT retests rather than receiving blocked-list notifications |

`--backfill_policy easy` equals `hybrid` with depth 1. These settings
reproduce trigger and reservation behavior in the single-pool FCFS model;
Slurm applies them per partition. Add `--node_release_delay` calibrated to
the target system's cleanup time where node reuse matters.

### Out-of-horizon accounting

| Planner | Job outside its current planning/allocatable scope | Reservation slot | Re-examination |
|---|---|---|---|
| Slurm `bf_window` | Counts toward `bf_max_job_test` | No reservation slot | Retested every pass |
| Fluxion blocked list | Not counted (`--m_scheduled_cnt`) | No reservation slot | Reconsidered on Fluxion status, completion, or priority triggers |
| DR_EVT `--planning_horizon` | Does not count toward `--backfill_queue_depth` | No reservation slot | Retested every pass |

DR_EVT leaves the job in its normal queue and recomputes its reservation each
pass. This captures observable releases and scheduled capacity changes without
modeling Fluxion's external resource-status notification path.

## Remaining differences

- **Backfill queue-depth accounting.** Conservative and hybrid
  `--backfill_queue_depth` scans are shared across all backfill work at one
  simulation timestamp and reset only when time advances. Other scheduler
  paths reset their count for each invocation. The reservation-based policies
  therefore model one Slurm-style backfill cycle while retaining event-driven
  FCFS dispatch.

| Aspect | DR_EVT | Difference and bias |
|---|---|---|
| Evaluation trigger | Event-driven by default | Slurm and LSF backfill periodically. Default DR_EVT backfill reacts sooner, so waits of backfillable jobs are optimistic. `--backfill_interval` removes this. |
| Planning grid and horizon | Exact and unlimited by default | Slurm grids planned times and bounds the window; Fluxion plans in whole seconds and uses blocked-list notifications. DR_EVT can match the grid and bound the planning window, but retests out-of-horizon jobs rather than modeling those notifications. |
| Scan and reservation depth | Unlimited scan by default | Slurm and Fluxion cap both. Matching depths removes the difference. |
| Node reuse | Immediate by default | Production nodes return after cleanup. `--node_release_delay` removes the optimism only to the degree it is calibrated. |
| Eligible time | Submission time | Held or dependent jobs in a real trace become eligible later than they were submitted. Their simulated wait is counted from submission, which overstates it relative to the production record. |
| Warm start | Inherited waiting queue | Jobs waiting before `--sim_start_time` are reconstructed by default; historical reservation state is not, so the first conservative or hybrid backfill decision can differ. `--warm_start_wait_queue drop` retains the legacy omission behavior. |
| Resources and priority | One homogeneous pool, FCFS/SJF/LJF | Partitions, topology, and priority plugins are out of scope. |

With default settings several rows can make DR_EVT backfilling more optimistic
than a production scheduler. Matching interval, grid, horizon, and depths
narrows those differences; the horizon re-examination approximation remains a
separate behavioral distinction.

## Validation evidence

- **Event-driven policies.** The 34 scheduler-correctness workloads match a
  from-scratch Python reference exactly in schedule and resource trace
  (`tests/run_scheduler_correctness_tests.sh`). This is a consistency check
  between two implementations of the same model, not a comparison with a
  production scheduler.
- **Periodic backfill.** The same workloads under hybrid (depth 1) and
  conservative backfill at 7 s and 30 s ticks match the Python reference in
  schedule and resource trace, 136 runs
  (`tests/run_periodic_backfill_tests.sh`), and warm start under periodic
  backfill matches a cold run seeded with the boundary state
  (`tests/test_warm_start.cpp`).
- **Planning horizon.** EASY, hybrid, conservative, and SJF event-driven
  cases are checked against the Python references, including a depth-one case
  where an out-of-horizon head does not consume the candidate scan slot;
  explicit unlimited horizon remains byte-identical on all 34 correctness
  workloads (`tests/run_planning_horizon_tests.sh`).
- **Queue depth under EASY.** With a queue depth, with and without a planning
  horizon, the deque, circular, block, and multimap FCFS schedulers and the
  EASY mode of both conservative schedulers match the Python EASY reference;
  a depth of 1 equals no backfilling for FCFS, SJF, and LJF
  (`tests/run_backfill_queue_depth_tests.sh`).
- **Node release delay.** The 34 workloads with a constant delay match the
  Python reference (`tests/run_node_release_delay_tests.sh`).
- **RAPS.** The [comparison helper](../../scripts/compare_with_raps.py) joins
  RAPS and DR_EVT start times for one job list. It is a manual tool, and RAPS
  is not an oracle for reservation-preserving backfill: at RAPS commit
  [3f21cd3](https://github.com/ExaDigiT/RAPS/tree/3f21cd35b7bf6a8f5decc41050c8d871809608ad)
  three verified differences separate the two.
  1. RAPS advances one second per iteration and detects a completion on the
     iteration after the true end (`engine.py`, `prepare_timestep` and
     `complete_timestep`), so dependent starts and ends shift one second
     late and the lag compounds.
  2. In [`find_backfill_job`](https://github.com/ExaDigiT/RAPS/blob/3f21cd35b7bf6a8f5decc41050c8d871809608ad/raps/schedulers/default.py#L150-L198)
     running jobs are sorted by `time_limit` (line 167) and the shadow is the
     `start_time + time_limit` of the last job needed (lines 172–177), not
     the earliest projected end. The error is two-sided.
  3. RAPS admits a candidate when `time_limit <= shadow - now` (line 202);
     DR_EVT requires the projected end strictly before the shadow.

  On the CTC SP2 archive trace the first two effects became visible from
  roughly the 200th job on in a manual run against that commit.

## Calibration and extensions

- **Node release delay.** Calibration needs a measurement of the real cleanup
  time. Flux records it per job as `t_inactive - t_cleanup`; a replay trace
  bounds it from above by the gap before the freed nodes are next needed.
- **Planning horizon.** Set it to the target's configured window (for example,
  Slurm `bf_window`); its re-examination semantics are described above.
- **Priority hooks.** The experimental external selector ranks backfill
  candidates by a caller-supplied cost; extending the same callback to queue
  ordering would allow priority experiments without changing the built-in
  policies.

## Sources

- Slurm: `slurm.conf(5)` (SchedulerType, SchedulerParameters) and the
  Scheduling Configuration Guide, read from the `master` branch of the
  SchedMD repository on 2026-10-30; pinned source:
  [`_attempt_backfill`](https://github.com/SchedMD/slurm/blob/slurm-25-11-8-1/src/plugins/sched/backfill/backfill.c#L2126-L2150),
  [`job_test_count` before the window test](https://github.com/SchedMD/slurm/blob/slurm-25-11-8-1/src/plugins/sched/backfill/backfill.c#L2610-L2618),
  and the [`backfill_window` comparison](https://github.com/SchedMD/slurm/blob/slurm-25-11-8-1/src/plugins/sched/backfill/backfill.c#L3536-L3544).
- Flux: flux-sched at commit
  [3b49d19](https://github.com/flux-framework/flux-sched/tree/3b49d1998ed53836618e54188f45b52bc1aac811),
  `qmanager/policies/queue_policy_bf_base_impl.hpp`, the `easy`, `hybrid`,
  and `conservative` policy constructors,
  `qmanager/config/queue_system_defaults.hpp`,
  `qmanager/modules/qmanager_callbacks.cpp`, and
  `doc/man5/flux-config-sched-fluxion-qmanager.rst`; pinned source:
  [`resource_match.cpp`](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/resource/modules/resource_match.cpp),
  [`dfu.cpp`](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/resource/traversers/dfu.cpp#L106-L139),
  [`allocate_orelse_reserve_jobs()` EBUSY branch](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/qmanager/policies/queue_policy_bf_base_impl.hpp#L120-L141),
  and [`reconsider_blocked_jobs()`](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/qmanager/policies/base/queue_policy_base.hpp#L188-L194).
- LSF: IBM Spectrum LSF 10.1 documentation, `lsb.params`
  `JOB_SCHEDULING_INTERVAL` (read 2026-10-30); the slot-reservation and
  post-execution statements are summarized from the `lsb.queues` reference.
- RAPS: [ExaDigiT/RAPS at commit
  `3f21cd35`](https://github.com/ExaDigiT/RAPS/tree/3f21cd35b7bf6a8f5decc41050c8d871809608ad),
  `raps/schedulers/default.py` (`find_backfill_job`, lines 150–198,
  `return_first_fit`, line 202) and `raps/engine.py` (`prepare_timestep`,
  `complete_timestep`, `run_simulation`).
- CTC SP2 trace: the [CTC SP2 log](https://www.cs.huji.ac.il/labs/parallel/workload/l_ctc_sp2/)
  of the Parallel Workloads Archive (D. G. Feitelson, D. Tsafrir, and
  D. Krakov, "Experience with using the Parallel Workloads Archive," Journal
  of Parallel and Distributed Computing 74(10), 2014). The trace is not
  shipped with this repository.
- DR_EVT: `src/sim/sim.cpp` (`advance_to_impl`), `src/sim/scheduler_*.cpp`,
  and `src/sim/scheduler_base.cpp` on this branch.
