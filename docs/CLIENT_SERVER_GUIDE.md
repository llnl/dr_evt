# gRPC Client/Server Guide and API

## Overview

DR_EVT's [streaming API](api/STREAMING_API.md) ([`append_job()`][cpp-simulation-append-job]/[`append_jobs()`][cpp-simulation-append-jobs],
[`advance_to()`][cpp-simulation-advance-to], and the monitoring/statistics methods) lets
external code feed genuinely new jobs incrementally and control simulation
time advancement, rather than loading a full trace and running it
start-to-finish in one call. The gRPC client/server exposes that same
streaming API over the network: a `dr_evt_server` process holds one
[`Simulation`][cpp-simulation] instance per connected session, and any number of
`dr_evt_client` processes (or your own gRPC client, in any language gRPC
supports) can drive it remotely.

This is separate from, and does not replace, the plain CLI `simulator`
binary (batch mode) or the Python bindings (in-process streaming API). Use
the gRPC client/server specifically when the thing feeding jobs needs to
run in a different process - or on a different machine - than the
simulation itself.

Build and connection instructions are in
[Client/Server Setup](user-guide/grpc-setup.md). Dependency discovery is
documented in [Installation](getting-started/installation.md).

(grpc-service-definition)=
## The service definition

`src/proto/dr_evt_service.proto` defines a single bidirectional-streaming
RPC (`SimulationService.Session`), wrapping the same operations available
in-process via the streaming API:

