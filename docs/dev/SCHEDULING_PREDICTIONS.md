# Scheduling Predictions and Reservations

## Status and scope

This document records the design for exposing scheduler timing information to
streaming clients and experiments. It separates a scheduler reservation from a
start-time estimate and from an aggregate queue-drain estimate. The common
cheap per-job query and resource-area per-job approximation are implemented;
exact lookahead remains design work.

The design must not add persistent prediction state or work to the normal
scheduling path. Prediction work is performed only when a caller asks for it.

## Terms

| Term | Meaning |
| --- | --- |
| Reservation | A start time protected by the scheduler's current backfill policy. Backfill work must not delay it. |
| Start-time estimate | A non-binding prediction for one waiting job. It can change after a later scheduling pass, arrival, or capacity change. |
| Lookahead | An output-free fork of the current simulation that runs the real scheduler forward. It provides the most faithful estimates. |
| Queue-drain horizon | An aggregate estimate of when the currently known waiting work will drain. It is intended for experiments, not as a job-start guarantee. |

A protected reservation is authoritative for that job's reported planned start;
there is no reason to replace it with a weaker estimate. Jobs without a
reservation may still receive an estimate, but that estimate must respect the
capacity claimed by protected reservations.

## Current behavior

`Simulation::get_job_statuses(job_ids, estimation_depth)` builds a temporary
reservation-aware capacity projection. A pending result carries a start time
and a `reserved` or `estimated` kind: EASY's blocked in-horizon head, the
in-horizon hybrid reservation-depth prefix, and in-horizon conservative
blocked jobs are reported as `reserved`; other pending starts are estimates
that respect those retained windows. A zero `estimation_depth` is the default
and reports pending timing for the complete waiting queue. A finite depth
bounds this temporary projection to the reported prefix. It does not change
scheduler state or policy; exact effects from later admissible backfill remain
the responsibility of the separate full-lookahead design.

The projection is deliberately not a scheduler replay. It walks the queue in
order and carves every projected placement from its temporary profile before
placing the next job. An `estimated` result is therefore a serial queue-order
forecast, not a scheduler-protected reservation.
They use requested time limits for running and protected work: a completion
before its limit can advance a future estimate, while a later admissible
backfill choice can delay an unreserved estimate.  This is why `estimated` is
an optimistic opportunity rather than a guarantee, and why a scheduler-
consistent answer requires exact lookahead.

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

A later per-job approximation can reuse the same residual profile. For an
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
| `get_job_statuses()` | Common cheap per-job start-time reporting for EASY, hybrid, and conservative | Profile sweep over selected queue prefix |
| `get_prediction_horizon(utilization)` | Aggregate experimental queue-drain horizon | Resource-area event sweep |
| Lookahead API (new) | Policy-exact per-job starts | Output-free side simulation |

`get_job_statuses()` remains the common lifecycle-status API. Its pending-job
result will identify whether its reported start time is `reserved` (from the
scheduler's reservation projection) or `estimated` (from the
reservation-aware profile). Existing scheduled jobs continue to return their
actual start and end times.

The query gains an estimation-depth option. It controls how many waiting jobs
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

The existing status and EASY prediction-horizon tests provide starting
coverage, but new tests are required before enabling the proposed APIs.
