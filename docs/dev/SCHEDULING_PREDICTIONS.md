# Scheduling Predictions and Reservations

## Status and scope

This document records the design for exposing scheduler timing information to
streaming clients and experiments. It separates a scheduler reservation from a
start-time estimate and from an aggregate queue-drain estimate. The common
cached live-planner query, on-demand per-job query, resource-area per-job
approximation, and synchronous in-memory exact lookahead are implemented.

The on-demand queries perform prediction work only when called. The separate
cached interface retains values that normal scheduling already computes; it
does not add a forecast scan or prediction fields to waiting-queue entries.

## Terms

| Term | Meaning |
| --- | --- |
| Reservation | A start time protected by the scheduler's current backfill policy. Backfill work must not delay it. |
| Start-time estimate | A non-binding prediction for one waiting job. It can change after a later scheduling pass, arrival, or capacity change. |
| Lookahead | An output-free fork of the current simulation that runs the real scheduler forward. It provides the most faithful estimates. |
| Queue-drain horizon | An aggregate estimate of when the currently known waiting work will drain. It is intended for experiments, not as a job-start guarantee. |

A protected reservation is authoritative for that job's reported estimated start;
there is no reason to replace it with a weaker estimate. Jobs without a
reservation may still receive an estimate, but that estimate must respect the
capacity claimed by protected reservations.

## Cached start estimates: `get_estimated_job_starts()`

This interface follows the reporting model of Slurm `squeue --start`: read
predictions produced during the latest live scheduling pass. It returns
`Estimated_Job_Starts` with:

- `estimated_at`: the simulation timestamp of that pass, or no value before
  any planning pass and immediately after checkpoint restore.
- `jobs`: currently eligible pending jobs, sorted by predicted start time.
  Jobs without a prediction follow in scheduler queue order. Future arrivals,
  running jobs, and completed jobs are excluded.
- Each job has `job_idx`, optional `expected_start_time`, and
  `start_time_kind` (`NONE`, `RESERVED`, or `ESTIMATED`).

The query never runs a forecast, clones state, advances simulation, or changes
the live plan. A finite start computed beyond the planning horizon may be
reported as `ESTIMATED`, without a protected reservation. The query does not
extend the scheduler's examination budget to fill missing predictions.
Predictions can be stale between periodic backfill ticks; `estimated_at` makes
their age visible. Appended jobs have no cached prediction until a live pass
computes one. Estimates are not clamped to the current time.

Coverage follows the actual implementation's planning work. EASY generally
projects only the blocked head; its later candidates are tested for admission
now rather than assigned future starts. Hybrid and conservative planners save
the future fits they compute within their scan. A pass that returns early,
for example because no nodes are free, may produce only a head estimate.
When one wake makes several internal passes, the latest recorded prediction
for each job wins; starting jobs are omitted from the query result.
Schedulers that do not calculate future fits report unknown start times.

Storage is a reusable sparse vector of predicted job IDs, timestamps, and
protection flags, plus one timestamp for the pass. On a typical 64-bit build,
each cached record occupies 24 bytes; vector capacity follows the largest
recorded pass. The live path adds writes for computed predictions but no
extra resource searches. Query enumeration, lookup, and sorting still cost
work proportional to the pending population; the query is not constant time.
The query sorts only jobs with known predictions; unknown jobs retain queue
order. With N waiting-queue entries, P cached records, and K eligible known
starts, the query costs O(N + P + K log K). It skips sorting when all starts are
unknown and avoids partitioning when all starts are known. This uses query-
local bookkeeping and temporary algorithm storage, with no extra fields in
the live scheduler or waiting queue.
The cache is transient and is not serialized into checkpoints.

The interface is exposed in C++ and Python as `get_estimated_job_starts()` and
over gRPC as `GetEstimatedJobStartsRequest` / `GetEstimatedJobStartsResponse`.

## Slurm comparison

These observations come from the local Slurm 26.11.0 release-candidate source
under `external/slurm`.

### `squeue --start`

The command displays pending jobs' expected starts and planned nodes. It reads
job information; it does not initiate a new forecasting run. The backfill
scheduler computes estimates during its normal passes and stores them in
`job_record_t.start_time`, an existing field shared by expected and actual
start times. Waiting-queue entries point to job records rather than carrying
another prediction timestamp.

