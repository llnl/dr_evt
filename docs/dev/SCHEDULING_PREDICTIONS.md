# Scheduling Predictions and Reservations

## Design

DR_EVT provides several ways to report future job starts: read live planning
estimates, forecast missing starts, approximate queued resource-time demand,
or replay the scheduler in an isolated simulation. The APIs expose the cost
and uncertainty of each method rather than treating every estimate as a
protected reservation.

Cached queries reuse normal scheduling work. On-demand queries leave live
state unchanged. Replay follows the configured policy, but can predict only
from information supplied to its input state.

## Terms

| Term | Meaning |
| --- | --- |
| Reservation | A start time protected by the scheduler's current backfill policy. Backfill work must not delay it. |
| Start-time estimate | A non-binding prediction for one waiting job. It can change after a later scheduling pass, arrival, or capacity change. |
| Lookahead | An isolated continuation using the configured scheduler. It reproduces scheduling behavior; prediction accuracy depends on its inputs. |
| Queue-drain horizon | An aggregate estimate of when the currently known waiting work will drain. It is intended for experiments, not as a job-start guarantee. |

A reservation protects a placement under the current plan. An estimate can
exist without that protection. Neither is a guarantee against later arrivals,
capacity changes, or inaccurate duration estimates.

(scheduler-public-interface-behavior)=
## Production scheduler reporting: Slurm and Flux

The following describes observable behavior and prediction methods at a high
level. It does not compare measured Slurm or Flux query performance with DR_EVT.

The discussion uses these version baselines:

| Component | Version | Revision |
| --- | --- | --- |
| Slurm | 26.11.0-0rc2 | `17abb848e200` |
| Flux Core | 0.91.0 development, 35 commits after v0.91.0 | `d0309fcf0e67` |
| Fluxion (`flux-sched`) | 0.57.0 | `9eba598b4e09` |