| Request | Corresponds to |
|---|---|
| `InitRequest` | Constructing a [`Simulation`][cpp-simulation] from a [`Sim_Params`][cpp-sim-params]-equivalent config |
| `RunRequest` | Calls [`Simulation::run()`][cpp-simulation-run]; initialized input and `sim_start_time` select simulation, full replay, or replay-based warm start, and configured `max_time` supplies an inclusive stopping boundary |
| `InitializeTraceRequest` | [`Simulation::initialize_trace()`][cpp-simulation-initialize-trace] |
| `AppendJobRequest` | [`Simulation::append_job()`][cpp-simulation-append-job] - a genuinely new job, optionally including its known `actual_run_time` |
| `AppendJobsRequest` | [`Simulation::append_jobs()`][cpp-simulation-append-jobs] - the batch counterpart, with an optional known runtime per job |
| [GetJobStatusesRequest](#job-status-forecasts) | Query lifecycle and timing for IDs returned by the append APIs |
| [GetStartTimesByEstCachesRequest](#cached-start-estimates) | Read cached start estimates with their planning timestamp |
| `AdvanceToRequest` | [`Simulation::advance_to()`][cpp-simulation-advance-to] |
| `RunUntilExclusiveRequest` | [`Simulation::run_until_exclusive()`][cpp-simulation-run-until-exclusive] |
| [GetFCFSHeadShadowTimeRequest](#head-shadow-time) | FCFS-head shadow time only: the earliest reserved start time, or `-1` with no waiting head |
| [GetBackfillWindowRequest](#backfill-window) | One FCFS/EASY reservation snapshot: current capacity, shadow time, and projected releases |
| [EstimateQueueHorizonRequest](#aggregate-queue-horizon) | On-demand FCFS/EASY waiting-resource-time horizon after the shadow time |
| `SaveCheckpointRequest` | Return the current simulation state as binary checkpoint bytes |
| `LoadCheckpointRequest` | Replace the current simulation state from compatible checkpoint bytes |
| `GetStatisticsRequest`, `GetCurrentTimeRequest`, etc. | The monitoring/statistics methods |

(grpc-job-statuses)=
### Job status forecasts

`GetJobStatusesRequest` returns `GetJobStatusesResponse.jobs`, a list of
`JobStatus` records. `GetJobStatusesRequest.job_idx` accepts multiple appended-job IDs and returns
one `JobStatus` per ID in the same order. A pending job carries
`expected_start_time`; a running or completed job carries `scheduled` with
`start_time` and `end_time`; and a rejected job carries no timing value.
The request reuses cached live start predictions by default and forecasts
missing starts. Set the optional `use_cached_predictions` field to `false`
for a forecast without backfill that recomputes all pending starts, including
reservations; absent or `true` enables reuse.
The optional `use_cached_unprotected_predictions` field defaults to `true`.
With cache reuse enabled, set it to `false` to retain compatible protected
reservation windows and forecast unprotected jobs. It is ignored when
`use_cached_predictions=false`.
`expected_start_time` is paired with a `start_time_kind`: `reserved` is a
protected scheduler reservation, while `estimated` is a non-binding earliest
fit that respects protected reservations. A short job behind a blocked head
can therefore receive an estimate earlier than the head. `estimation_depth`
on the request limits temporary planning and pending timing to a queue prefix;
zero is the default and considers the complete waiting queue. It does not
change scheduling policy or reservations. A cached head reservation reflects
the shadow time from its planning pass; a recomputed forecast can differ from
an older `GetFCFSHeadShadowTimeRequest` snapshot.
Every projected placement consumes its requested time window before the next
queue job is placed; use a full scheduler lookahead when a policy-exact future
schedule is required.
Unknown or non-appended IDs produce `ErrorResponse`. The request reads the
server's simulation state and works regardless of whether Redis output support
is compiled or configured.

(grpc-estimated-starts)=
### Cached start estimates

Send the empty `GetStartTimesByEstCachesRequest` to read the latest live
planning estimates without forecasting or advancing the simulation.
`GetStartTimesByEstCachesResponse` contains:

- Optional `estimated_at`: the planning timestamp; absent before planning or
  after checkpoint restore.
- `jobs`: eligible pending jobs ordered by known expected start, with unknown
  starts last. Each entry has `job_idx`, optional `expected_start_time`, and
  `start_time_kind` distinguishing protected reservations from estimates.

Future arrivals, running jobs, and completed jobs are excluded. Unexamined
jobs can have no estimate; cached values can be stale between planning passes.
This request corresponds to [`get_start_times_by_est_caches()`][cpp-simulation-get-start-times-by-est-caches].

### Session and checkpoint operations

See [Checkpoint/Restart](user-guide/checkpoint-restart.md) for checkpoint
request requirements, compatibility, and file or Redis output recovery.

For a replay-based warm start, set `InitRequest.sim_start_time` to a positive
global simulation boundary, provide a replay-format `infile`, and send
`RunRequest`.
This field is distinct from each job's historical `begin_time`. The server then
applies the same two-stage warm-start classification as the CLI and Python
batch API. A zero simulation start time preserves ordinary batch behavior;
negative and non-finite values are rejected.

Every `ClientMessage` carries a `request_id`, echoed back on the matching
`ServerMessage`, so a client can correlate responses even if it pipelines
multiple in-flight requests. The provided `dr_evt_client` batch-appends jobs
but sends one protocol request at a time and waits for each response; the
protocol itself does not require that.

### FCFS/EASY shadow-time and resource-change queries

(grpc-head-shadow-time)=
#### Head shadow time

Send `GetFCFSHeadShadowTimeRequest` when only the FCFS queue head's earliest
reserved start time is needed. Its `GetFCFSHeadShadowTimeResponse.shadow_time`
is `-1` when no job is waiting.

(grpc-backfill-window)=
#### Backfill window

Send `GetBackfillWindowRequest` when the resource-change times that lead to
that reservation are also needed. After submitting work and advancing the
simulation to the desired point in time, its matching
`GetBackfillWindowResponse` is an atomic scheduling snapshot with:

- `current_time`: the simulation time at which the snapshot was made.
- `available_nodes`: nodes free immediately at `current_time`.
- `shadow_time`: the earliest start time reserved for the FCFS queue head;
  `-1` if no job is waiting.
- `releases`: the resource-change-time query: chronologically ordered
  `ResourceRelease` events for every running job that ends after
  `current_time`. Each has its absolute simulation `time` and `nodes_released`;
  jobs ending at the same time are combined into one event.

The projection deliberately uses each running job's `time_limit`, not its
actual runtime. That is the same estimate used by the FCFS/EASY scheduler to
calculate `shadow_time`, so clients can safely use the response to evaluate
backfill candidates without seeing a conflicting reservation model. `releases`
may be nonempty even when no job is waiting; it is empty only when nothing is
running.

For example, with no free nodes, a 40-node job predicted to end at time 50,
a 60-node job predicted to end at time 100, and a 100-node FCFS head, the
response at time 0 has `shadow_time = 100` and releases `(50, 40)` and
`(100, 60)`.

(grpc-queue-horizon)=
#### Aggregate queue horizon

Send `EstimateQueueHorizonRequest` with a utilization factor in `[0, 1]` to
estimate how long the current waiting resource-time demand takes to drain after
the shadow time. `EstimateQueueHorizonResponse.horizon` is that duration, not
an absolute job start time. Zero selects the fallback factor `1`. The server scans fields
already stored in supported FCFS queues only for this request; no prediction
state is maintained during normal scheduling.

Session initialization, completion, reuse, and shutdown are documented in
[Client/Server Setup](user-guide/grpc-setup.md#session-identity-and-completion).

## Examples, use cases, and tests

- The [example C++ client](https://github.com/LLNL/dr_evt/blob/main/src/proto/dr_evt_client.cpp)
  demonstrates the complete request sequence described in
  [Connecting the example client](user-guide/grpc-setup.md#connecting-the-example-client).
  Its optional Redis mode advances to a requested time, pipelines finalized
  job lookups, queries the server only for missing IDs, and reports the merged
  statuses in append order.
- [Client/Server Use Cases](user-guide/client-server-use-cases.md) links the
  Python multi-server, MPI-launcher, and synchronized-system examples.
- The [distributed client/server tests](https://github.com/LLNL/dr_evt/blob/main/tests/README.md#distributed-clientserver-tests)
  section lists the exact gRPC and MPI test commands, fixtures, and coverage.

[cpp-sim-params]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/configuration.html#_CPPv4N6dr_evt10Sim_ParamsE
[cpp-simulation]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4I0EN6dr_evt15BasicSimulationE
[cpp-simulation-advance-to]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation10advance_toE10sim_time_t
[cpp-simulation-append-job]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation10append_jobE10sim_time_t11num_nodes_tRKNSt6stringE7tdiff_tNSt8optionalI7tdiff_tEE
[cpp-simulation-append-jobs]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation11append_jobsERKNSt6vectorI18Job_Append_RequestEE
[cpp-simulation-get-start-times-by-est-caches]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation29get_start_times_by_est_cachesEv
[cpp-simulation-initialize-trace]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation16initialize_traceE10num_jobs_t
[cpp-simulation-run]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation3runEv
[cpp-simulation-run-until-exclusive]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation19run_until_exclusiveE10sim_time_t

## Exact placement resources

Select `resource_model = "placement"` in SimParams (Python uses
`params.resource_model`) or `resource_model: "placement"` in gRPC InitRequest.
`placement_order` defaults to `"low"`, `placement_optimizations` to `"all"`, and
`placement_coarse_filter` to false. The CLI/protobuf/Python options select the
same simulator implementation. See
[Resource Models](dev/RESOURCE_MODELS.md) for supported configurations.

JobStatus.node_ids contains exact live or reserved node IDs. A pending status
reports only the latest live reservation, with kind RESERVED; jobs without a
reservation have no pending estimate. The query does not substitute an aggregate
forecast by default. Disabling either cache-reuse flag selects an aggregate
approximation, which omits node identities. Use exact lookahead for a fresh
placement forecast when Ser20 is enabled. Query depth limits pending
reporting, and queries preserve requested order and duplicates.

Exact allocations survive END through delayed RELEASE and checkpoint restart.
Released IDs are not retained in completed statuses. Reservation reporting
snapshots, like existing estimate caches, are rebuilt by scheduling after
checkpoint restore. Resource CSV output continues to report aggregate counts.

Aggregate queue-drain and capacity-area horizon forecasts remain available in
placement mode as approximations. Their results omit identity constraints;
policy protection in an aggregate forecast does not establish an exact node
reservation. Backfill-window summaries contain counts and the actual head start,
but cannot establish an exact placement fit.