At the beginning of a backfill cycle, Slurm clears pending-job estimates.
Once `bf_max_job_test` is reached, it stops testing more queue entries; it
does not perform another scan to predict their starts for `squeue`. A tested
job can retain a start beyond `bf_window`, but Slurm creates no backfill
reservation for that out-of-window start. Thus an estimate and a protected
reservation are different results.

Source paths: `src/squeue/opts.c`, `src/squeue/squeue.c`,
`src/common/job_record.h`, and `src/plugins/sched/backfill/backfill.c` within
the Slurm checkout. Its `_clear_job_estimates()` resets pending starts, and
the `StartTime set to time after current backfill window` branch explicitly
keeps an estimate without creating a reservation.

### `sbatch --test-only`

This command asks for a fresh estimate for a hypothetical submission. It calls
`slurm_job_will_run()`, which sends `REQUEST_JOB_WILL_RUN`. The controller
constructs a temporary job, invokes `job_start_data()`, and removes the job.
With `select/cons_tres`, the resource plugin copies partition/node usage,
projects termination of running jobs, and tests when the requested job fits.
It uses requested time limits for projected completions, and considers
applicable partition, reservation, and preemption constraints.

This is a resource-release projection, not a replay of every queued job's
future scheduling decisions. `job_scheduler.c` explicitly states that its
fresh test considers currently running jobs, whereas the stored backfill
start considers higher-priority pending jobs. For an existing pending job,
the returned start is no earlier than its stored backfill prediction. A new
hypothetical job lacks that cached queue-aware prediction, so its estimate
can be optimistic about waiting-job contention.

Source paths: `src/sbatch/sbatch.c`, `src/api/allocate.c`,
`src/slurmctld/proc_req.c`, `src/slurmctld/job_mgr.c`,
`src/slurmctld/job_scheduler.c`, and
`src/plugins/select/cons_tres/job_test.c`. The latter's `_future_run_test()`
copies resource state and removes running jobs from the copies as they are
projected to complete.

### Choosing an interface

| Interface | When predictions are computed | Coverage and limits | What the query changes |
| --- | --- | --- | --- |
| Slurm `squeue --start` | During prior backfill planning | Tested jobs; out-of-window estimates may exist; jobs beyond the test limit can be unknown | Reads stored job information |
| Slurm `sbatch --test-only` | During the request | One hypothetical job, using projected running-job releases and applicable constraints; does not replay queued starts | Performs a temporary allocation test; submits no lasting job |
| DR_EVT `get_estimated_job_starts()` | During prior live scheduling | Predictions actually computed; queue limits and early exits leave unknowns; out-of-horizon estimates may exist | Reads cached predictions and current pending membership |
| DR_EVT `get_job_statuses(ids, estimation_depth)` | Cached starts first; gaps during the request | Reuses live predictions and extends coverage with a conservative forecast; query depth is independent of live scheduler depth and horizon | Builds a temporary plan only for missing starts; live scheduler is unchanged |
| DR_EVT `get_exact_job_start_times(ids, estimation_depth)` | During the request | Replays the configured scheduler, including its ticks, depth, horizon, and known arrivals | Advances an isolated simulation clone |

Use the cached API to observe what the live planner last calculated. Use
`get_job_statuses()` for lifecycle information and broader cache-first forecasts.
These two interfaces can report different expected starts because they answer
different questions. Exact lookahead supplies a simulated continuation when
future scheduler decisions must be reproduced.

### Measured latency and accuracy

- Cached lookup reuses live backfill start estimates, but leaves unexamined
  jobs unknown and can retain stale or unprotected opportunities.
- Fresh-only forecasting allocates every queued job's full time-limit window
  in a temporary timeline. It is a conservative scenario estimate, rather
  than a replay of future scheduling passes, and is not a worst-case bound.
- Cache-first status reporting reuses live starts and forecasts missing starts.
  Cache-only requests skip forecasting; starts before current time are refreshed.
- Side simulation replays completion events and backfill decisions using
  estimated durations. Prediction must not know future actual runtimes or
  unsubmitted jobs; actual durations establish only observed history and the
  reference execution.

Event-driven hybrid (R=32/Q=64), 2,000 jobs, with actual runtimes having a
median of about 29% of time limits:

