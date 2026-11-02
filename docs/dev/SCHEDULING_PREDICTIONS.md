# Scheduling Predictions and Reservations

## Status and scope

This document records the design for exposing scheduler timing information to
streaming clients and experiments. It separates a scheduler reservation from a
start-time estimate and from an aggregate queue-drain estimate. The proposed
hybrid/conservative extensions described below are not implemented yet.

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

`Simulation::get_job_statuses()` currently creates a temporary earliest-fit
capacity projection for all pending jobs. It has no persistent sidecar state
and is not restricted by `backfill_queue_depth`, hybrid reservation depth, or
`planning_horizon`. This makes it useful as an all-queue forecast, but it does
not presently expose a distinct reservation result or replay the full
hybrid/conservative policy.

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
- Conservative protects the applicable waiting prefix.
- A job projected beyond `planning_horizon` creates no reservation and does
  not consume a reservation-depth slot.
- A backfill queue-depth limit constrains the pass that identifies protected
  jobs; it must not suppress an estimate for a deeper waiting job.

The exact prefix and ordering must come from the scheduler's policy, not from
a separate interpretation in an API layer. This prevents status reporting from
drifting from the scheduling decision it describes.

For every protected job, the API should report its reservation time. For a
non-protected job, the estimator uses the capacity profile remaining after
protected reservations. Non-protected estimates do not themselves create
reservations unless the caller explicitly requests an exact lookahead.

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

## Proposed API shape

The public API should make the result kind explicit rather than requiring a
caller to infer it from a timestamp:

| Result kind | Source | Intended use |
| --- | --- | --- |
| `reserved` | Scheduler's protected reservation | Planned start visibility |
| `lookahead` | Output-free side simulation | Policy-exact start prediction |
| `estimated` | Reservation-aware profile | Low-cost per-job estimate |
| `queue_drain` | Reservation-aware area integration | Experimental aggregate horizon |

The existing `get_prediction_horizon()` should remain compatible as the
EASY queue-drain calculation. A new or generalized queue-drain API should
return enough metadata to interpret an experimental result, at least the
horizon, utilization factor, unreserved resource-time area, protected
reservation count, and latest reservation completion.

## Validation plan

Tests should cover the following independently:

1. A protected job reports its reservation rather than a generic estimate.
2. An unreserved job's estimate respects every protected reservation.
3. Hybrid depth, queue-depth limits, and horizon-skipped jobs select the same
   protected set as the scheduler.
4. Exact lookahead start times match an output-suppressed normal simulation.
5. Queue-drain area accounting does not double-count reserved work and reacts
   to running releases and capacity changes.
6. No prediction query mutates live scheduling state or creates output.

The existing status and EASY prediction-horizon tests provide starting
coverage, but new tests are required before enabling the proposed APIs.
