# Scheduling loops and DR_EVT configurations

This page explains the core actions of batch scheduling, groups production
engines by trigger, and shows how to approximate those actions with DR_EVT's
current options. The four production loops provide context for the mappings;
[Realism and Known Gaps](REALISM_AND_GAPS.md) records the remaining differences,
excluded scope, and validation limits. Detailed policy definitions are in
[Scheduling Policies](../BACKFILLING_ALGORITHMS.md).

## FCFS dispatch and backfill

**FCFS dispatch** considers waiting jobs in submission order. Start the head
if it fits, remove it from the queue, and repeat. When the new head cannot
start, stop this strict dispatch pass. Production priority policies can
change the order; the same idea then applies to that priority-ordered queue.

**Backfill** considers later jobs that could use otherwise idle resources.
A future reservation protects a waiting job's projected start by setting
aside its requested resources for its estimated execution interval. Start a
later job only when its resource use preserves the protected reservations.

```text
strict dispatch:
    while the queue head fits now and any dispatch limit permits:
        start the head and remove it

backfill planning and selection:
    build the resource-time plan from running jobs and known capacity changes
    examine waiting jobs in order within the backfill evaluation budget
    start a job now if it fits without delaying protected reservations
    otherwise, reserve a feasible future interval when policy permits
```

These are conceptual actions, not necessarily two consecutive passes.
Slurm triggers its main dispatch and backfill separately. Fluxion and Spur
combine immediate placement and future reservation in one ordered traversal.
LSF applies its configured policies within scheduling sessions.

## Shared terms

- **Scheduling interval:** when a scheduling cycle may run.
- **Queue depth Q:** the maximum number of waiting jobs examined in a cycle.
  In DR_EVT, `--backfill_queue_depth` limits backfill examinations, excluding
  ordinary FCFS starts. In unified modes, `--dispatch_queue_depth` limits the
  whole placement prefix, including immediate starts.
  Production engines can count different stages of candidate evaluation.
- **Reservation depth R:** the maximum number of waiting jobs given protected
  future starts. It is not added to Q. Creating a reservation can consume one
  scan slot and one reservation slot for the same job.
- **Planning horizon H:** how far ahead the planner searches or records starts.
  It does not establish when scheduling runs.
- **Planning resolution:** the granularity of the future resource-time plan,
  independent of scheduling cadence and actual execution times.

Every accepted start consumes resources in the current pass's plan. Every
future reservation protects resources over its planned execution interval.
Later starts must preserve earlier protected reservations; finishing before
a reservation is one sufficient case, not a universal admission rule.
Planning uses scheduler-visible runtime estimates, not future actual runtimes.

## Categories by scheduling trigger

The trigger determines **when** decisions run; the policy and depth limits determine
**which** jobs can start or receive protection.

| Category | Trigger behavior | Production example |
|---|---|---|
| Unified event-driven | A relevant state event requests an allocation/reservation cycle | Fluxion |
| Separate dispatch and backfill triggers | Events request main dispatch; a separate interval governs backfill; main dispatch also has a periodic pass | Slurm |
| Unified timer and event triggers | An explicit notification or a periodic timer runs the same placement/reservation loop | Spur at the inspected commit |
| Sessions with a minimum interval | Scheduling work is handled in sessions no more often than the configured minimum interval | LSF |

For timing alone, suppose a fitting job arrives at 2.3 s and a periodic timer's
next wake is 3 s. Fluxion can evaluate at 2.3 s. Slurm's main scheduler can
dispatch then, subject to its throttling. Spur's submission notification can
also wake its unified loop at 2.3 s. For LSF, if the previous session began
at 0 s and the minimum interval is 5 s, the next session cannot begin before
5 s; the interval does not promise a session exactly then. A hypothetical
strict tick-gated scheduler would wait until 3 s, but that is not the inspected
Spur submission behavior.

## DR_EVT's current loop

DR_EVT batches arrivals, completions, node releases, and scheduled capacity
changes at their exact timestamps before making scheduling decisions.
There are four supported trigger modes:

