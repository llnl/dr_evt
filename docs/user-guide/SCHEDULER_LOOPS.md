# Scheduling loops and DR_EVT configurations

DR_EVT represents scheduling approaches through configurable policy sets:
queue ordering, backfill protection, triggers, examination and reservation
depths, planning horizon and resolution, and reservation admission. This page
explains those choices and their supported combinations. Publicly documented
production schedulers provide examples of related approaches, not identities
for DR_EVT configurations or claims of equivalent implementations.
[Realism and Known Gaps](REALISM_AND_GAPS.md) records the remaining differences
and excluded scope. Detailed policy definitions are in
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
    examine waiting jobs in order within the backfill examination limit
    start a job now if it fits without delaying protected reservations
    otherwise, reserve a feasible future interval when policy permits
```

These are conceptual actions, not necessarily two consecutive passes.
Slurm triggers its main dispatch and backfill separately. Fluxion and Spur
combine immediate placement and future reservation in one ordered traversal.
OpenPBS also evaluates starts and future protection in the same cycle.
LSF applies its configured policies within scheduling sessions.

## Shared terms

- **Scheduling interval:** when a scheduling cycle may run.
- **Scheduling pass:** one traversal with a defined starting cursor. Yielding
  and resuming a traversal does not start a new pass; counter resets depend
  on the mode described below.
- **Scheduling cycle:** the work associated with a scheduling wake. A cycle
  may contain several passes; counter resets depend on the implementation.
- **Queue depth Q:** the maximum number of counted waiting-job examinations
  before a scan stops. Its reset boundary matters as much as its value.
  By default, `--backfill_queue_depth` limits backfill examinations, excluding
  ordinary FCFS starts. With `--per_pass_accounting`, it counts the entire
  event-driven placement pass, including immediate starts. In fixed periodic
  modes, `--dispatch_queue_depth` limits the
  whole placement prefix, including immediate starts.
  Production engines can count different stages of candidate evaluation.
- **Reservation depth R:** the maximum number of waiting jobs given protected
  future starts in the current plan. It is not added to Q. Creating a reservation
  can consume one scan slot and one reservation slot for the same job.
- **Planning horizon H:** how far ahead the planner searches or records starts.
  It does not establish when scheduling runs.
- **Planning resolution:** the granularity of the future resource-time plan,
  independent of scheduling cadence and actual execution times.
- **Cycle execution-time limit:** elapsed scheduler computation time before a
  traversal stops. This is separate from Q, R, H, and scheduling cadence;
  DR_EVT does not model scheduler computation consuming simulation time. An
  assumed evaluation throughput can translate this allowance into a derived Q.

Every accepted start consumes resources in the current pass's plan. Every
future reservation protects resources over its planned execution interval.
Later starts must preserve earlier protected reservations; finishing before
a reservation is one sufficient case, not a universal admission rule.
Planning uses scheduler-visible time limits or runtime estimates, rather than
future actual runtimes.

## Categories by scheduling trigger

The trigger determines **when** decisions run; the policy and depth limits determine
**which** jobs can start or receive protection. These categories describe timers
that request scheduling, rather than daemon heartbeats or health checks. A
health-check timer matters here only if it requests a scheduling pass or
changes scheduling-relevant state.

The examples classify selected public behaviors. They do not imply that
DR_EVT implements every trigger category or every configuration of an engine.
DR_EVT's supported trigger modes are defined below.
Vendor option names in the production examples are evidence of related
policies or controls. They are not DR_EVT options, and their semantics are
not automatically reproduced by choosing similar simulator settings.

| Category | Trigger behavior | Publicly documented example |
|---|---|---|
| Event-driven dispatch and backfill | A relevant state event requests an allocation/reservation cycle | Fluxion; OpenPBS |
| Separate dispatch and backfill triggers | Events request main dispatch; a separate interval governs backfill; main dispatch also has a periodic pass | Slurm |
| Timer and notification triggers | An explicit notification or a periodic timer runs the same placement/reservation loop | Spur at the inspected commit |
| Sessions with a minimum interval | Scheduling work is handled in sessions no more often than the configured minimum interval | LSF |

For timing alone, suppose a fitting job arrives at 2.3 s and a periodic timer's
next wake is 3 s. Fluxion can evaluate at 2.3 s. Slurm's main scheduler can
dispatch then, subject to its throttling. Spur's submission notification can
also wake its placement/reservation loop at 2.3 s. For LSF, if the previous session began
at 0 s and the minimum interval is 5 s, the next session cannot begin before
5 s; the interval does not promise a session exactly then. A hypothetical
strict tick-gated scheduler would wait until 3 s, but that is not the inspected
Spur submission behavior. OpenPBS can request a cycle on that arrival too,
subject to scheduler availability; its fallback timer is discussed in
[PBS timing and cycle limits](REALISM_AND_GAPS.md#pbs-timing-and-cycle-limits).

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
        clear the flag; run backfill with a fresh examination count
        repeat scheduler calls until none starts a job
    on other ticks: advance the tick deadline without a backfill scan

fixed periodic dispatch (dispatch_interval > 0, dispatch_on_events = false):
    on a state-event batch: apply it without scheduling
    on each tick while jobs wait:
        apply all events at that timestamp
        examine one prefix of up to dispatch_queue_depth eligible jobs
        perform FCFS starts and backfill with a fresh reservation plan
        do not repeat the scan at this tick
    retry from the current queue head at the next tick

fixed periodic dispatch with arrival notifications (dispatch_interval > 0, dispatch_on_events = true):
    apply all state events at their exact timestamps
    on an arrival batch or a tick while jobs wait:
        run the same FCFS/reservation/backfill pass once
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
| `--dispatch_interval` | Aligned ticks run both FCFS and backfill; 0 disables fixed periodic dispatch |
| `--dispatch_on_events` | Also run the combined FCFS/backfill pass on job arrival batches without resetting ticks; requires positive dispatch interval |
| `--dispatch_queue_depth` | Entire examined prefix per dispatch wake, including immediate starts and out-of-horizon jobs; 0 is unlimited |
| `--priority_policy` | FCFS, SJF, or LJF orders the waiting queue |
| `--backfill_policy` | EASY protects the head; hybrid protects up to R waiting jobs; conservative protects all planned earlier waiting jobs; none disables backfill |
| `--reservation_depth` | R for hybrid; a job beyond the planning horizon gets no reservation slot |
| `--backfill_queue_depth` | Q includes the waiting head and bounds backfill examinations; ordinary FCFS dispatch uses none of it; 0 is unlimited |
| `--planning_horizon` | Reservations must start within H seconds of now; jobs beyond H remain pending in queue order; omitted means unlimited |
| `--planning_time_resolution` | Round projected releases up to a grid; actual events remain exact; 0 disables rounding |
| `--total_nodes`, `--capacity_schedule` | One homogeneous node pool, optionally changing capacity over time |
| `--node_release_delay` | Hold nodes after actual completion for constant, exponential, or uniform cleanup; replayed history is not delayed |

Fixed periodic dispatch uses its own Q, `--dispatch_queue_depth`, for all
examined candidates. It counts out-of-horizon candidates and retries on every
tick regardless of the scheduling-state flag. EASY reuses hybrid protection
with R=1 in this mode.

Trigger modes are selected once per `advance_to` call (and at warm-start
entry). The event loop uses compile-time specializations, so event-driven
and separate periodic-backfill loops contain no arrival-notification checks;
fixed periodic dispatch also excludes them when the option is disabled.

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
[command-line reference](command-line.md).

## Supported policy combinations

These policy sets apply to one homogeneous pool and a fixed queue-order policy.
The values are illustrative study choices. Similar controls can appear in
production schedulers, but matching option values does not establish matching
decisions or implementation semantics.

| Policy set | DR_EVT option choices |
|---|---|
| Fixed periodic FCFS with a total candidate limit | `--dispatch_interval 5 --dispatch_queue_depth 3 --backfill_policy none` |
| Fixed periodic FCFS and backfill with head protection | `--dispatch_interval 5 --dispatch_queue_depth 3 --backfill_policy hybrid --reservation_depth 1`; optionally set H and resolution |
| Event-driven FCFS with separate periodic backfill; protect one waiting job | `--queue_impl circular --backfill_interval 5 --backfill_policy hybrid --reservation_depth 1 --backfill_queue_depth 3 --planning_horizon 300 --planning_time_resolution 1` |
| Event-driven FCFS with separate periodic backfill; protect N waiting jobs | Same settings with `--reservation_depth N` |
| Event-driven FCFS with separate periodic conservative backfill | Same interval, Q, H, and resolution with `--backfill_policy conservative` |
| Event-driven head protection with cycle-wide backfill Q | `--backfill_interval 0 --backfill_policy hybrid --reservation_depth 1 --backfill_queue_depth 3 --planning_time_resolution 1`; omit H |
| Event-driven conservative backfill | Same event-driven settings with `--backfill_policy conservative`; omit H |
| Fixed periodic conservative backfill with arrival notifications | `--dispatch_interval 1 --dispatch_on_events --dispatch_queue_depth 3 --backfill_policy conservative`; omit H; use the default resource-time rule (`DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`) |
| Event-driven hybrid with backfill examination cap Q | `--priority_policy fcfs --backfill_policy hybrid --reservation_depth N --backfill_interval 0 --dispatch_interval 0 --backfill_queue_depth Q`; omit H; use the default resource-time rule (`DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`) |
| Fixed periodic hybrid with total-candidate cap Q | `--dispatch_interval I --dispatch_queue_depth Q --backfill_policy hybrid --reservation_depth N`; omit H; use the default resource-time rule |
| Fixed periodic EASY with strict completion-before-start admission | `--dispatch_interval 5 --backfill_policy easy --dispatch_queue_depth 0`; omit H; build with `DR_EVT_LEGACY_RESERVATION_BARRIER=ON` |

Related controls in Slurm include `bf_interval` for backfill cadence,
`bf_max_job_test` for Q, `bf_job_part_count_reserve=N` for protection depth,
and `bf_resolution` for planning resolution. These are conceptual counterparts;
their accounting and rounding need not agree with DR_EVT. Convert `bf_window`
from minutes to seconds when choosing H: a 5-minute window
becomes `--planning_horizon 300`. Its zero reservation-count setting removes
that limit, so use conservative rather than interpreting it as R=0.

Fluxion hybrid also exposes `queue-depth` and `reservation-depth`, related
to Q and R.
Use hybrid with R=1 for head protection when cycle-wide Q accounting matters.
Its event-driven allocation/reservation cycle belongs to the event-driven
category; candidate accounting and blocked-job handling differ.

OpenPBS with `strict_ordering=true` and global protection depth N > 0
provides another example of event-driven hybrid protection.
The event-driven row excludes ordinary FCFS starts from Q; the fixed-cadence
row includes them but uses aligned ticks rather than its event and fallback
timing. See
[PBS timing and cycle limits](REALISM_AND_GAPS.md#pbs-timing-and-cycle-limits).

The fixed-periodic rows update state on events, wait for the next tick, and
make one bounded scheduling pass. Spur provides an example of a shared
placement/reservation prefix with timer and notification triggers; the
arrival-enabled policy set studies that combination under DR_EVT's wake rules.

LSF's minimum-interval sessions relate to fixed cadence only under the
aligned-session assumption described in the
[LSF timing example](#lsf-scheduling-sessions-with-a-minimum-interval).
Set cleanup parameters from measurements when node reuse matters. Production
modeling limits are in [Remaining differences](REALISM_AND_GAPS.md#remaining-differences).

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

## Per-pass placement accounting

`--per_pass_accounting` selects a dedicated event-driven FCFS subclass
with a circular queue. It supports EASY (R=1), hybrid (configured R),
conservative (unlimited R), and strict FCFS. `--backfill_queue_depth` supplies
Q for the entire placement pass, including ordinary starts and the blocked
head. Use zero dispatch/backfill intervals and an unlimited planning horizon.

Each pass scans from the queue head with fresh Q/R and a rebuilt reservation
plan. An immediate start consumes Q; a future reservation consumes Q and R.
Before R is reached, each candidate can start now or receive a future
reservation. After R, only immediate starts are tested, and failed tests
consume Q. A job with no feasible allocation or reservation consumes neither
count and is skipped until a state event permits reconsideration.

When Q is reached, candidates remain, and the pass allocated or reserved a
job, another pass runs at the same simulation time. Already selected starts
remain committed. A **tentative guard** stops retries when a pass starts no
jobs, blocks no new jobs, and recreates the previous reservation plan. A
finite pass ceiling also protects termination. The next arrival, completion,
or capacity change can request scheduling again. These are synchronous
policy passes; external callback invocation is not modeled. The guard must
be revisited once the native restart handling is fully understood.

```sh
--priority_policy fcfs --queue_impl circular --backfill_policy hybrid \
--reservation_depth 32 --backfill_queue_depth 64 --per_pass_accounting
```

This subclass always uses resource-time reservation protection, independently
of `DR_EVT_LEGACY_RESERVATION_BARRIER`. It retains DR_EVT's aggregate node-count
resource model. Other scheduler implementations keep their existing
accounting and admission rules.

## Fluxion: event-driven allocation/reservation passes

This loop describes the backfill policy in `flux-sched` at the cited
revision `9eba598b4e0940d597695e65f20f5357fc513427`. A pass starts at the queue
head with fresh examination and reservation counters.

```text
on a scheduling-relevant event:
    update state and reconsider blocked jobs when the event permits
    request a scheduling pass

