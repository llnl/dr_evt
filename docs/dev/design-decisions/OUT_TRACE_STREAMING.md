# Trace as a self-contained, streaming-ready state container

`Trace` owns the state required to run a simulation and produce its output
traces. The two potentially unbounded collections—the job-record store and the
resource-history log—use `boost::circular_buffer`. This keeps their memory
behavior explicit while allowing a simulation to accept jobs incrementally.

`Simulation` owns a `Trace`; schedulers access job records through `Trace`
rather than retaining a pointer to a separate, fixed-layout job vector.

## The two circular buffers

| Buffer | Contents | When entries can be released | What happens when full |
| --- | --- | --- | --- |
| `Trace::m_data` | Job records used for scheduling, statistics, and the simulated-job trace | After the job's departure event has been processed, or immediately for a rejected job | Reclaim eligible records from the front, then grow or abort according to `--job_store_overflow` |
| `Trace::m_resource_history` | Finalized `(time, free_nodes, allocated_nodes)` resource samples for `--resource_trace` | Immediately after the sample is recorded | Flush samples to the resource-trace file and clear the buffer |

The buffers are independent of the scheduler's circular FCFS wait queue. The
CSV output files are streamed; they are not circular buffers themselves. See
[Output Trace Files](../../user-guide/output-traces.md) for their formats and
defaults.

## Job-record storage and stable identifiers

Jobs retain their permanent `job_no` even as completed records are reclaimed
from the front of `m_data`. `Trace::job_at(job_no)` is the sole translation
point between that permanent identifier and the buffer's physical position:

```text
physical position = job_no - m_num_reclaimed
```

This works because job records are reclaimed only from the front and are never
compacted in the middle. A backfilled job may finish before an earlier job, but
it remains stored until every preceding record is also reclaimable. Schedulers
therefore hold a `Trace` reference and use `job_at()` rather than indexing the
buffer directly.

Before a record is discarded, `write_job_line()` consumes it for the simulated
job trace and accumulates the summary statistics. The same function is used by
both reclamation and the final flush, so each scheduled job is written and
counted exactly once.

## Reclamation rules

Reclamation is lazy: it occurs only when new capacity is needed, rather than on
every simulated completion.

- A job record becomes reclaimable after its departure event has run. In
  practice, the check is `end_time <= current_time`.
- A permanently unschedulable job is marked with
  `Job_Record::unscheduled_sentinel()` and can be reclaimed without waiting for
  an end time that will never exist.
- Resource samples need no delay. Once written to the resource-history buffer,
  no later simulation step needs to read them, so a full buffer can be flushed
  immediately.

For job records, the insertion order is reclaim first, then grow or abort only
if the required room is still unavailable. This permits a completed job's slot
to be reused without increasing capacity.

## Loading and streaming jobs

There are three ways jobs enter `m_data`.

### Single-file batch loading

`load_data()` reads and sorts the whole input trace before simulation starts.
The job store grows to fit the entire file, even if a smaller initial
`--job_store_capacity` was requested. Consequently, that setting controls the
initial allocation in single-file mode, not the steady-state memory limit.

### Streaming API

`Trace::append_job()` and `Simulation::append_job()` add one newly arriving
job. Their gRPC counterpart is `AppendJobRequest`.

`Trace::append_jobs()` and `Simulation::append_jobs()` add a vector of newly
arriving jobs; their gRPC counterpart is `AppendJobsRequest`. A batch is
validated and given capacity before any record is appended:

1. Every submit time must be at least the current simulation time.
2. Submit times within the batch must be non-decreasing.
3. The store attempts to reclaim enough front records for the entire batch.
4. If necessary, capacity grows once to fit the batch, or the operation aborts
   before changing `m_data` when `--job_store_overflow=abort` is selected.

This makes a batch append all-or-nothing. `submit_job(job_no, ...)` is not an
append operation: it schedules a job record that is already present.

### Progressive multi-file loading

`--infile_list` supplies a pre-split, pre-sorted sequence of trace files.
`Trace::load_next_file()` reads one file at a time, and
`Simulation::run_progressive()` advances the simulation through that file
before loading the next. This avoids keeping all files' job records in memory
at once.

Each file must be sorted by `submit_time`, and the earliest job in a file must
not precede the latest job in the previous one. This is deliberately different
from adaptive chunking of a single input file: the caller chooses the file
boundaries.

## Capacity and memory-pressure checks

`resolve_job_store_capacity()` and the shared
`Trace::ensure_batch_capacity()` helper implement the job-store capacity
policy. The default job-store capacity is sized for the loaded job count;
resource history defaults to twice that count, because each job contributes at
most a start and an end resource sample. Both defaults have a minimum capacity
of 4096.

`--check_memory_pressure FRACTION` optionally prevents a capacity increase
whose projected peak allocation exceeds the requested fraction of available
memory. The check mirrors the actual doubling logic, including the temporary
overlap of the old and new circular buffers during `set_capacity()`. It also
accounts for the incoming batch. It is disabled unless explicitly set and is a
no-op when available memory cannot be determined (for example, outside Linux
or without `MemAvailable` in `/proc/meminfo`).

The check refuses the operation rather than waiting: a synchronous simulation
cannot obtain additional useful memory by waiting, and reclamation has already
been attempted before the check.

## Limits and future direction

Progressive loading is the memory-bounded option for known traces. Single-file
batch loading still reads the complete input file before the run begins. A
future adaptive single-file reader could choose chunks according to remaining
job-store capacity and memory pressure, but it is not required for the current
streaming or multi-file interfaces.

The reclamation-safety decision is isolated in `is_front_reclaimable()`. That
keeps the current single-process rule simple while leaving a natural extension
point for a future optimistic or parallel simulation horizon.

## Related code and tests

- `src/trace/trace.{hpp,cpp}`: storage, capacity, reclamation, and output
  writing
- `src/sim/simulation.{hpp,cpp}`: streaming and progressive-run entry points
- `src/utils/system_memory.hpp`: Linux available-memory lookup
- `tests/test_append_job_api.cpp`: single and batched append behavior,
  including reclaim-before-grow and all-or-nothing capacity failures
- `tests/test_grpc_streaming_api.cpp`: gRPC streaming API
- `tests/test_progressive_load.cpp` and `tests/run_progressive_load_tests.sh`:
  multi-file loading and memory-pressure behavior