```text
event-driven (backfill_interval = 0 and dispatch_interval = 0):
    apply the state-event batch
    repeat FCFS dispatch followed by backfill until no job starts
    all passes at this timestamp form one cycle

periodic backfill (backfill_interval > 0):
    on a state-event batch: apply it, then run FCFS dispatch
    on a backfill tick with the scheduling-state flag set:
        clear the flag; run backfill with a fresh evaluation budget
        repeat scheduler calls until none starts a job
    on other ticks: advance the tick deadline without a backfill scan

unified fixed periodic dispatch (dispatch_interval > 0, dispatch_on_events = false):
    on a state-event batch: apply it without scheduling
    on each tick while jobs wait:
        apply all events at that timestamp
        examine one prefix of up to dispatch_queue_depth eligible jobs
        perform FCFS starts and backfill with a fresh reservation plan
        do not repeat the scan at this tick
    retry from the current queue head at the next tick

unified timer and arrival notifications (dispatch_interval > 0, dispatch_on_events = true):
    apply all state events at their exact timestamps
    on an arrival batch or a tick while jobs wait:
        run the same unified placement/reservation/backfill pass once
        use one fresh dispatch_queue_depth prefix and reservation plan
    coalesce an arrival batch and timer at the same timestamp into one pass
    ordinary completions, releases, and capacity changes wait for a tick
    preserve the timer phase after every arrival notification
```

Periodic ticks begin one interval after the loop is initialized (normally
time 0, or the warm-start boundary), and preserve their cadence. State events at a tick are processed
before its backfill evaluation. In the event-driven and periodic-backfill
modes, the head starts as soon as it fits; a future reservation does not force it to wait for a tick. Arrivals,
resource releases, and capacity changes set the scheduling-state flag; a
backfill start alone does not set it. Periodic mode requires a circular queue
and FCFS or `fcfs_conservative` ordering, and does not support custom selectors.

| Control | Current behavior |
|---|---|
| `--dispatch_interval` | Aligned ticks run both FCFS and backfill; 0 disables unified periodic dispatch |
| `--dispatch_on_events` | Also run the unified pass on job arrival batches without resetting ticks; requires positive dispatch interval |
| `--dispatch_queue_depth` | Entire examined prefix per unified wake, including immediate starts and out-of-horizon jobs; 0 is unlimited |
| `--priority_policy` | FCFS, SJF, or LJF orders the waiting queue |
| `--backfill_policy` | EASY protects the head; hybrid protects up to R waiting jobs; conservative protects all planned earlier waiting jobs; none disables backfill |
| `--reservation_depth` | R for hybrid; a job beyond the planning horizon gets no reservation slot |
| `--backfill_queue_depth` | Q includes the waiting head and bounds backfill examinations; ordinary FCFS dispatch uses none of it; 0 is unlimited |
| `--planning_horizon` | Reservations must start within H seconds of now; jobs beyond H remain pending in queue order; omitted means unlimited |
| `--planning_time_resolution` | Round projected releases up to a grid; actual events remain exact; 0 disables rounding |
| `--total_nodes`, `--capacity_schedule` | One homogeneous node pool, optionally changing capacity over time |
| `--node_release_delay` | Hold nodes after actual completion for constant, exponential, or uniform cleanup; replayed history is not delayed |

Unified periodic dispatch uses its own Q, `--dispatch_queue_depth`, for all
examined candidates. It counts out-of-horizon candidates and retries on every
tick regardless of the scheduling-state flag. EASY reuses hybrid protection
with R=1 in this mode.

