# Simulation Pipeline and Job Lifecycle

This page describes how input becomes scheduler state, events, and output. It
is the development-level companion to the public
[Streaming API](../api/STREAMING_API.md) and the generated C++ Development API
Reference.

:::{figure} ../_static/simulation-internals.png
:alt: Simulation owns a scheduler and trace. The scheduler contains the wait queue; the trace contains the job store, event queue, and resource history and writes the output traces.
:width: 72%
:align: center

Component ownership in the simulation pipeline.
:::

## Pipeline overview

```text
configuration
  -> input ingestion
  -> job store
  -> scheduler wait queue
  -> event loop
  -> start/end event queue
  -> resource accounting
  -> scheduled-job and resource traces
```

A `Simulation` owns a `Trace` and a scheduler. The `Trace` owns job records,
the running-job event queue, resource history, and output writers. The
scheduler owns the wait queue and decides which eligible jobs start; it does
not own the full job records.

## State required by an implementation

An implementation in another language needs the following logical state. The
container types may differ as long as their ordering and ownership contracts
are preserved.

| State | Minimum contents | Required behavior |
|---|---|---|
| Job store | Stable job ID, submit time, requested nodes, queue, time limit, actual runtime, start time, end time | Preserve input order for equal submission times and retain completed records until their output rows are consumed. |
| Scheduler wait queue | Job ID plus copied submission, node, and runtime-estimate fields | Distinguish future arrivals from arrived jobs and remove a job exactly once when selected. |
| Running jobs | Job ID, start time, requested nodes, and time-limit estimate | Supply projected releases for backfill reservations and monitoring queries. |
| Resource-event queue | Job ID, timestamp, and start/end kind | Sort by timestamp, then end before start, then stable job ID. |
| Resource state | Total nodes, allocated nodes, and optional trace-policy fields | Apply one allocation or release transition per event and reject underflow or over-allocation. |
| Output state | Scheduled-job cursor, resource-history buffer, and aggregate statistics | Write each eligible job exactly once, even when its in-memory record is later reclaimed. |
| Simulation clock | Current logical time | Never move backward; advancing through an idle interval still updates the clock to the requested target. |

The stable job ID must not be a physical array index if the job store can
discard its front. DR_EVT keeps a count of reclaimed records and translates a
job ID to the current circular-buffer offset.

In code, the resource-event type called an *arrival* means a job **start** and
allocation. It is distinct from a job submission arriving at the scheduler.
This distinction is important when porting the implementation.

## Input paths

All simulation inputs converge on the same submission and event-processing
pipeline:

- **Single-file batch:** `Simulation::run()` calls `initialize_trace()`, which
  loads the file, stably sorts records by submission time, and determines each
  actual runtime. It then submits the loaded records and advances until no
  waiting or running jobs remain.
- **Progressive files:** `run_progressive()` loads one already-sorted file at
  a time, submits that file's records, advances through its last submission
  time, and then loads the next file. This permits completed front records to
  be reclaimed between batches.
- **Live streaming:** `append_job()` or `append_jobs()` creates new records in
  the job store and immediately submits them. The caller controls progress
  with `advance_to()` or `run_until_exclusive()`.

Replay-format input follows a different execution path. Its recorded
`begin_time` and `end_time` values are authoritative, so `Trace::run_job_trace()`
bypasses the scheduler and reconstructs resource use directly. See
[Simulation vs. Replay Modes](design-decisions/SIMULATION_VS_REPLAY_MODES.md).

Input columns and normalization rules are specified in
[Input Trace Files](../user-guide/trace-formats.md). For batch and progressive
input, runtime selection happens once after records are loaded: `actual` uses
the recorded duration, `limit` copies the requested limit, and `distribution`
draws from the configured seeded generator. The live API supplies only a time
limit, so a live job uses that as its actual runtime. Scheduling reservations
always use the requested time limit, while completion events use the selected
actual runtime.

## Submission

```text
Simulation::append_job()
  -> Trace::append_job()             create a Job_Record in the job store
  -> Simulation::submit_job()        validate arrival and collect accounting
     -> SchedulerBase::insert_job()  add scheduling data to the wait queue
```

`submit_job()` records resource occupancy as observed at arrival, rejects jobs
that request more than the system's total nodes, and inserts accepted jobs into
the scheduler. Queue-length statistics count the earlier jobs already waiting
when each accepted job arrives.

`SchedulerBase::insert_job()` and `Trace::insert_job()` deliberately operate
on different structures despite their shared name. The former is wait-queue
insertion; the latter records that a selected job has started. Neither creates
a new `Job_Record`; only `Trace::append_job()` does that for live arrivals.

Appending a batch is atomic with respect to validation and capacity: validate
ordering and all submission times, reserve or reclaim enough storage for the
whole batch, then append and submit its jobs. A partial append would make a
retry duplicate jobs and is therefore not an acceptable failure mode.

The resulting state progression is:

```text
known future job -> waiting -> running -> completed -> written -> reclaimed
                       \
                        -> rejected (request exceeds total capacity)
```

Rejected jobs never enter the wait queue and are omitted from scheduled-job
output and completed-job statistics.

## Event loop and scheduling

`advance_to(target_time)` repeatedly chooses the next scheduler arrival or
pending trace event at or before the target. At each timestamp it:

1. synchronizes the scheduler to the current simulation time;
2. records queue-length observations for arrivals at that time;
3. processes all pending events at that timestamp, releasing resources for
   completed jobs;
