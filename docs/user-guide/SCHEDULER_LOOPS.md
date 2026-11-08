# Scheduling loops and DR_EVT configurations

DR_EVT configures queue ordering, backfill protection, scheduling triggers,
examination and reservation depths, planning horizon and resolution, and
reservation admission. This page explains the supported combinations and how
they represent selected production scheduling behaviors.
[Realism and Known Gaps](REALISM_AND_GAPS.md) records differences and excluded
scope; [Scheduling Validation](SCHEDULER_VALIDATION.md) reports measured
comparisons. Detailed policy definitions are in
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

Slurm triggers main dispatch and backfill separately. Fluxion and Spur
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

Starts and future reservations consume resources in the current plan. Later
starts must preserve earlier reservations; they may overlap if enough resources
remain. Planning uses visible time limits or runtime estimates, rather than
future actual runtimes.

An **endpoint rule** determines whether a job ending at 120 s can fit before
a reservation starting at 120 s. A **half-open** fit, written `[start, end)`,
excludes the end timestamp, so the intervals can touch. An **inclusive** fit
also checks capacity at 120 s, so the new reservation can block that fit.
The rule applies both to starting a job now and finding its future start;
it does not extend the job's execution time.

## Categories by scheduling trigger

Triggers determine **when** decisions run; policies and depth limits determine
**which** jobs can start or receive protection. Only timers that request
scheduling or change scheduling-relevant state matter here.
The examples describe selected production behaviors. Similar vendor and DR_EVT
options can have different semantics; the supported DR_EVT modes follow below.

The production-loop descriptions and pseudocode are DR_EVT's explanatory
summaries of the cited sources. Product and project names identify the systems
being discussed; they do not imply affiliation or endorsement.

| Category | Trigger behavior | Publicly documented example |
|---|---|---|
| Event-driven dispatch and backfill | A relevant state event requests an allocation/reservation cycle | Fluxion; OpenPBS |
| Separate dispatch and backfill triggers | Events request main dispatch; a separate interval governs backfill; main dispatch also has a periodic pass | Slurm |
| Timer and notification triggers | An explicit notification or a periodic timer runs the same placement/reservation loop | Spur at the inspected commit |
| Sessions with a minimum interval | Scheduling work is handled in sessions no more often than the configured minimum interval | LSF |

Suppose a fitting job arrives at 2.3 s before a timer wake at 3 s. Fluxion,
Slurm's main scheduler, Spur's submission notification, and OpenPBS can request
scheduling at 2.3 s, subject to controller availability and any throttling.
Tick-only dispatch waits until 3 s. For LSF, a session at 0 s and a minimum
interval of 5 s prevent another session before 5 s, but do not guarantee one
at 5 s. OpenPBS's fallback timer is discussed in
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
    any job start sets the flag for the next tick's changed queue prefix
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

Ticks begin one interval after initialization (time 0 or the warm-start
boundary) and preserve their cadence. Events at a tick are processed first.
Event-driven and separate periodic-backfill modes start a fitting head without
waiting for a tick. Arrivals, resource releases, capacity changes, and starts
set the scheduling-state flag. Separate periodic backfill requires a circular
queue and FCFS or `fcfs_conservative` ordering; custom selectors are unsupported.

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