on each requested pass:
    cancel previous future reservations; merge new pending jobs
    cursor = queue head; tested = 0; reserved = 0; dirty = false
    while cursor remains and tested < Q:
        job = next pending job
        try_reserve = (reserved < R)
        attempt allocation now, or a future reservation if try_reserve
        if allocated:
            move to running; tested += 1; dirty = true
        else if reserved:
            keep pending; reserved += 1; tested += 1; dirty = true
        else if EBUSY and try_reserve:
            move to blocked list; consume no test slot
        else if EBUSY:
            leave pending; tested += 1
        else:
            reject the request; tested += 1
        asynchronous matching resumes with the same cursor and counters
        response/annotation processing between callbacks can clear dirty
    end the pass
    if tested reached Q AND cursor remains AND dirty:
        request another pass, starting again from the queue head
```

A successful reservation consumes both a test slot and a reservation slot;
it does not reset either counter. Yielding within a pass preserves them.
A new pass resets both counters and replaces the previous reservation plan.
Thus Q limits one pass, not cumulative work over repeated passes, and R
limits one plan rather than reservations accumulated across passes.

The policy supplies R: zero for allocation-only behavior, one for EASY,
N for hybrid, and an effective unlimited value for conservative. Reaching R
disables further reservations but allows immediate-allocation tests within Q.
With R=32 and Q=32, reserving 32 candidates ends that pass; with Q=64,
another 32 candidates can still be tested for backfill. Successful reservations
also set the dirty bit, so another pass can be requested even if no job starts.
The bit is the current `is_scheduled()` state, not a record of every success
in the pass: response/annotation processing can clear it before a later
matching reply. Thus a reservation earlier in a pass does not by itself
guarantee another pass at Q. Immediate-allocation `EBUSY` failures count toward
Q; allocation-or-reservation `EBUSY` failures do not.

The referenced matcher has no Slurm-like configurable H: it searches its
planner's future timeline. A blocked job is one that cannot currently be
allocated or reserved, not simply one beyond a chosen lookahead window.
Blocked jobs are reconsidered on applicable state changes, including
completion and priority changes. Fluxion plans in whole seconds.

DR_EVT's `--per_pass_accounting` selects
[per-pass placement accounting](#per-pass-placement-accounting), which models
these counting and restart rules with synchronous passes. Its tentative
termination guard does not claim to reproduce the asynchronous flag-clearing
mechanism. The default hybrid/conservative scheduler retains its separate
backfill examination count shared across calls at one timestamp.

## Spur: timer and submission notification triggers

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

The supported fixed periodic dispatch policy with `--dispatch_interval 1
--dispatch_on_events` shares the timer-plus-submission trigger category,
using one FCFS/backfill pass for each job-arrival batch
and each aligned tick while jobs wait. Choose `--backfill_policy conservative`
to protect feasible future starts throughout the examined prefix and
`--dispatch_queue_depth` for its size. Omit a planning horizon to avoid adding
a lookahead filter when studying this policy set. Use the default resource-time
reservation rule (`DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`). This extends the
fixed periodic mode; omitting `--dispatch_on_events` retains tick-only dispatch.

This policy set models ordinary submission notifications and timer polling;
it does not implement the production controller's asynchronous execution.
Additional notification sources and asynchronous wake differences are in
[Trigger and evaluation scope](REALISM_AND_GAPS.md#trigger-and-evaluation-scope);
regression coverage is described in [Testing Guide](../TESTING_GUIDE.md).

## OpenPBS: event-driven dispatch and backfill

For ordinary jobs with strict ordering, OpenPBS follows the fixed periodic
event-driven pattern: rebuild the calendar, examine jobs in order, start
fitting jobs, and protect eligible waiting jobs. Its fallback timer and
cycle-limit differences are in
[PBS timing and cycle limits](REALISM_AND_GAPS.md#pbs-timing-and-cycle-limits).

| OpenPBS control | Meaning | Inspected default |
|---|---|---|
| `scheduler_iteration` | Fallback scheduling interval | 600 seconds |
| `sched_cycle_length` | Main-traversal execution-time limit | 20 minutes |
| `max_job_check` | Candidate cap Q_count, including starts and failed checks | No count cap (`ALL_JOBS`) |
| `backfill_depth` | Protected waiting-job depth R; queue values override | Unset uses 1; server 0 disables backfill |
| `strict_ordering` | Preserve queue order through backfill protection | False |

`backfill_depth` counts protected waiting jobs, not backfill borrowers or
`pbs_rsub` advance reservations. Reaching R does not stop later examinations.
With strict ordering, R=1 approximates head protection and R=N hybrid backfill;
this does not classify every PBS policy as EASY.

No separate `bf_window`-like horizon was found in the inspected path; omit H
in the mapping. Walltime limits execution duration, not future-start lookahead.
Unstarted jobs remain pending; infeasible jobs can receive a `can_never_run`
classification without future protection. Use DR_EVT's default resource-time
rule (`DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`) to preserve protected intervals.

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

DR_EVT supports fixed periodic dispatch. Relating that policy to LSF's
minimum-interval sessions requires an additional study assumption: sessions
follow aligned interval boundaries whenever work waits, without additional
delay. The interval setting alone does not establish this behavior in LSF.
Use `--dispatch_interval` for the assumed cadence; do not infer a mapping from
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

EASY head protection can produce the decisions in IBM's worked example.
This agreement does not
establish that LSF protects exactly one waiting job in every configuration,
or that LSF performs only EASY backfill. The example illustrates an approach
with head protection; it does not identify a complete production policy set.

One supported policy set for studying head protection and strict admission uses:

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

Reserved-slot ownership and admission differences are in
[LSF admission on reserved slots](REALISM_AND_GAPS.md#lsf-admission-on-reserved-slots).
Accounting and policy evidence limits are in
[LSF evidence limits](REALISM_AND_GAPS.md#sources).

## Sources

- **Slurm:** copied commit `17abb848e20054009d136a2c504a23e5462f26c8`,
  [`backfill_agent`](https://github.com/SchedMD/slurm/blob/17abb848e20054009d136a2c504a23e5462f26c8/src/plugins/sched/backfill/backfill.c#L1238).
  Main dispatch is described in the [Scheduling Configuration Guide](https://slurm.schedmd.com/sched_config.html)
  and the supplied SchedMD scheduling tutorial, pages 8–10 and 21–22.
- **Fluxion pass/restart rules:** source commit
  `9eba598b4e0940d597695e65f20f5357fc513427`,
  [`queue_policy_bf_base_impl.hpp`](https://github.com/flux-framework/flux-sched/blob/9eba598b4e0940d597695e65f20f5357fc513427/qmanager/policies/queue_policy_bf_base_impl.hpp).
  Earlier policy inspection at `3b49d1998ed53836618e54188f45b52bc1aac811`
  also covered the
  [matcher](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/resource/traversers/dfu.cpp#L106-L139),
  [blocked-job reconsideration](https://github.com/flux-framework/flux-sched/blob/3b49d1998ed53836618e54188f45b52bc1aac811/qmanager/policies/base/queue_policy_base.hpp#L188-L194),
  `qmanager/config/queue_system_defaults.hpp`, and the EASY, hybrid, and
  conservative policy constructors.
- **Spur:** copied commit `693c0b32bf91a0965a63a263206e6e22a3e5a9e9`:
  `crates/spurctld/src/scheduler_loop.rs` (timer/notification selection,
  classification, trial placement, dispatch; timer selection at lines 232–250),
  `crates/spurctld/src/cluster.rs` (`submit_job`, `classify_pending_jobs_with`),
  `crates/spur-sched/src/backfill.rs` (`init_timelines`, `schedule_jobs_with`),
  and `crates/spur-core/src/config.rs` (`SchedulerConfig`).
- **OpenPBS:** copied commit `cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a`:
  [shared scheduler command path](https://github.com/openpbs/openpbs/blob/cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a/src/scheduler/fifo.cpp#L502),
  [waiting-job protection depth](https://github.com/openpbs/openpbs/blob/cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a/src/scheduler/fifo.cpp#L1644),
  and [future calendar search](https://github.com/openpbs/openpbs/blob/cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a/src/scheduler/simulate.cpp#L715).
  Control definitions also use `src/scheduler/config.h`, `parse.cpp`,
  `server_info.cpp`, and the scheduler/server attribute manuals under `doc/man1`.
- **LSF:** IBM Spectrum LSF 10.1
  [`JOB_SCHEDULING_INTERVAL`](https://www.ibm.com/docs/en/SSWRJV_10.1.0/lsf_config_ref/lsb.params.job_scheduling_interval.5.html),
  [processor reservation](https://www.ibm.com/docs/en/SSWRJV_10.1.0/lsf_admin/reserving_processors.html),
  and [backfill](https://www.ibm.com/docs/en/SSWRJV_10.1.0/lsf_admin/backfill.html),
  plus the supplied *Backfill scheduling* excerpt, updated 2026-10-26.

- **DR_EVT:** `src/sim/sim.cpp` (`advance_to_impl`),
  `src/sim/scheduler_*.cpp`, and `src/sim/scheduler_base.cpp` on this branch.

References for accounting differences, fallback timing, and evidence limits
are in [Realism and Known Gaps](REALISM_AND_GAPS.md#sources).