| Method | Coverage | µs per prediction | Common MAE (min) | Own MAE (min) |
| --- | ---: | ---: | ---: | ---: |
| Cached lookup | 6.13% | 0.138 | 106.73 | 106.73 |
| Fresh-only forecast | 100% | 0.258 | 155.88 | 547.40 |
| Cache-first extension | 100% | 0.271 | 112.36 | 544.74 |
| Side simulation, time-limit estimates | 100% | 8.754 | 155.88 | 647.25 |

When execution durations equal time limits:

| Method | Coverage | µs per prediction | Common MAE (min) | Own MAE (min) |
| --- | ---: | ---: | ---: | ---: |
| Cached lookup | 1.58% | 0.420 | 49.34 | 49.34 |
| Fresh-only forecast | 100% | 0.236 | 0.28 | 112.39 |
| Cache-first extension | 100% | 0.254 | 44.04 | 113.08 |
| Side simulation, time-limit estimates | 100% | 6.237 | 0 | 0.003 |

Cost is summed mean query time divided by finite returned predictions.
Common MAE uses jobs predicted by all four methods at the same checkpoints;
own MAE uses each method's predicted set. Actual-duration results have 573
common observations and 9,348 full-coverage observations; the control has 202
and 12,768 respectively. Unknown starts are excluded from both error measures.

Side simulation uses estimated unfinished durations and completion events; its
query cost includes preparing the input and an additional checkpoint clone.
Future submissions remain unknown in both conditions.

(choosing-how-much-computation-to-spend)=
### Choosing computation cost

With uncertain durations, cache-first is a reasonable inexpensive default.
Here, side simulation costs about 32× more per prediction and has higher
own-set error. With accurate durations, replay is substantially more accurate.
Use it when duration estimates are reliable or scheduler-policy fidelity matters.
These results establish neither a worst-case bound nor an accuracy threshold
for choosing replay.

See `validation/performance/estimated-starts/prediction-evaluation/COMPARISON.md`
for interpretation, `cache-first/NORMALIZED_RESULTS.md` for all ten
configurations, and `README.md` in that directory for reproduction.

## On-demand status behavior

`Simulation::get_job_statuses(job_ids, estimation_depth, use_cached_predictions)`
reuses live cached starts by default and fills gaps with a temporary
reservation-aware capacity projection. Cached starts before current time are
recomputed; other cached values can remain stale between periodic ticks.
Requests satisfied by the cache skip forecasting. For mixed requests, the
forecast stops after the last missing requested job, while still accounting
for earlier queue entries. Compatible cached protected placements reuse their
full-duration windows; unprotected point-fit opportunities are not blindly
carved from the fallback timeline because their full windows can overlap.
Setting `use_cached_predictions=false` selects the original entirely fresh
conservative forecast. The fallback does not replay future backfill decisions.

A pending result carries a start time
and a `reserved` or `estimated` kind: EASY's blocked in-horizon head, the
in-horizon hybrid reservation-depth prefix, and in-horizon conservative
blocked jobs are reported as `reserved`; other pending starts are estimates
that respect those retained windows. A zero `estimation_depth` is the default
and reports pending timing for the complete waiting queue. A finite depth
bounds this temporary projection to the reported prefix. It does not change
scheduler state or policy; exact effects from later admissible backfill remain
the responsibility of the separate full-lookahead design.

The cheap pending projection does not apply `--dispatch_interval`,
`--dispatch_on_events`, or `--dispatch_queue_depth`. In fixed periodic mode it can therefore forecast
resource availability before a permitted launch tick, or beyond the candidate
prefix for that tick. Lifecycle states and recorded actual starts remain
accurate. Use exact lookahead when estimates must follow the fixed periodic dispatch loop.

The projection is deliberately not a scheduler replay. It walks the queue in
order and carves every projected placement from its temporary profile before
placing the next job. An `estimated` result is therefore a serial queue-order
forecast, not a scheduler-protected reservation.
They use requested time limits for running and protected work: a completion
before its limit can advance a future estimate, while a later admissible
backfill choice can delay an unreserved estimate.  This is why `estimated` is
an optimistic opportunity rather than a guarantee, and why a scheduler-
consistent answer requires exact lookahead.