Periodic hybrid/conservative share Q across calls at one tick and count
out-of-horizon jobs. Periodic EASY performs one backfill scan per tick and
exempts an out-of-horizon head. Other examined EASY candidates still count.
In event-driven hybrid/conservative, calls at one timestamp share Q and
out-of-horizon jobs use no scan slot. Event-driven EASY paths reset Q per
pass and exempt only the out-of-horizon head; see
[Backfill evaluation accounting](REALISM_AND_GAPS.md#backfill-evaluation-accounting).

Reservations are rebuilt each pass from requested time limits and known
capacity changes. Earlier selections enter the plan before later candidates
are tested. Actual runtimes come from the trace or a sampled distribution,
capped by time limits. Cleanup nodes remain unavailable until release; their
future actual release times are hidden from planning. A job's nodes become
reusable when its slowest node finishes cleanup. Options are defined in the
[command-line reference](command-line.md).

## Supported policy combinations

These examples use one homogeneous pool and a fixed queue-order policy.
Values are illustrative; production mappings and their limits follow the table.

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
and `bf_resolution` for planning resolution. Accounting and rounding can differ.
Convert `bf_window`
from minutes to seconds when choosing H: a 5-minute window
becomes `--planning_horizon 300`. Its zero reservation-count setting removes
that limit, so use conservative rather than interpreting it as R=0.

Fluxion hybrid exposes `queue-depth` and `reservation-depth` for Q and R.
Use DR_EVT's `--per_pass_accounting` to select its Flux-style placement loop;
default hybrid instead shares backfill Q across calls at one timestamp.

OpenPBS with `strict_ordering=true` and global protection depth N > 0
provides another example of event-driven hybrid protection.
The event-driven row excludes ordinary FCFS starts from Q; the fixed-cadence
row includes them but uses aligned ticks rather than its event and fallback
timing. See
[PBS timing and cycle limits](REALISM_AND_GAPS.md#pbs-timing-and-cycle-limits).

Spur shares one placement/reservation prefix between timer and notification
wakes. Represent it with fixed periodic dispatch and `--dispatch_on_events`;
omitting that option restricts dispatch to ticks.

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

Increasing R protects more waiting starts but can reduce backfill opportunities;
increasing Q exposes more candidates. Shortening H can remove protection even
with large R. The final row assumes S is examined: whether out-of-horizon jobs
consume Q depends on the scheduler.

For DR_EVT, a baseline for the third row is this option fragment:

```text
--total_nodes 4 --priority_policy fcfs --backfill_policy hybrid
--backfill_queue_depth 3 --reservation_depth 1
--planning_horizon 300 --backfill_interval 0 --planning_time_resolution 0
```

The workload must supply the running jobs and queue above. Change
`--reservation_depth` to `2` for the fourth row or additionally change
`--planning_horizon` to `60` for the fifth. All five time-zero decisions were
checked against DR_EVT with the default resource-time reservation rule
(`DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`).

## Slurm: event-driven main dispatch plus periodic backfill

**Endpoint rule:** backfill checks capacity at the candidate's end timestamp too (inclusive).

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
passes. The guard approximates native asynchronous restart handling, which
remains under investigation.

```sh
--priority_policy fcfs --queue_impl circular --backfill_policy hybrid \
--reservation_depth 32 --backfill_queue_depth 64 --per_pass_accounting
```

This subclass always uses resource-time reservation protection, independently
of `DR_EVT_LEGACY_RESERVATION_BARRIER`. It retains DR_EVT's aggregate node-count
resource model.

## Fluxion: event-driven allocation/reservation passes

**Endpoint rule:** resource fit checks exclude the end timestamp (`[start, end)`).

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

A reservation consumes both Q and R. Yielding preserves the counters; a new
pass resets both and replaces the reservation plan. Q therefore limits each
pass, and R each plan, rather than cumulative work across passes.

R is zero for allocation-only behavior, one for EASY, N for hybrid, and
effectively unlimited for conservative. After R reservations, immediate
allocation tests can continue within Q: R=32, Q=64 permits another 32 tests.
Reservations set the dirty bit and can request another pass even without a
start. However, response/annotation processing can clear `is_scheduled()` before
a later matching reply, so an earlier reservation does not guarantee a retry.
Immediate-allocation `EBUSY` failures count toward Q; allocation-or-reservation
`EBUSY` failures do not.

The referenced matcher has no Slurm-like configurable H: it searches its
planner's future timeline. A blocked job is one that cannot currently be
allocated or reserved, not simply one beyond a chosen lookahead window.
Blocked jobs are reconsidered on applicable state changes, including
completion and priority changes. Fluxion plans in whole seconds.

DR_EVT's `--per_pass_accounting` selects
[per-pass placement accounting](#per-pass-placement-accounting). It models the
counts with synchronous passes and approximates restart handling with a
termination guard; asynchronous flag clearing is not reproduced.

## Spur: timer and submission notification triggers

**Endpoint rule:** Spur and DR_EVT's timer/notification model exclude the end timestamp (`[start, end)`).

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

Immediate placements, future reservations, and failures all consume slots in
the capped prefix; there is no separate unbudgeted head dispatch. Earlier
classification can inspect and filter held, dependent, quota-blocked, or
structurally unsuitable jobs beyond that prefix. Q limits placement candidates,
not all controller work; quota classification can add trial placement passes.

Future placement timelines are rebuilt each real pass. No separate
configurable R or H was found in the inspected scheduler configuration;
reservations are limited by the candidate prefix and feasibility. Internal
365-day search bounds and the assumed duration of unlimited jobs are not a
configurable horizon filter that refunds Q. Idle-fill borrowers are an
exception: they can start now but receive no future reservation.

The timer starts with the loop and has an immediate first Tokio tick. Wakes
continue without new submissions, and every pass restarts at the candidate-list
head. Submission notifications preserve timer phase: with ticks at 5, 10, and
15, a submission at 7 can run a pass at 7 without moving the next tick from 10.

Use `--dispatch_interval 1 --dispatch_on_events` for one DR_EVT pass per
arrival batch and aligned tick while jobs wait. Set `--backfill_policy conservative`
to protect feasible starts throughout the prefix, and
`--dispatch_queue_depth` for its size. Omit H and use the default resource-time
rule (`DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`). Additional notifications and
asynchronous wake differences are in
[Trigger and evaluation scope](REALISM_AND_GAPS.md#trigger-and-evaluation-scope);
regression coverage is described in [Testing Guide](../TESTING_GUIDE.md).

## OpenPBS: event-driven dispatch and backfill

**Endpoint rule:** ordinary resource/calendar fit checks exclude the end timestamp (`[start, end)`).

For ordinary jobs with strict ordering, OpenPBS uses event-driven cycles:
rebuild the calendar, examine jobs in order, start fitting jobs, and protect
eligible waiting jobs. Its fallback timer and
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
decisions**, not a verified count of examined queue entries. The cited IBM
documentation does not establish whether unsuccessful tests or future
reservations consume decisions, so the pseudocode deliberately does not use
`tested += 1` for every candidate.

Using DR_EVT's `--dispatch_interval` for LSF assumes sessions run at aligned
boundaries whenever work waits, without additional delay. LSF's minimum
interval does not establish that cadence or a mapping from its job-decision
limit to `--dispatch_queue_depth`.

IBM documents processor reservation as optional and initially disabled.
Its [backfill guide](https://www.ibm.com/docs/en/SSWRJV_10.1.0/lsf_admin/backfill.html)
describes queue-level eligibility and admission based on the protected job's
predicted start. A borrower needs a run limit that ends before that start;
missing limits on occupying jobs can prevent the estimate. `RUNTIME` provides
a scheduling estimate, while `RUNLIMIT` constrains execution. The admission
restriction concerns borrowed reserved slots, not jobs using other resources.
This documentation does not establish a universal protection depth or EASY
policy.

For a **DR_EVT illustration**, take six nodes, five occupied until 100 s,
and a head job requiring all six. A one-node job arriving at 30 s with a
60-second limit can finish at 90 s, before the protected start. With a
90-second limit it would run through 120 s and fail strict admission.
This constructed example illustrates the modeled rule, rather than a measured
LSF result.

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

- **Slurm:** inspected source revision `17abb848e20054009d136a2c504a23e5462f26c8`,
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
- **Spur:** inspected source revision `693c0b32bf91a0965a63a263206e6e22a3e5a9e9`:
  `crates/spurctld/src/scheduler_loop.rs` (timer/notification selection,
  classification, trial placement, dispatch; timer selection at lines 232–250),
  `crates/spurctld/src/cluster.rs` (`submit_job`, `classify_pending_jobs_with`),
  `crates/spur-sched/src/backfill.rs` (`init_timelines`, `schedule_jobs_with`),
  and `crates/spur-core/src/config.rs` (`SchedulerConfig`).
- **OpenPBS:** inspected source revision `cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a`:
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

Upstream software and documentation retain their respective copyrights and
license terms. The inspected repositories provide their terms in
[Slurm COPYING](https://github.com/SchedMD/slurm/blob/17abb848e20054009d136a2c504a23e5462f26c8/COPYING),
[Flux Core LICENSE](https://github.com/flux-framework/flux-core/blob/d0309fcf0e672d90e65b0797168e56a430b9fb57/LICENSE)
and [NOTICE](https://github.com/flux-framework/flux-core/blob/d0309fcf0e672d90e65b0797168e56a430b9fb57/NOTICE.LLNS),
[Fluxion LICENSE](https://github.com/flux-framework/flux-sched/blob/9eba598b4e0940d597695e65f20f5357fc513427/LICENSE)
and [NOTICE](https://github.com/flux-framework/flux-sched/blob/9eba598b4e0940d597695e65f20f5357fc513427/NOTICE.LLNS),
[Spur LICENSE](https://github.com/ROCm/spur/blob/693c0b32bf91a0965a63a263206e6e22a3e5a9e9/LICENSE),
and [OpenPBS LICENSE](https://github.com/openpbs/openpbs/blob/cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a/LICENSE).