Slurm manuals: [`squeue`](https://slurm.schedmd.com/squeue.html) and
[`sbatch`](https://slurm.schedmd.com/sbatch.html).

### `squeue --start`

Displays cached expected start times and expected nodes for pending jobs.
Backfill planning computes these estimates from projected resource availability,
requested time limits, and protection for earlier waiting jobs. The command
reports available estimates rather than forecasting every missing start on
demand. Estimates can be stale, change after replanning, or remain unknown.

Planning limits affect the available information:

| Limit | Effect on expected starts |
| --- | --- |
| Reservation budget (reservation depth) | Limits protected future placements. More jobs can have estimates than have reservations; an estimate alone does not protect a start. |
| Queue examination depth | Limits which pending jobs are tested during planning. Unexamined jobs can have no cached estimate; the reporting query does not fill those gaps. |
| Planning horizon | Bounds future reservation planning. An examined job can have an estimate beyond the horizon without a protected reservation. |

These limits govern planning coverage and protection, not the number of rows
the command may return. They are planning concepts; DR_EVT's depth and horizon
options do not map one-to-one to Slurm's configuration controls.

### `sbatch --test-only`

Validates a proposed batch job and requests a fresh estimate without submitting
it. At a high level, the estimate is the earliest projected time when enough
resources fit the job, considering running jobs' expected releases and
applicable partition, reservation, and preemption constraints. Requested time
limits provide estimated completion times for running jobs.

For a new hypothetical job, this resource-release projection does not replay
the waiting queue and place every earlier job ahead of it. The result can
therefore be optimistic about waiting-job contention. It is not equivalent to
the queue-aware cached estimate for an existing pending job, and does not
guarantee a start time. Backfill scan depth and horizon should not be interpreted
as response-coverage limits for this single-job request.

### Flux scheduler annotations

Flux exposes scheduler-provided metadata through job annotations. When an
estimated start is available, clients can inspect `sched.t_estimate`, for
example through `flux jobs --format='{id} {sched.t_estimate}'`. Listing jobs
reports the available annotation; it does not promise an estimate for every
waiting job or replay the queue to fill missing values.

Fluxion backfill policies compute future placements and protect the waiting
jobs selected by the policy: the head for EASY, a bounded set for hybrid, and
a broader set for conservative. Queue depth limits examined work; reservation
depth limits protected placements. Repeated planning passes can change which
jobs have estimates, so these limits are not a fixed count of reportable rows.

Fluxion's estimates reflect its concrete resource model, which can differ from
DR_EVT's aggregate-count model. See the [Flux validation](../user-guide/FLUX_VALIDATION.md)
for the measured scheduling differences, and the public
[`flux jobs` manual](https://flux-framework.readthedocs.io/projects/flux-core/en/latest/man1/flux-jobs.html)
and [Fluxion queue-policy configuration](https://flux-framework.readthedocs.io/projects/flux-sched/en/latest/man5/flux-config-sched-fluxion-qmanager.html).

## DR_EVT APIs and the gaps they address

Relative to the reporting interfaces above, cached estimates can leave jobs
unknown, and a hypothetical resource-fit test need not account for the full
waiting workload. DR_EVT offers explicit alternatives for broader coverage,
queue-aware forecasts, and scheduler replay. It also exposes reservation and
queue-drain information for experiments.

| API | Purpose | Gap addressed |
| --- | --- | --- |
| [get_start_times_by_est_caches()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation29get_start_times_by_est_cachesEv) | Report estimates from live planning | Provides estimate age and distinguishes unknown, estimated, and reserved starts. |
| [get_job_statuses()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation16get_job_statusesERKNSt6vectorI8job_no_tEE6size_tbb) | Reuse cached starts and forecast gaps | Extends sparse cached coverage while accounting for earlier queued work, without full replay. |
| [get_start_times_by_queue_drain()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation30get_start_times_by_queue_drainEd6size_t) | Estimate per-job starts from resource-time area | Offers a coarse alternative to per-job window placement or replay. |
| [get_start_times_by_lookahead()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation28get_start_times_by_lookaheadERKNSt6vectorI8job_no_tEE6size_t) | Run an isolated scheduler continuation | Includes future scheduling passes and backfill effects absent from a simple resource-release projection. |
| [estimate_queue_horizon()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation22estimate_queue_horizonEd) | Report an aggregate EASY queue-drain metric | Summarizes remaining workload rather than promising individual starts. |
| [get_fcfs_head_shadow_time()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation25get_fcfs_head_shadow_timeEv) / [get_backfill_window()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation19get_backfill_windowEv) | Inspect head protection and current capacity/releases | Makes the reservation inputs available for policy experiments and diagnostics. |

The cached, status, aggregate-horizon, and reservation-snapshot APIs are exposed
in C++, Python, and gRPC. See the
[Python API](../api/PYTHON_API.md) and [streaming API](../api/STREAMING_API.md)
for language-specific signatures and response fields. The API names link
to the generated C++ parameter and return-value documentation. The gRPC
operations below are request/response pairs carried by
[SimulationService.Session](../CLIENT_SERVER_GUIDE.md#the-service-definition); their links describe
the wire fields.

## API behavior

### Cached start estimates: [get_start_times_by_est_caches()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation29get_start_times_by_est_cachesEv)

The gRPC request and response are documented in
[GetStartTimesByEstCachesRequest / GetStartTimesByEstCachesResponse](../CLIENT_SERVER_GUIDE.md#cached-start-estimates).

Returns `Estimated_Job_Starts` containing:

- `estimated_at`: the last planning timestamp; absent before planning and
  immediately after checkpoint restore.
- `jobs`: eligible pending jobs ordered by expected start, with unknown starts
  last in queue order. Future arrivals, running jobs, and completed jobs are excluded.
- Each job's `job_idx`, optional `expected_start_time`, and `start_time_kind`
  (`NONE`, `RESERVED`, or `ESTIMATED`).

The query reads existing predictions without forecasting or advancing time.
EASY generally predicts the blocked head; hybrid and conservative retain the
future fits they calculate. Queue limits and early exits can leave unknowns.
An out-of-horizon fit may be estimated without protection. The latest recorded
value for each job wins when one wake performs several passes.

Estimates can be stale between planning ticks and are not clamped to current
time. Newly appended jobs remain unknown until planning examines them. The
cache is transient and is not serialized into checkpoints.

Storage is a sparse reusable vector: 24 bytes per record and 40 fixed bytes on
a typical 64-bit build, with no extra fields in waiting-queue entries. Query
cost is O(N + P + K log K), for N examined queue entries, P cached records, and
K known results. Only known results are sorted; all-unknown responses skip
sorting and all-known responses skip partitioning. Live planning adds cache
writes, not additional resource searches.

### Reservation-aware forecasts

This temporary forecast model is used by [get_job_statuses()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation16get_job_statusesERKNSt6vectorI8job_no_tEE6size_tbb) when it needs
missing starts, and by [get_start_times_by_queue_drain()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation30get_start_times_by_queue_drainEd6size_t). Both account for
running-job releases, capacity changes, and protected windows. Cached protected
windows are reused only when the selected cache mode allows it; otherwise the
status forecast recomputes policy reservations. Cached lookup
reads live planning results instead; lookahead executes the scheduler itself.

Protection follows the selected backfill policy: EASY's blocked head, hybrid's
reservation-depth budget, or conservative's in-horizon blocked jobs.
Out-of-horizon fits create no reservation and consume no reservation-depth slot.
The query's `estimation_depth` sets its reporting prefix independently of live
backfill examination depth; it does not change scheduling.

#### Job status and start estimates: [get_job_statuses()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation16get_job_statusesERKNSt6vectorI8job_no_tEE6size_tbb)

The gRPC request and response are documented in
[GetJobStatusesRequest / GetJobStatusesResponse](../CLIENT_SERVER_GUIDE.md#job-status-forecasts).

[get_job_statuses()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation16get_job_statusesERKNSt6vectorI8job_no_tEE6size_tbb) reports
lifecycle state and start/end timing. It reuses cached starts by default,
refreshes starts before current time, and forecasts missing starts. With default
cache reuse, cache-only requests skip forecasting. Small requests stop after
the last requested job needing a forecast while accounting for earlier queue
entries. Protected-only reuse first checks and installs cached reservation
windows, including those beyond the reporting prefix.

The fallback builds a temporary capacity profile from running-job releases,
scheduled capacity changes, and protected reservation windows. It places each
queued job's full time-limit window before placing the next. Compatible cached
reservations reuse their full windows; unprotected point-fit opportunities are
not blindly carved from the timeline because their full windows can overlap.
Cache reuse has three modes:

| Parameters | Protected reservations | Unprotected estimates |
| --- | --- | --- |
| Defaults: both cache flags `true` | Reuse cached starts | Reuse cached starts where available; forecast gaps |
| `use_cached_predictions=true`, `use_cached_unprotected_predictions=false` | Preserve compatible cached windows | Recompute against those windows |
| `use_cached_predictions=false` | Recompute | Recompute |

The second flag is ignored when cache reuse is disabled. The default reuses
unprotected estimates as well as reservations; an unprotected cached start is
not a full queue-aware forecast. In protected-only mode, past or incompatible
reservation windows are recomputed. Reused windows are installed before any
unprotected job is placed, so those jobs cannot displace the reservations.

Disabling cache reuse recomputes reserved starts too. Recomputing can change
those values because it builds a new plan from current state. However, a cached
reservation often reflects the earliest fit after running jobs finish and
earlier protected jobs are accounted for. The forecast can find the same fit
without simulating future backfill, so omitting backfill does not always
produce a noticeable difference.

Pending results are `RESERVED` or `ESTIMATED`. A protected forecast uses the
selected reservation; other forecast starts respect those windows. The
fallback does not replay future backfill admissions. Early completions can
advance starts, while later backfill decisions can delay unprotected starts.
Neither the forecast without backfill nor a reused cached estimate is a guaranteed
worst-case bound.

The fallback also omits fixed-dispatch ticks and candidate limits, so it can
predict resource availability before an allowed launch. It uses half-open
intervals and unrounded pending time limits, unlike live scheduling with
`DR_EVT_LEGACY_RESERVATION_BARRIER=ON` or a planning-time grid. Use replay when
those policy and boundary details matter.

#### Resource-area start estimates: [get_start_times_by_queue_drain()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation30get_start_times_by_queue_drainEd6size_t)

This API is available in C++ and Python; it has no gRPC counterpart.

Computes start approximations from cumulative resource-time area. Protected
jobs return their reservation; each unreserved job is assigned the time at
which usable free-resource area covers the unreserved work ahead of it.

```text
available(t) = capacity(t) - running_allocation(t) - reservation_allocation(t)
U * integral(available(t) dt) >= unreserved node-time ahead of the job
```

Running releases and capacity changes are profile events. Reserved windows are
removed from available capacity and reserved jobs are excluded from unreserved
demand, avoiding double-counting. `U` models usable capacity and scheduling
losses; it is not instantaneous utilization. It accepts values in (0, 1], with
zero selecting 1.

This area approximation does not ensure a contiguous feasible window for each
job or reproduce later backfill choices. Unreserved estimates can overlap and
are not reservations. The stronger contiguous-window forecast is provided by
[get_job_statuses()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation16get_job_statusesERKNSt6vectorI8job_no_tEE6size_tbb), rather than this area calculation.

### Start times by lookahead: [get_start_times_by_lookahead()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation28get_start_times_by_lookaheadERKNSt6vectorI8job_no_tEE6size_t)

This API is available in C++ and Python when serialization is enabled; it has
no gRPC counterpart.

Clones runnable state into a reusable in-memory buffer and advances the copy
through ordinary scheduler events. It returns start times with `started`,
`rejected`, or `capacity_unreachable` outcomes. Replay follows reservation
depth, queue depth, horizon, dispatch/backfill ticks, capacity changes, and
inherited warm-start state.

The call is synchronous and has no forecast-time budget: it runs until the
requested starts are known or the supplied workload drains. The isolated copy
has no CSV, resource-trace, Redis, or automatic checkpoint output destinations.
It does not acquire live output ownership through the public checkpoint-restart
path.

Replay uses the durations and arrivals present in its input state. It does not
automatically remove unknown future information. For production-style
predictions, provide only observed jobs and estimate unfinished durations and
completion events; the performance evaluation below uses such an adapter.
Accuracy therefore depends on duration estimates even though scheduler
behavior is faithfully reproduced. Cost grows with the events replayed.

### Aggregate queue horizon: [estimate_queue_horizon()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation22estimate_queue_horizonEd)

The gRPC request and response are documented in
[EstimateQueueHorizonRequest / EstimateQueueHorizonResponse](../CLIENT_SERVER_GUIDE.md#aggregate-queue-horizon).

The existing aggregate metric is available only for EASY-capable FCFS
schedulers using EASY backfilling. Call it after scheduling at the current
time. It integrates waiting resource-time demand against predicted releases
following the FCFS head shadow time and returns a duration from that shadow,
not an absolute per-job start time. Zero utilization selects a factor of 1.

A hybrid/conservative aggregate extension is not implemented. The per-job
resource-area API above already supports those reservation policies.

### Reservation snapshots

[get_fcfs_head_shadow_time()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation25get_fcfs_head_shadow_timeEv)
returns the current FCFS head's projected start, or -1 when no head waits or
no projected capacity change lets it start.

[get_backfill_window()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation19get_backfill_windowEv)
returns `Backfill_Window`: current time, available nodes, head shadow time,
and ordered running-job release events. Releases use requested-limit
estimates; the head projection also accounts for scheduled capacity changes.
These snapshots expose planning inputs rather than forecasting the whole queue.
Their gRPC counterparts are documented in
[GetFCFSHeadShadowTimeRequest / GetFCFSHeadShadowTimeResponse](../CLIENT_SERVER_GUIDE.md#head-shadow-time)
and [GetBackfillWindowRequest / GetBackfillWindowResponse](../CLIENT_SERVER_GUIDE.md#backfill-window).

## API contract

### Request scope and results

For [get_job_statuses()](https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4NK6dr_evt15BasicSimulation16get_job_statusesERKNSt6vectorI8job_no_tEE6size_tbb), explicit IDs are appended-job IDs and preserve request
order and duplicates. An empty list enumerates waiting jobs in scheduler order.
Zero `estimation_depth` covers the full queue; a finite depth limits pending
timing to that prefix, while explicit requested IDs retain status records.
Running and completed jobs report recorded start/end times; depth affects only
pending estimates.

Exact lookahead uses the same empty-list enumeration. Its depth applies only
to that enumeration; explicitly requested IDs are always returned. Cached
lookup has no query-depth setting, and resource-area estimates enumerate the
selected waiting prefix. These query limits never alter live planning limits.

### Correctness requirements

- Forecasted protected jobs report their reservations; other forecast estimates
  respect those windows. Cached values retain the age and protection of their source pass.
- Reservation selection follows policy, reservation depth, and horizon.
- Replay matches a normal continuation given the same input information.
- Resource-area accounting counts reserved work once and incorporates releases
  and capacity changes.
- Prediction queries leave live state unchanged and create no simulation output.

## Validation coverage

Status, cached-start, resource-area, EASY horizon, and exact-lookahead tests
cover the implemented APIs. Cached-start tests cover unknowns versus forecasts
without backfill, queue-depth suppression, out-of-horizon estimates, periodic
freshness, ordering, and checkpoint invalidation. Replay tests compare with
independent continuations and check that query calls preserve live state.

## Performance and accuracy comparison

These measurements compare DR_EVT APIs and prediction methods. The production
scheduler discussion above describes behavior, not benchmark results.

Cost is summed mean query time divided by finite returned predictions.
Mean Absolute Error (MAE) measures the absolute difference between predicted
and reference start times, averaged over job/checkpoint observations. Common
MAE uses jobs predicted by all four methods at the same checkpoints;
own MAE uses each method's predicted set. Actual-duration results have 573
common observations and 9,348 full-coverage observations; the control has 202
and 12,768 respectively. Unknown starts are excluded from both error measures.

Side simulation uses estimated unfinished durations and completion events; its
query cost includes preparing the input and an additional checkpoint clone.
Future submissions remain unknown in both conditions.

### Actual execution durations

Event-driven hybrid (R=32/Q=64), 2,000 jobs, with actual runtimes having a
median of about 29% of time limits:

| Method | Coverage | µs per prediction | Common MAE (min) | Own MAE (min) |
| --- | ---: | ---: | ---: | ---: |
| Cached lookup | 6.13% | 0.138 | 106.73 | 106.73 |
| Forecast without backfill | 100% | 0.258 | 155.88 | 547.40 |
| Cache-first extension | 100% | 0.271 | 112.36 | 544.74 |
| Side simulation, time-limit estimates | 100% | 8.754 | 155.88 | 647.25 |

### Execution duration equals time limit

| Method | Coverage | µs per prediction | Common MAE (min) | Own MAE (min) |
| --- | ---: | ---: | ---: | ---: |
| Cached lookup | 1.58% | 0.420 | 49.34 | 49.34 |
| Forecast without backfill | 100% | 0.236 | 0.28 | 112.39 |
| Cache-first extension | 100% | 0.254 | 44.04 | 113.08 |
| Side simulation, time-limit estimates | 100% | 6.237 | 0 | 0.003 |

The **0.28 versus 49.34 minutes** comparison uses the same 202 job/checkpoint
observations. Of the cached predictions, 96 protected reservations have zero
error; 106 unprotected estimates have 94.02 minutes MAE, producing the combined
49.34 minutes. Unprotected cached estimates record when enough nodes become
available, without placing every earlier waiting job's full execution window.
They can therefore predict starts too early. That behavior is consistent with
Slurm’s `squeue --start`. The forecast without backfill
places those windows in queue order; with durations equal to time limits, this
closely matches execution for this common set. Cached lookup reuses planning
results, but does not provide a complete forecast of future backfill decisions.

Cache-first reuses accurate protected reservations **and** unprotected cached
estimates. In this control, the latter account for its 44.04-minute common-set
MAE, versus 0.28 minutes when cache reuse is disabled. It refreshes cached starts
before the query time, reducing error from the cache-only result of 49.34 minutes,
but retains other optimistic estimates. Accurate reservations therefore do not
make the entire cache accurate. The protected-only cache option avoids reusing
those unprotected estimates; the tables above measure the original default
cache-first mode, not this option.

The forecast without backfill assumes jobs occupy resources for their full time
limits. When jobs finish earlier, it holds resources too long in its temporary
timeline, and this error accumulates as it places later jobs. When execution
duration equals the time limit, that duration assumption matches the reference
run: own-set MAE falls from 547.40 to 112.39 minutes in these experiments.
Residual error remains because the forecast does not replay future backfill
admissions and dispatch rules. The two conditions have different observation
sets; this is not a paired comparison of identical job/checkpoint observations.

(choosing-how-much-computation-to-spend)=
### Choosing computation cost

With uncertain durations, cache-first is a reasonable inexpensive default.
Here, side simulation costs about 32× more per prediction and has higher
own-set error. With accurate durations, replay is substantially more accurate.
Use it when duration estimates are reliable or scheduler-policy fidelity matters.
These results establish neither a worst-case bound nor an accuracy threshold
for choosing replay.

See [full results for all ten configurations](https://github.com/LLNL/dr_evt/blob/main/validation/performance/estimated-starts/prediction-evaluation/cache-first/NORMALIZED_RESULTS.md),
[the concise comparison](https://github.com/LLNL/dr_evt/blob/main/validation/performance/estimated-starts/prediction-evaluation/COMPARISON.md),
and [reproduction instructions](https://github.com/LLNL/dr_evt/blob/main/validation/performance/estimated-starts/prediction-evaluation/README.md).