The query uses half-open resource intervals, so a forecast may reuse capacity
exactly at a prior placement's end. This differs from the live scheduler when
`DR_EVT_LEGACY_RESERVATION_BARRIER=ON`, which requires completion strictly
before a protected start. Pending forecast limits also bypass the scheduler's
`planning_time_resolution` grid. Exact lookahead follows the live scheduler
for both cases.

`Simulation::get_prediction_horizon(utilization)` is a separate existing
queue-level metric. It is currently available only to the EASY-capable FCFS
schedulers. It integrates queued resource-time demand against predicted
running-job releases after the FCFS head shadow time. It is an aggregate drain
metric, not a per-job start-time API.

## Reservation-aware model

For hybrid and conservative backfill, prediction begins with the same
reservation set that the scheduler protects in its current pass:

- EASY protects the blocked head.
- Hybrid protects the configured reservation-depth prefix.
- Conservative projects every in-horizon blocked waiting job.
- A job projected beyond `planning_horizon` creates no reservation and does
  not consume a reservation-depth slot.
- A backfill queue-depth limit constrains scheduling work in one pass. It does
  not limit the reservation projection: a query can project every waiting job.

The exact prefix and ordering must come from the scheduler's policy, not from
a separate interpretation in an API layer. This prevents status reporting from
drifting from the scheduling decision it describes.

For every waiting job, the cheap API reports a start-time projection and its
kind. A protected job reports `reserved` and its reservation time. A
non-protected job reports `estimated`, using the capacity profile remaining
after protected reservations. Non-protected estimates do not themselves create
reservations unless the caller explicitly requests an exact lookahead.

The caller can choose the reporting scope with `estimation_depth`: all waiting
jobs (zero, the default) or an explicit queue prefix. It is independent of the
scheduler's `backfill_queue_depth` and must not change scheduler policy.

## Exact lookahead

`Simulation::get_exact_job_start_times(job_ids, estimation_depth)` clones the
complete runnable simulation state into an in-memory binary buffer and drains
that clone through the ordinary scheduler event loop. It returns the cloned
jobs' actual start times with an explicit `started`, `rejected`, or
`capacity_unreachable` outcome. A non-empty list accepts appended-job IDs; an empty list enumerates
the current waiting-queue prefix, matching
`get_job_statuses()`. Zero depth selects the whole current prefix.
`estimation_depth` applies only to empty-list enumeration; explicitly
requested IDs are always returned.

The clone is synchronous and unbounded in this first version: it runs until
the requested starts are known or the known work drains. Its snapshot uses no
filesystem checkpoint and does not flush live CSV output, write Redis state,
or trigger automatic checkpoint output. The snapshot buffer is thread-local,
reused between calls, and preallocated from the live state size estimate.

An exact estimate can be produced by forking the current simulation in memory
and advancing the fork with the ordinary event loop and scheduler. The fork
has no job-output, resource-output, Redis, or checkpoint destination. It holds
only the jobs known at query time, so its trace and wait-queue capacities can
be bounded by the current running, waiting, and known future-arrival
population.

The fork is discarded after requested jobs start or the known queue drains.
Because it uses the real scheduler, it naturally respects reservation depth,
queue depth, planning horizon, periodic backfill ticks, capacity changes, and
the state inherited by a completed warm start. It does not change the live
simulation. Its cost is proportional to how far it advances, so it is suitable
for accurate or bounded queries rather than every monitoring poll.

In particular, a fork admits later backfill candidates exactly as the live
scheduler would. Those candidates can consume capacity and change an earlier
unreserved job's eventual start; the cheap first-fit query intentionally does
not revise already reported estimates for that effect.

The existing public checkpoint restart path is not itself the fork mechanism:
it validates output identities and manages restart output segments. A private
in-memory fork must restore scheduling state without acquiring or modifying
output ownership.

## Fast queue-drain estimate

Experiments also need a cheap estimate for draining all currently known
waiting work. This is distinct from the exact lookahead and should have its
own API.

Let the temporary residual capacity profile be:

```text
available(t) = capacity(t) - running_allocation(t) - reservation_allocation(t)
```

`running_allocation` includes predicted releases of live jobs. `reservation_allocation`
contains only protected reservations, over each reservation's projected run
window. Capacity changes are profile events as well.

Let the unreserved queue demand be:

```text
A_unreserved = sum(nodes_i * estimated_runtime_i)
```

