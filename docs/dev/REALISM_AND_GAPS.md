# Realism and Known Gaps

DR_EVT models the core behavior of a batch scheduler: when scheduling
decisions are evaluated, which reservations backfill protects, how many jobs
hold reservations, how far down the queue backfill looks, how far ahead
reservations are planned, what the scheduler knows about run times, and when a
finished job's nodes become reusable. This page
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

- **Reservations and backfill policies.** Waiting jobs are ordered by
  submission time (FCFS), or by estimated run time under SJF and LJF
  ([`--priority_policy`](../user-guide/command-line.md#-p---priority_policy-policy)).
  The job at the front, the head, starts as soon as enough nodes are free;
  this is FCFS dispatch.
  When the head does not fit, nodes would otherwise sit idle until it does.
  Backfilling lets a later job start out of order if it fits now and does not
  delay the jobs that hold reservations. A reservation is the earliest time a
  waiting job is projected to start, computed from the running jobs' time
  limits and any scheduled capacity changes. The backfill policy, set with
  [`--backfill_policy`](../user-guide/command-line.md#-b---backfill_policy-policy),
  decides how many jobs hold reservations:
  - EASY reserves for the head only.
  - Conservative reserves for every waiting job.
  - Hybrid reserves for the first
    [`--reservation_depth`](../user-guide/command-line.md#--reservation_depth-n)
    waiting jobs. Depth 1 equals EASY and an unlimited depth equals
    conservative.

  A backfill candidate starts only if its projected completion is strictly
  before the earliest reservation it must protect. Reservations are recomputed
  on every pass rather than carried over, as Slurm does, and jobs started
  earlier in the same pass are included when later candidates are tested. SJF
  and LJF keep their historical inclusive test, which also admits a candidate
  that completes exactly at the reservation.
- **When the scheduler runs.** There are two modes.
  - *Event-driven*, the default, as in Fluxion. Three kinds of events change
    scheduling state: a job completes, a job arrives, or scheduled capacity
    changes. All events at the same timestamp form one batch: they are applied
    together, and only then does the scheduler run. It repeats scheduling
    passes until a pass starts no job; each pass does FCFS dispatch and then
    backfills. These passes together are the backfill cycle for that
    timestamp.
  - *Periodic backfill*, with
    [`--backfill_interval`](../user-guide/command-line.md#--backfill_interval-seconds),
    as in Slurm. FCFS dispatch stays event-driven: after each batch the head
    starts as soon as it fits, and so does each following job that fits in
    turn. A job holding a reservation therefore starts as soon as
    its turn comes and it fits, without waiting for a tick. Backfill runs only
    at periodic ticks, and one tick is one backfill cycle. Ticks start one interval after the first
    event (or after the warm-start boundary) and keep their cadence; a tick
    that passes while nothing is waiting does nothing. Periodic EASY runs as
    hybrid with reservation depth 1.
- **Queue depth.**
  [`--backfill_queue_depth`](../user-guide/command-line.md#--backfill_queue_depth-n)
  caps how many waiting jobs, the head included, one backfill cycle examines
  (default unlimited), under every policy and ordering, including the custom
  EASY selector. A job counts when it is examined, whether or not it fits. In
  periodic mode the cycle is one tick: all backfill passes at the tick share
  the count, it restarts at the next tick, and FCFS dispatch between ticks
  uses none of it. In event-driven mode the cycle is all the passes at one
  timestamp for conservative and hybrid; the separate EASY implementations
  differ, as described under [Remaining differences](#remaining-differences).
- **Planning horizon.**
  [`--planning_horizon`](../user-guide/command-line.md#--planning_horizon-seconds)
  stops planning reservations that would start more than the horizon after the
  current time, in both modes, as Slurm's `bf_window` does. Such a job holds no
  reservation and uses no reservation-depth slot. It keeps its queue position
  and is re-evaluated on the next pass, so released nodes or a capacity change
  can bring it back within the horizon. Whether it uses a queue-depth slot
  follows the system each mode reproduces: it does in periodic mode, as in
  Slurm, and does not in event-driven mode, as for a job Fluxion blocks (see
  [Out-of-horizon accounting](#out-of-horizon-accounting)).
- **Planning time resolution.**
  [`--planning_time_resolution`](../user-guide/command-line.md#--planning_time_resolution-seconds)
  rounds each projected release up to a grid once its start is known, as
  Slurm's `bf_resolution` does; the clock and actual execution stay exact.
- **Run-time knowledge.** The scheduler plans with the requested time limit.
  The observed run time comes from the trace or from a sampled distribution
  ([run-time options](../user-guide/command-line.md#simulation-mode-options))
  and never exceeds the limit, which is equivalent to killing a job at its
  limit. A job that ends before its limit frees its nodes at that moment and
  the scheduler runs again, so waiting jobs can start earlier than their
  reservations. Jobs that already started keep their start times.
- **Node reuse.** By default a finished job's nodes are reusable at once.
  [`--node_release_delay`](../user-guide/command-line.md#--node_release_delay-seconds)
  holds them for a sampled cleanup time
  ([constant, exponential, or uniform per node](../user-guide/command-line.md#--node_release_delay_model-model),
  a job released when its slowest node is), while planning still projects
  releases at the time limit, as production schedulers do. Replayed history is
  never delayed.
- **Resources.** One pool of identical nodes
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

Slurm schedules with two components. The main scheduler starts jobs in
strict priority order and does not backfill. It runs after any event that
could let a job start, such as a submission or a completion, but then
considers at most `default_queue_depth` jobs, and it runs no more often than
`sched_min_interval` allows. It also runs a full pass over the queue every
`sched_interval`. In each partition it stops at the first job that cannot
start, so no lower-priority job in that partition starts ahead of it. In the
documentation's words, it "runs in a limited fashion every time that any
event happens which could enable a job to start", and "once any job or job
array task in a partition is left pending, no other jobs in that partition
will be scheduled".

The second component, the backfill plugin (`SchedulerType=sched/backfill`,
the default), runs periodically and rebuilds its plan in every backfill cycle
([`slurm.conf(5)`](https://slurm.schedmd.com/slurm.conf.html),
[Scheduling Configuration Guide](https://slurm.schedmd.com/sched_config.html)).
The settings that matter here:

| Parameter | Default | Meaning (paraphrased from `slurm.conf(5)`) |
|---|---|---|
| `bf_interval` | 30 s | Time between backfill cycles. |
| `bf_window` | 1440 min | How far ahead backfill plans. A job that would start later gets no reservation. |
| `bf_resolution` | 60 s | Time grid for planned start and end times. |
| `bf_max_job_test` | 500 (100 in older releases) | Most jobs one backfill cycle examines, that is, the queue depth. |
| `bf_job_part_count_reserve` | 0 | How many highest-priority jobs per partition get reservations. 0 means every examined job that cannot start gets one. |
| `bf_max_time` | `bf_interval` | Most wall time one backfill cycle may spend. |
| `default_queue_depth` | 100 | Most jobs the main scheduler tries after a submission, completion, or similar event. |
| `sched_interval` | 60 s | Time between the main scheduler's full passes. |

Each backfill cycle counts examined jobs from zero, so `bf_max_job_test`
limits one backfill cycle, not one scheduling event. DR_EVT's periodic mode
follows the same split: its event-driven FCFS dispatch plays the main
scheduler's role, its backfill at ticks plays the plugin's role, and the
`--backfill_queue_depth` count restarts at every tick.

### Flux (Fluxion)

Fluxion is event-driven like DR_EVT's default mode. An event such as a job
submission or completion starts a backfill cycle. The cycle walks the
pending queue in priority order and, for each job, either allocates resources now or
reserves its earliest future start
([`queue_policy_bf_base_impl.hpp` at 3b49d19](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/qmanager/policies/queue_policy_bf_base_impl.hpp)).
A backfill cycle examines at most `queue-depth` jobs. It yields to Flux's event loop
after every six jobs and later resumes where it stopped. Its count of examined
jobs, `m_scheduled_cnt`, includes every job tried for allocation or
reservation, and it is
[reset only when a new backfill cycle starts](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/qmanager/policies/queue_policy_bf_base_impl.hpp#L52-L80),
so the depth limits one backfill cycle. This matches DR_EVT's event-driven
backfill cycle: all the passes at one timestamp, before time advances. Fluxion plans in whole seconds and places jobs on a
resource graph.

When a job can be neither started now nor reserved, Fluxion moves it to a
blocked list so that it does not hold its place in the queue. The resource
matcher's
[allocate-or-reserve path](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/resource/traversers/dfu.cpp#L106-L139)
first looks for resources now, then searches its planner for the earliest
future time. If there is none, it checks whether the request could be met at
all on the full resource set. If it could not, the job is rejected as
unsatisfiable. If it could, for example when some nodes are down, the matcher
returns `EBUSY`. On
[`EBUSY`](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/qmanager/policies/queue_policy_bf_base_impl.hpp#L120-L141),
the backfill policy moves the job to `m_blocked` and subtracts it from
`m_scheduled_cnt`, so it uses no queue depth.
[`reconsider_blocked_jobs()`](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/qmanager/policies/base/queue_policy_base.hpp#L188-L194)
later returns blocked jobs to the pending queue. The base policy calls it
after a job completes and after a priority change, and the source comment also
names resource-status changes. Fluxion's planner covers all future time, so a
job is blocked only when it cannot be placed at any time. DR_EVT's
event-driven `--planning_horizon` counts a job that is merely too far ahead the
same way.

| Setting | Default | Meaning |
|---|---|---|
| `queue-policy` | `fcfs` | One of `fcfs`, `easy`, `conservative`, and `hybrid`, which reserve for 0, 1, all, or N jobs. |
| `queue-params.queue-depth` | 32 | Most pending jobs one backfill cycle examines. |
| `policy-params.reservation-depth` | 64 | Under `hybrid`, how many higher-priority jobs get reservations in one backfill cycle. The queue depth caps it, so the default acts as 32. |

### IBM Spectrum LSF

LSF ran on Sierra and Lassen, which are retired.

LSF runs FCFS dispatch and backfill together in scheduling passes. By default
a pass starts at most every five seconds, but the interval can be set to zero,
which runs passes back to back. A pass takes no time in DR_EVT, so zero-interval
LSF behaves like DR_EVT's default event-driven mode. We assume LLNL configured
its systems close to this. With a longer interval, FCFS dispatch would also be
periodic, which DR_EVT does not reproduce.

LSF can also reserve nodes for a waiting parallel job and keep them idle until
it has enough. This is off by default.

## Reproducing a production configuration

| DR_EVT option | Slurm, reserve all | Slurm, reserve N | Slurm, head only | Fluxion hybrid |
|---|---|---|---|---|
| `--backfill_policy` | `conservative` | `hybrid` | `hybrid` | `hybrid` |
| `--reservation_depth` | not used | N | `1` | `reservation-depth` |
| `--backfill_interval` | `bf_interval` (30) | `bf_interval` | `bf_interval` | `0` |
| `--backfill_queue_depth` | `bf_max_job_test` (500) | `bf_max_job_test` | `bf_max_job_test` | `queue-depth` (32) |
| `--planning_time_resolution` | `bf_resolution` (60) | `bf_resolution` | `bf_resolution` | `1` |
| `--planning_horizon` | `bf_window` (86400) | `bf_window` | `bf_window` | unlimited |

"Slurm, reserve all" is Slurm's default, `bf_job_part_count_reserve=0`.
"Slurm, reserve N" sets `bf_job_part_count_reserve=N`. `--backfill_policy easy`
is the same as `hybrid` with reservation depth 1. These settings reproduce
when backfill runs and which jobs it protects, within DR_EVT's single pool of
nodes; Slurm applies them per partition. Where node reuse matters, also set
`--node_release_delay` to the target system's measured cleanup time.

### Out-of-horizon accounting

In every case below, the job holds no reservation and uses no reservation
slot.

| Planner | Uses a queue-depth slot | Re-evaluated |
|---|---|---|
| Slurm `bf_window` | Yes (`bf_max_job_test`) | Every backfill cycle |
| Fluxion blocked list | No (`m_scheduled_cnt` decremented) | After a completion or priority change |
| DR_EVT, periodic | Yes, as in Slurm | Every tick |
| DR_EVT, event-driven | No, as in Fluxion | Every pass |

DR_EVT keeps such a job in the queue and re-evaluates it on every pass, so nodes freed by completions or by a scheduled capacity change can bring
it back. It does not model the resource-status notifications Fluxion relies
on.

## Remaining differences

- **Queue depth in event-driven EASY.** EASY is meant to equal hybrid with
  reservation depth 1, but in event-driven mode their queue-depth counts
  differ. Conservative and hybrid count once across all the passes at a
  timestamp. The separate EASY, SJF, LJF, and custom-selector implementations
  count from zero in every pass, so at one timestamp they can examine more
  jobs than the depth. Aligning them is future work.

| Aspect | DR_EVT | Difference and its effect |
|---|---|---|
| When backfill runs | Event-driven by default | Slurm backfills periodically, so by default DR_EVT backfills sooner and backfilled jobs wait less than they would. `--backfill_interval` removes this. |
| Planning grid | Exact by default | Slurm rounds planned times to a grid and Fluxion plans in whole seconds. `--planning_time_resolution` reproduces either. |
| Scan and reservation depth | Unlimited by default | Slurm and Fluxion limit both. Setting the same depths removes the difference. |
| Node reuse | Immediate by default | Production nodes return after cleanup. `--node_release_delay` removes this optimism only as far as it is calibrated. |
| Eligible time | Submission time | Held or dependent jobs in a real trace became eligible after they were submitted. DR_EVT counts their wait from submission, so it overstates their wait compared with the production record. |
| Warm start | Waiting queue rebuilt | Jobs waiting before `--sim_start_time` are rebuilt by default, but their past reservations are not, so the first conservative or hybrid decision after the boundary can differ. `--warm_start_wait_queue drop` keeps the old behavior of leaving them out. |
| Resources and priority | One pool of identical nodes; FCFS, SJF, or LJF | Partitions, topology, and priority plugins are out of scope. |

With default settings, several of these rows make DR_EVT's backfilling more
optimistic than a production scheduler's. Setting the interval, grid, horizon,
and depths to match the target narrows those differences.

## Validation evidence

- **Event-driven policies.** The 34 scheduler-correctness workloads produce
  exactly the same schedule and resource trace as an independent Python
  implementation (`tests/run_scheduler_correctness_tests.sh`). This checks two
  implementations of the same model against each other. It is not a
  comparison with a production scheduler.
- **Periodic backfill.** The same workloads, under hybrid with reservation
  depth 1 and under conservative, with 7 s and 30 s ticks, match the Python
  reference in schedule and resource trace in all 136 runs
  (`tests/run_periodic_backfill_tests.sh`). A warm start in periodic mode
  matches a cold run started from the same state (`tests/test_warm_start.cpp`).
- **Planning horizon.** Event-driven EASY, hybrid, conservative, and SJF runs
  with a horizon match the Python references. This includes a case with queue
  depth 1, where an out-of-horizon head leaves its slot to the next job. An
  explicit unlimited horizon gives byte-identical output on all 34 workloads
  (`tests/run_planning_horizon_tests.sh`). How out-of-horizon jobs count
  toward the queue depth in each mode is covered by a unit test
  (`tests/test_append_job_api.cpp`) and by the Python reference test
  (`tests/test_python_conservative_options.py`).
- **Queue depth under EASY.** With a queue depth, with and without a horizon,
  every FCFS EASY implementation (deque, circular, block, and multimap)
  matches the Python EASY reference, as does the `fcfs_conservative` ordering,
  which uses the same schedulers. A depth of 1 equals no backfilling for FCFS,
  SJF, and LJF. Periodic EASY equals periodic hybrid with reservation depth 1
  under the same depth, because it runs as that hybrid
  (`tests/run_backfill_queue_depth_tests.sh`).
- **Node release delay.** The 34 workloads with a constant delay match the
  Python reference (`tests/run_node_release_delay_tests.sh`).
- **RAPS.** The [comparison helper](../../scripts/compare_with_raps.py) runs
  one job list through RAPS and DR_EVT and lines up the start times. It is a
  manual tool, not part of CI. RAPS cannot serve as a correctness check for
  DR_EVT's backfill: at RAPS commit
  [3f21cd3](https://github.com/ExaDigiT/RAPS/tree/3f21cd35b7bf6a8f5decc41050c8d871809608ad),
  three differences separate the two, each verified in the RAPS source.
  1. RAPS advances in one-second steps and notices a completion one step after
     the job really ends (`engine.py`, `prepare_timestep` and
     `complete_timestep`). A job waiting for those nodes starts a second late,
     ends a second late, and passes the delay on, so the lag compounds.
  2. To find when the head can start, its shadow time,
     [`find_backfill_job`](https://github.com/ExaDigiT/RAPS/blob/3f21cd35b7bf6a8f5decc41050c8d871809608ad/raps/schedulers/default.py#L150-L198)
     sorts running jobs by time limit (line 167) and uses the start plus limit
     of the last job it needs (lines 172–177), rather than the earliest time
     enough nodes are free. The result can be too late or too early.
  3. RAPS admits a candidate that ends exactly at the shadow time
     (`time_limit <= shadow - now`, line 202); DR_EVT requires it to end
     strictly before.

  On the CTC SP2 archive trace, the first two effects became visible from
  roughly the 200th job in a manual run against that commit.

## Calibration and extensions

- **Node release delay.** Calibrating it needs the real cleanup time. Flux
  records it for each job as `t_inactive - t_cleanup`. A replay trace gives an
  upper bound: the gap before the freed nodes are next used.
- **Planning horizon.** Set it to the target's planning window, for example
  Slurm's `bf_window`.
- **Priority hooks.** The experimental external selector lets a caller rank
  backfill candidates by its own cost. Extending the same callback to queue
  order would allow priority experiments without changing the built-in
  policies.

## Sources

- Slurm: `slurm.conf(5)` (SchedulerType, SchedulerParameters) and the
  Scheduling Configuration Guide, read from the `master` branch of the
  SchedMD repository on 2026-10-30; pinned source:
  [`_attempt_backfill`](https://github.com/SchedMD/slurm/blob/slurm-25-11-8-1/src/plugins/sched/backfill/backfill.c#L2126-L2150),
  the [`bf_max_job_test` check](https://github.com/SchedMD/slurm/blob/slurm-25-11-8-1/src/plugins/sched/backfill/backfill.c#L2343-L2349),
  the [increment of its counter, `job_test_cnt`](https://github.com/SchedMD/slurm/blob/slurm-25-11-8-1/src/plugins/sched/backfill/backfill.c#L3067-L3071), and the
  window checks [before](https://github.com/SchedMD/slurm/blob/slurm-25-11-8-1/src/plugins/sched/backfill/backfill.c#L2910-L2915) and
  [after](https://github.com/SchedMD/slurm/blob/slurm-25-11-8-1/src/plugins/sched/backfill/backfill.c#L3536-L3544) node selection.
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
- LSF: IBM Spectrum LSF 10.1 documentation, read 2026-11-04:
  [job life cycle](https://www.ibm.com/docs/en/SSWRJV_10.1.0/lsf_foundations/job_life_cycle_lsf.html),
  [`JOB_SCHEDULING_INTERVAL`](https://www.ibm.com/docs/en/SSWRJV_10.1.0/lsf_config_ref/lsb.params.job_scheduling_interval.5.html),
  [reserving processors](https://www.ibm.com/docs/en/SSWRJV_10.1.0/lsf_admin/reserving_processors.html), and
  [backfill scheduling](https://www.ibm.com/docs/en/SSWRJV_10.1.0/lsf_admin/backfill.html).
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