4. asks the scheduler for jobs that can start with the newly available nodes;
   and
5. calls `Trace::insert_job()` for each selected job, creating its start and
   end events and updating the running-job map.

The scheduling step repeats at the same timestamp until no additional job can
start. FCFS, EASY, and CONSERVATIVE decisions are described in
[Backfilling Algorithms](../BACKFILLING_ALGORITHMS.md).

The following pseudocode captures the orchestration contract. Policy-specific
selection belongs inside `scheduler.schedule()`.

```text
advance_to(target):
    require target >= current_time

    scheduler.sync_to(current_time)
    record_submission_queue_observations(current_time)
    schedule_until_stalled()

    while waiting jobs exist or resource events exist or future arrivals exist:
        event_time   = earliest resource-event time, or infinity
        arrival_time = scheduler.next_arrival_time(), or infinity
        next_time    = min(event_time, arrival_time)
        if next_time > target:
            break

        current_time = next_time
        scheduler.sync_to(current_time)
        record_submission_queue_observations(current_time)

        processed_end = false
        if event_time <= arrival_time:
            while earliest resource event is at current_time:
                event = pop earliest event
                apply its allocation or release
                emit one resource-history sample
                processed_end |= event is an end

        if arrival_time <= current_time or processed_end:
            schedule_until_stalled()

    current_time = target

schedule_until_stalled():
    repeat:
        selected = scheduler.schedule(total_nodes - allocated_nodes,
                                      running_jobs, current_time)
        if selected is empty:
            return
        for each selected job in returned order:
            set begin_time = current_time
            set end_time = current_time + actual_runtime
            enqueue start and end resource events
            add the job to running_jobs
            immediately process its start event
```

When several resource events share a timestamp, end events sort before start
events, and job ID breaks remaining ties. Process every already-queued event at
that timestamp before making the next scheduling decision. This makes released
nodes visible to all same-time scheduling choices and keeps resource traces
deterministic.

`advance_to(t)` is inclusive. A strict-boundary API must stop before events at
`t`; it can find the latest event earlier than `t` and invoke the same inclusive
engine at that earlier timestamp. Callers must never later submit a job whose
submission time is earlier than the simulation clock.

## Replay pipeline

Replay uses the same resource-event ordering and accounting but never calls a
scheduler. Given records ordered by submission time:

```text
for each recorded job:
    process all queued start/end events through job.submit_time
    record allocated nodes observed at submission
    enqueue (job.begin_time, start, job.id)
    enqueue (job.end_time, end, job.id)

process all remaining events in order
flush requested resource and analysis outputs
```

Processing events before enqueuing the next job is important for bounded
storage: a replay record cannot be reclaimed until both of its events have
been placed on the event queue and subsequently consumed. The recorded
schedule is never modified, and `total_nodes` affects only the derived free
node count.

## Accounting, output, and reclamation

Processing a start event allocates nodes; processing an end event releases
them. Each transition records a resource-history sample. Scheduled-job rows
remain tied to their job-store records until their departure has been processed
and their row is safe to write.

Completed records are written and reclaimed in a contiguous front prefix when
space is needed, when the configured flush interval is reached, on an explicit
flush, or during final output. Resource history uses a separate circular
buffer and output stream. The ownership rules and flush triggers are detailed
in [Output-Trace Buffers](OUTPUT_TRACE_BUFFERS.md).

A front job is reclaimable only after its end time is no later than the
current time and all resource events through that time have been consumed. In
replay, its start and end events must also have been enqueued. Write the job's
schedule row before removing the record, advance a monotonic output cursor,
and update completed-job statistics at that same exactly-once point.

Resource output begins with a baseline sample at time zero with all nodes free.
Each processed start or end event adds a row after applying that transition, so
multiple rows may legitimately share a timestamp. Scheduled-job rows and CLI
statistics are specified in [Output Trace Files](../user-guide/output-traces.md).

## Behavioral equivalence checks

A port should preserve observable behavior rather than C++ container choices:

- batch and streaming execution of the same ordered jobs produce the same
  schedule and resource history;
- progressive file boundaries do not change results;
- all FCFS queue implementations produce the same schedule and resource
  history;
- replay bypasses scheduling and reproduces resource transitions from recorded
  start and end times; and
- output flushing or job reclamation does not change rows, statistics, or
  event ordering.

Concrete fixtures and runners are linked from the
[streaming API tests](https://github.com/LLNL/dr_evt/blob/main/tests/README.md#streaming-api-tests),
[wait-queue tests](https://github.com/LLNL/dr_evt/blob/main/tests/README.md#wait-queue-tests),
and [replay tests](https://github.com/LLNL/dr_evt/blob/main/tests/README.md#replay-tests)
sections of the Test Suite README.

## Implementation entry points

- [`src/sim/sim.cpp`](https://github.com/LLNL/dr_evt/blob/main/src/sim/sim.cpp): input-path selection, submission, and the event loop.
- [`src/sim/scheduler_base.hpp`](https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_base.hpp): scheduler interface and wait-queue ownership.
- [`src/trace/trace.cpp`](https://github.com/LLNL/dr_evt/blob/main/src/trace/trace.cpp): event processing, resource accounting, output, and
  reclamation.
- [`src/trace/job_record.hpp`](https://github.com/LLNL/dr_evt/blob/main/src/trace/job_record.hpp): persistent job input and scheduling results.