Periodic hybrid/conservative share Q across calls at one tick and count
out-of-horizon jobs. Periodic EASY performs one backfill scan per tick and
exempts an out-of-horizon head. Other examined EASY candidates still count.
In event-driven hybrid/conservative, calls at one timestamp share Q and
out-of-horizon jobs use no scan slot. Event-driven EASY paths reset Q per
pass and exempt only the out-of-horizon head; see
[Backfill evaluation accounting](REALISM_AND_GAPS.md#backfill-evaluation-accounting).

Reservations use requested time limits and known capacity changes, and are
rebuilt each pass. Jobs selected earlier in that pass are added to the plan
before later candidates are tested. Actual runtimes come from the trace or a
sampled distribution and are capped by time limits. Cleanup nodes remain
unavailable until their release event; planning does not reveal their future
actual release. A job's nodes become reusable when its slowest node finishes
cleanup. Runtime and cleanup options are defined in the
[command-line reference](../user-guide/command-line.md).

## Replicating core behavior with current options

These mappings apply to one homogeneous pool and a fixed queue-order policy.
The values are illustrative choices, not production defaults. Matching the
controls reproduces selected decisions, not the entire production engine.

| Target behavior | DR_EVT option choices |
|---|---|
| Fixed periodic FCFS with a total candidate limit | `--dispatch_interval 5 --dispatch_queue_depth 3 --backfill_policy none` |
| Fixed periodic FCFS and backfill with head protection | `--dispatch_interval 5 --dispatch_queue_depth 3 --backfill_policy hybrid --reservation_depth 1`; optionally set H and resolution |
| Slurm-like periodic backfill; protect one waiting job | `--queue_impl circular --backfill_interval 5 --backfill_policy hybrid --reservation_depth 1 --backfill_queue_depth 3 --planning_horizon 300 --planning_time_resolution 1` |
| Slurm-like periodic backfill; protect N waiting jobs | Same settings with `--reservation_depth N` |
| Slurm-like periodic backfill; protect all planned earlier jobs | Same interval, Q, H, and resolution with `--backfill_policy conservative` |
| Fluxion-like event-driven hybrid; protect one waiting job | `--backfill_interval 0 --backfill_policy hybrid --reservation_depth 1 --backfill_queue_depth 3 --planning_time_resolution 1`; omit H |
| Fluxion-like event-driven conservative | Same event-driven settings with `--backfill_policy conservative`; omit H |
| Spur-like unified timer and submission notifications | `--dispatch_interval 1 --dispatch_on_events --dispatch_queue_depth 3 --backfill_policy conservative`; omit H; use the default resource-time rule (`DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`) |
| LSF timing under the earliest-permitted-session assumption | Set `--dispatch_interval` to the assumed session interval, e.g. `5`; choose queue depth and backfill policy separately, since their LSF equivalence is unverified |

For Slurm, map `bf_interval` to the backfill interval, `bf_max_job_test` to Q,
`bf_job_part_count_reserve=N` to hybrid R, and `bf_resolution` to planning
resolution. Convert `bf_window` from minutes to seconds: a 5-minute window
becomes `--planning_horizon 300`. Its zero reservation-count setting removes
that limit, so use conservative rather than interpreting it as R=0.

For Fluxion hybrid, map `queue-depth` and `reservation-depth` to Q and R.
Use hybrid with R=1 for head protection when cycle-wide Q accounting matters.
The default event-driven mode approximates Fluxion's trigger mechanism.

The fixed-periodic rows implement the chat algorithm: update state on events,
wait for the next tick, and make one bounded scheduling pass. They model
Spur's timer path and shared candidate prefix, but Spur also dispatches on
notifications.

For LSF timing studies, assume sessions occur at every interval boundary while
jobs are waiting, with the session phase aligned to DR_EVT's ticks and no
additional session delay. Under this earliest-permitted-session assumption,
unified periodic dispatch represents the session cadence. For example, with
5-second intervals anchored at 0, an arrival at 2.3 waits until 5, and waiting
work is reconsidered at 10, 15, and so on. This is an explicit modeling
assumption, not a timing guarantee established by the supplied LSF evidence.
Queue-depth accounting, reservation depth, and horizon require separate
matching; the cadence assumption does not establish their equivalence.

Set cleanup parameters from measurements when node reuse matters. Current
options do not supply LSF sessions with variable timing beyond the assumed
regular cadence. Production eligibility and topology, additional Spur
notification sources, and the residual Slurm/Fluxion differences
are detailed in [Remaining differences](REALISM_AND_GAPS.md#remaining-differences).

## Example: separating Q, R, and H

Use four identical nodes, with three occupied until time 120 s and one idle.
At a scheduling cycle at time 0, the waiting queue is:

| Queue order | Job | Nodes | Requested runtime |
|---|---|---:|---:|
| 1 | H | 2 | 20 s |
| 2 | L | 4 | 100 s |
| 3 | S | 1 | 200 s |

Strict FCFS starts nothing because H needs two nodes. Backfill can protect H
at 120–140 s. If it also protects L, L's interval is 140–240 s. With no other
constraints and exact planning, the following **generic hybrid model** shows
what the controls change:

| Q | R | H (seconds) | Decision at time 0 |
|---:|---:|---:|---|
| 1 | 1 | 300 | Examine H and reserve 120–140; neither L nor S is tested |
| 2 | 1 | 300 | Examine H and L; reserve H; leave L pending without protection; S is untested and the idle node stays idle |
| 3 | 1 | 300 | Also examine S and start it now: S uses one node through 200 s, leaving enough nodes for H at 120 s |
| 3 | 2 | 300 | Protect H and L; S cannot start now because it would prevent L from using all four nodes at 140 s |
| 3 | 2 | 60 | H and L have no start within the horizon, so neither receives protection; S can start now |

The final row illustrates loss of reservation protection, not a universal
rule for scan accounting. For a fair comparison, specify whether out-of-horizon
jobs consume Q. Increasing R can preserve more waiting starts but reduce
backfill opportunities; increasing Q exposes more candidates. Shortening H
can remove protection even if R is large.

For DR_EVT, a baseline for the third row is this option fragment:

```text
--total_nodes 4 --priority_policy fcfs --backfill_policy hybrid
--backfill_queue_depth 3 --reservation_depth 1
--planning_horizon 300 --backfill_interval 0 --planning_time_resolution 0
```

The workload must supply the running jobs and queue above. Change
`--reservation_depth` to `2` for the fourth row or additionally change
`--planning_horizon` to `60` for the fifth. These are examples of DR_EVT's
model, not assertions of complete production equivalence.
All five time-zero decisions in this table were checked against the rebuilt
native simulator. The implementation uses the default resource-time
reservation rule (`DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`).

## Slurm: event-driven main dispatch plus periodic backfill

```text
on submission, completion, or relevant configuration change:
    update cluster state
    request a main-scheduler pass, subject to throttling and defer settings
    test up to default_queue_depth entries in priority order
    start jobs that fit now
    stop considering a partition after a job in it remains pending

on the main-scheduler interval (sched_interval):
    run the same priority dispatch with no default_queue_depth limit
    retain partition_job_depth and scheduling-time limits

on a backfill wake (bf_interval):
    skip if the controller's work/state checks do not permit a new cycle
    rebuild the future resource-time plan
    placement_tests = 0
    reservations_per_partition = 0
    scan priority-ordered job-partition entries while placement_tests < bf_max_job_test:
        stop at bf_max_time
        skip entries failing preliminary eligibility/resource checks
        skip an entry whose initial search start is beyond bf_window
        when entering placement testing: placement_tests += 1
        try to start now without delaying protected jobs
        if successful: start and update the plan
        otherwise, if a future start is within bf_window and reservation
            policy permits: reserve its resources and update the plan
                reservations_per_partition += 1 for its partition
```

`bf_max_job_test` limits placement tests, while `bf_max_job_start` separately
limits starts. An entry skipped before placement testing consumes no
placement-test slot. A start found beyond the horizon after testing still
consumes that slot, even though no reservation is created. Retries for the
same counted entry do not charge it again. Slurm also records a separate
traversal-depth statistic; that is not the cap counter.

`bf_job_part_count_reserve=N` limits protected jobs per partition; zero removes
this particular reservation limit. `bf_resolution` controls planning
granularity. A job submitted to several partitions can produce several scan
entries. Main dispatch uses neither the backfill evaluation limit nor its
reservation-depth limit. Backfill's interval and state checks are not a guarantee
of fixed, startup-phased evaluation ticks.

## Fluxion: one event-driven allocation/reservation cycle

This loop describes Fluxion's policy in `flux-sched` at the pinned commit.
The inspected `flux-core` also provides `sched-simple`, `sched-fifo`, and
`sched-backfill` modules. Their `queue-depth` limits outstanding pending
allocation requests, rather than Fluxion's per-cycle examination count.
The scheduler module and version must therefore be specified when choosing Q.

```text
on a scheduling-relevant event:
    update state and reconsider blocked jobs when the event permits
    begin a new cycle:
        release completed allocations and cancel previous future reservations
        merge new pending jobs; cursor = queue head
        tested = 0; reserved = 0
    while cursor remains and tested < Q:
        job = next pending job
        try_reserve = (reserved < R)
        attempt allocation now, or a future reservation if try_reserve
        if allocated: move to running; tested += 1
        else if reserved: retain the future commitment; reserved += 1; tested += 1
        else if EBUSY and try_reserve:
            move to blocked list; consume no test slot
        else if EBUSY:
            leave pending; tested += 1
        else: reject the request; tested += 1
        after six entries, yield and resume with the same cursor and counters
    end the cycle when the queue or Q is exhausted
```

Yielding resumes the same cycle; it does not reset Q or R. The policy supplies
R: zero for allocation-only behavior, one for EASY, N for hybrid, and the
policy's effective unlimited value for conservative. Reaching R disables
additional reservations, but allocation-only tests continue within Q.

The referenced matcher has no Slurm-like configurable H: it searches its
planner's future timeline. A blocked job is one that cannot currently be
allocated or reserved, not simply one beyond a chosen lookahead window.
Blocked jobs are reconsidered on applicable state changes, including
completion and priority changes. Fluxion plans in whole seconds.

## Spur: unified timer and event triggers

```text
wait for either a periodic timer wake or an explicit scheduler notification
    (submission sends a notification; interval_secs defaults to 1)
if this controller is not the confirmed leader: skip the cycle
update housekeeping and classify pending jobs for scheduling eligibility
order admitted candidates by effective priority and other precedence rules
rebuild placement timelines from current resources and projected releases
for each job in the first max_jobs_per_cycle candidates:
    if placement fails: continue, without replacing its scan slot
    find a feasible start preserving earlier resource-time commitments
    if it can start now: select it and charge its resources in the plan
    otherwise: reserve its future interval in the plan
publish the future plan and dispatch validated selected placements
return to waiting; the next pass restarts at the current candidate-list head
```

There is no separate unbudgeted head-dispatch pass. Immediate placements,
future reservations, and failures within the capped prefix all occupy scan
slots. However, earlier classification can inspect jobs beyond that prefix
and filter held, dependent, quota-blocked, or structurally unsuitable jobs.
Thus Q bounds the placement-list prefix, not all controller work. Quota
classification can also use additional trial placement passes.

Future placement timelines are rebuilt each real pass. No separate
configurable R or H was found in the inspected scheduler configuration;
reservations are limited by the candidate prefix and feasibility. Internal
365-day search bounds and the assumed duration of unlimited jobs are not a
configurable horizon filter that refunds Q. Idle-fill borrowers are an
exception: they can start now but receive no future reservation.

Submission can wake this loop before the next timer tick. The timer is created
when the scheduling loop starts, and its first Tokio tick is immediate;
`interval_secs` is not a universal launch gate. Periodic wakes continue even
without new submissions. There is no persistent scan cursor across passes.
For a fixed interval, notification-triggered passes do not reset the timer
cadence. With ticks at 5, 10, and 15, a submission at 7 can trigger a pass at
7 while the next timer wake remains at 10.

DR_EVT implements this core trigger pattern with `--dispatch_interval 1
--dispatch_on_events`, using a full unified pass for each job-arrival batch
and each aligned tick while jobs wait. Choose `--backfill_policy conservative`
to protect feasible future starts throughout the examined prefix and
`--dispatch_queue_depth` for its size. Omit a planning horizon to avoid adding
a filter absent from Spur's exposed configuration. Use the default resource-time
reservation rule (`DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`). This extends the
fixed periodic mode; omitting `--dispatch_on_events` retains tick-only dispatch.

The inspected controller notifies on ordinary submission, administrative
requeue (including late finalization), internal health-check submission,
health-check recovery of a drained node, and burst-buffer stage-in readiness.
Ordinary completion and ordinary node updates do not notify; the timer picks
up those changes. DR_EVT models ordinary submissions as arrivals, and leaves
its completions, cleanup releases, and capacity changes for the timer. The
other notification sources involve APIs outside its homogeneous-pool model.

DR_EVT batches simultaneous arrivals and coalesces an arrival/timer coincidence
into one bounded pass. Spur's asynchronous notification and timer can instead
produce separate ready wakes. Spur also has an immediate initial Tokio tick;
DR_EVT starts its timer one interval after initialization, with initial arrivals
providing the notification pass. These are explicit simulation abstractions.
A new streaming arrival batch at the current clock can request another pass;
repeated advancement without new arrivals does not.

Tests cover arrival passes between ticks without shifting phase, one prefix
per batch, coincident arrival/timer wakes, completion and capacity polling,
cleanup, reservation depth, randomized FCFS comparison, streaming boundaries,
exact lookahead, warm starts, and checkpoint continuation. They validate the
DR_EVT mode against its model, rather than a running Spur controller.

## LSF: scheduling sessions with a minimum interval

```text
on a scheduling-relevant event:
    mbatchd updates state and records scheduling work
when another session is permitted by JOB_SCHEDULING_INTERVAL:
    hand eligible work to mbschd
    begin a fresh session job-decision budget
    make job decisions in the configured scheduling order
    stop when the session's job-decision limit is reached
    apply configured dispatch, processor-reservation, and backfill policies
    return decisions to mbatchd for execution
    reconsider remaining pending work in a later permitted session
```

`JOB_SCHEDULING_INTERVAL` is a minimum interval between sessions, not evidence
of a fixed tick phase. `LSB_MAX_JOB_DISPATCH_PER_SESSION` limits **job
decisions**, not a verified count of examined queue entries. IBM's supplied
definition does not establish whether unsuccessful tests or future
reservations consume decisions, so the pseudocode deliberately does not use
`tested += 1` for every candidate.

DR_EVT's unified periodic dispatch is representative of LSF session timing
under the earliest-permitted-session assumption above: sessions follow the
aligned interval boundaries whenever work waits, without additional delay.
Use `--dispatch_interval` for that cadence; do not infer a mapping from
`LSB_MAX_JOB_DISPATCH_PER_SESSION` to `--dispatch_queue_depth` solely from it.

Processor reservation is configurable and off by default in the referenced
LSF documentation. The supplied IBM *Backfill scheduling* excerpt, updated
2026-10-26, establishes the following behavior:

- Backfill is enabled at queue level. Only jobs in a backfill queue may borrow
  reserved slots; reservation and backfill can operate within the same queue
  when both are enabled there.
- The protected parallel job's predicted start uses the run limits of jobs
  occupying its reserved slots. Missing run limits can prevent that estimate
  and backfill. The page also describes `RUNTIME` as a scheduling estimate
  distinct from an enforced `RUNLIMIT`.
- A job borrowing reserved slots must have a run limit that expires strictly
  before the protected job's predicted start. This restriction concerns the
  borrowed reserved slots; it does not establish a global ban on overlapping
  jobs using other resources.

The worked example in IBM's documentation has a one-CPU job running from 08:00 with a two-hour
limit, and a waiting four-CPU job reserving the other three slots. A two-CPU
job arriving at 08:30 with a one-hour limit can borrow reserved slots because
it finishes by 09:30, before the protected start at 10:00. A two-hour limit
would prevent that backfill.

EASY head protection can mimic the example in IBM's documentation. The example does not
establish that LSF protects exactly one waiting job in every configuration,
or that LSF performs only EASY backfill. EASY is the selected approximation
for this example, not a classification of the entire LSF scheduler.

The chosen simplified LSF representation uses:

- `-DDR_EVT_LEGACY_RESERVATION_BARRIER=ON` at build time for strict
  completion-before-reservation admission.
- `--dispatch_interval 5 --backfill_policy easy --dispatch_queue_depth 0`
  for an illustrative five-second session cadence, one protected waiting job,
  and unlimited candidate examination. Set the interval to the value assumed
  in the study. A finite candidate limit is a model choice until its LSF
  job-decision accounting is verified.
- One homogeneous resource pool with FCFS ordering and known time limits.
  Treat the idle slots available for backfill as slots reserved for the head.
  Assume sessions occur at the aligned interval boundaries without additional
  delay, as described above.

This is a simplified LSF model with periodic sessions and strict backfill.
The build flag applies the time barrier throughout the pool and throughout
that build. LSF's documented barrier applies to jobs borrowing reserved slots;
it does not establish the same restriction for jobs using unreserved slots.
Queue-specific eligibility, slot ownership, and multiple reserving jobs are
not reproduced by this configuration.

DR_EVT's default resource-time rule (`OFF`) can permit safe overlap with a
protected reservation. The chosen strict configuration needs dedicated
validation; the unified-dispatch checks reported above used the default rule.

The evidence still establishes neither a general R equivalent to DR_EVT's
reservation depth nor an H equivalent to `bf_window`. It also does not specify
whether reservations persist between sessions, whether failed tests and
reservation attempts consume job decisions, or whether a separate examination
limit exists. The supplied explanatory claims about those details are not
supported by the quoted IBM passages and are not used as verified behavior.

## Sources

- **Slurm:** copied commit `17abb848e20054009d136a2c504a23e5462f26c8`,
  [`backfill_agent`](https://github.com/SchedMD/slurm/blob/17abb848e20054009d136a2c504a23e5462f26c8/src/plugins/sched/backfill/backfill.c#L1238),
  [initial horizon check](https://github.com/SchedMD/slurm/blob/17abb848e20054009d136a2c504a23e5462f26c8/src/plugins/sched/backfill/backfill.c#L3201),
  [test counter](https://github.com/SchedMD/slurm/blob/17abb848e20054009d136a2c504a23e5462f26c8/src/plugins/sched/backfill/backfill.c#L3359),
  and [post-test horizon check](https://github.com/SchedMD/slurm/blob/17abb848e20054009d136a2c504a23e5462f26c8/src/plugins/sched/backfill/backfill.c#L3836).
  Main dispatch is described in the [Scheduling Configuration Guide](https://slurm.schedmd.com/sched_config.html)
  and the supplied SchedMD scheduling tutorial, pages 8–10 and 21–22.
- **Fluxion:** commit `3b49d1998ed53836618e54188f45b52bc1aac811`,
  [`queue_policy_bf_base_impl.hpp`](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/qmanager/policies/queue_policy_bf_base_impl.hpp).
  Additional pinned references: the
  [matcher](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/resource/traversers/dfu.cpp#L106-L139),
  [blocked-job reconsideration](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/qmanager/policies/base/queue_policy_base.hpp#L188-L194),
  `qmanager/config/queue_system_defaults.hpp`, and the EASY, hybrid, and
  conservative policy constructors.
- **Flux core:** copied commit `d0309fcf0e672d90e65b0797168e56a430b9fb57`,
  [job-manager allocation requests](https://github.com/flux-framework/flux-core/blob/d0309fcf0e672d90e65b0797168e56a430b9fb57/src/modules/job-manager/alloc.c#L457),
  [Python scheduler request handling and queue-depth definition](https://github.com/flux-framework/flux-core/blob/d0309fcf0e672d90e65b0797168e56a430b9fb57/src/bindings/python/flux/scheduler.py#L237),
  and its separate [EASY backfill module](https://github.com/flux-framework/flux-core/blob/d0309fcf0e672d90e65b0797168e56a430b9fb57/src/modules/sched-backfill.py).
  These modules do not establish Fluxion's reservation-depth or scan-counter rules.
- **Spur:** copied commit `693c0b32bf91a0965a63a263206e6e22a3e5a9e9`:
  `crates/spurctld/src/scheduler_loop.rs` (timer/notification selection,
  classification, trial placement, dispatch; timer selection at lines 232–250
  and late-requeue notification at line 3881), `crates/spurctld/src/cluster.rs`
  (`submit_job`, `classify_pending_jobs_with`; notification call sites at lines
  1378, 2176, 3448, 3577, and 5980),
  `crates/spur-sched/src/backfill.rs` (`init_timelines`, `schedule_jobs_with`),
  and `crates/spur-core/src/config.rs` (`SchedulerConfig`).
- **LSF:** IBM Spectrum LSF 10.1
  [`JOB_SCHEDULING_INTERVAL`](https://www.ibm.com/docs/en/SSWRJV_10.1.0/lsf_config_ref/lsb.params.job_scheduling_interval.5.html),
  [processor reservation](https://www.ibm.com/docs/en/SSWRJV_10.1.0/lsf_admin/reserving_processors.html),
  and [backfill](https://www.ibm.com/docs/en/SSWRJV_10.1.0/lsf_admin/backfill.html),
  plus the supplied IBM excerpts for `LSB_MAX_JOB_DISPATCH_PER_SESSION`,
  *Manage scheduling performance*, *Enable fast job dispatch*, and
  *Backfill scheduling*, last updated 2026-10-26. The parameter reference gives
  default 15000, with `JOB_DISPATCH_PACK_SIZE=0` selecting the CPU-based formula;
  the performance guide gives that formula without the same qualification.
  These excerpts do not establish a Fix Pack 9 transition. No LSF source was
  inspected.

- **DR_EVT:** `src/sim/sim.cpp` (`advance_to_impl`),
  `src/sim/scheduler_*.cpp`, and `src/sim/scheduler_base.cpp` on this branch.