over waiting jobs that do not already occupy a protected reservation window.
With an experiment-supplied utilization factor `U` in `(0, 1]`, integrate:

```text
U * integral(available(t) dt)
```

until it covers `A_unreserved`. The full queue-drain horizon is the later of
that time and the latest completion of a protected reservation. This avoids
double-counting: reserved work is represented by its window in the profile and
is excluded from `A_unreserved`.

The utilization factor models fragmentation and scheduling losses; it is not
instantaneous machine utilization. A value of `1` makes the calculation an
optimistic capacity-area estimate. This method is fast because it sweeps only
running-release, reservation, and capacity-change events. It does not claim
that each individual job has a contiguous feasible interval.

## Per-job fast estimates

`Simulation::get_queue_drain_start_times(utilization, estimation_depth)`
reuses the same residual profile. For an
unreserved job `j`, drain the unreserved work ahead of `j` in scheduler order,
without counting protected jobs a second time. The resulting time estimates
when `j` may start. A stronger but still inexpensive variant scans profile
intervals for a contiguous window with at least `j`'s requested nodes for its
whole estimated runtime.

Neither variant is a reservation: other unreserved jobs can receive overlapping
optimistic estimates. Callers needing policy-exact start times should request
the output-free lookahead instead.

## API contract

The APIs have distinct cost and purpose:

| API | Purpose | Cost |
| --- | --- | --- |
| `get_fcfs_head_shadow_time()` | Core scheduler head-reservation state | Existing scheduler state |
| `get_backfill_window()` | Core FCFS/EASY capacity and release snapshot | On-demand snapshot |
| `get_estimated_job_starts()` | Cached live-planner starts, following the squeue reporting model | Pending-job enumeration and cached lookup/sort; no forecasting |
| `get_job_statuses()` | Common cheap per-job start-time reporting for EASY, hybrid, and conservative | Profile sweep over selected queue prefix |
| `get_queue_drain_start_times(utilization, estimation_depth)` | Fast per-job resource-area start approximation | Resource-area sweep over selected queue prefix |
| `get_prediction_horizon(utilization)` | Aggregate experimental queue-drain horizon | Resource-area event sweep |
| `get_exact_job_start_times()` | Policy-exact per-job starts | Output-free in-memory replay |

`get_job_statuses()` remains the common lifecycle-status API. Its pending-job
result will identify whether its reported start time is `reserved` (from the
scheduler's reservation projection) or `estimated` (from the
reservation-aware profile). Existing scheduled jobs continue to return their
actual start and end times.

The query's estimation-depth option controls how many waiting jobs
receive pending start-time results, while preserving a status record for every
requested ID. A depth of zero means the complete waiting queue and is the
default; a finite depth limits reported estimates to that queue prefix. The
option is an API output/work bound only: it never changes scheduler
configuration, backfill policy, or the reservation plan used as an input to
the calculation.

The exact-lookahead operation is a separate API because its side simulation is
materially more expensive. It returns policy-exact starts rather than reusing
the cheap API's `reserved`/`estimated` classification.

`get_prediction_horizon()` remains the separate queue-drain API for
experiments. Its existing EASY behavior remains compatible. Its
hybrid/conservative extension should expose enough metadata to interpret the
area calculation, at least the horizon, utilization factor, unreserved
resource-time area, protected reservation count, and latest reservation
completion.

## Validation plan

Tests should cover the following independently:

1. A protected job reports its reservation rather than a generic estimate.
2. An unreserved job's estimate respects every protected reservation.
3. The zero estimation-depth default covers all waiting jobs; a finite depth
   suppresses only pending start-time results outside the returned prefix.
4. Hybrid depth, queue-depth limits, and horizon-skipped jobs select the same
   protected set as the scheduler.
5. Exact lookahead start times match an output-suppressed normal simulation.
6. Queue-drain area accounting does not double-count reserved work and reacts
   to running releases and capacity changes.
7. No prediction query mutates live scheduling state or creates output.

Status, cached-start, queue-drain, EASY prediction-horizon, and exact-lookahead
tests cover the implemented APIs. Cached-start tests distinguish an unknown
prediction from an on-demand forecast, and cover queue-depth suppression,
out-of-horizon estimates, periodic freshness, and checkpoint invalidation.
