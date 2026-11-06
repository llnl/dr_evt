# Performance Analysis

This page records profiles of the C++ simulator: a June 2022 RIKEN/Fugaku
trace capture and a later unified-dispatch prefix benchmark. Each section
identifies costs for its measured workload and configuration; the results
are not general performance guarantees or comparisons among queue implementations.

For instructions on collecting a new profile, see
[Linux `perf` profiling](../getting-started/installation.md#linux-perf-profiling-recommended)
or [GNU `gprof` profiling](../getting-started/installation.md#gnu-gprof-profiling).
For end-to-end results comparing the available queue implementations, see the
[wait queue performance comparison](WAIT_QUEUES.md#benchmark-record).

## Workload and measurement

The source trace was `22_06_scheduling_trace.csv`, containing 473,224 job
records plus its header. The temporary profiling wrapper set
`MAX_JOBS=50000` and passed `--max_jobs 50000` to the simulator. Consequently,
this profile covers the first 50,000 records, not the complete June trace. It
used:

| Setting | Value |
| --- | --- |
| Priority policy | FCFS |
| Wait queue | `circular` (`boost::circular_buffer`) |
| Backfilling | EASY |
| Runtime mode | `actual` |
| System size | 142,167 nodes |
| Trace format | `simple` |
| Timestamp format | `epoch` |

The profile was captured on October 4, 2026, on the LLNL Dane system with an
Intel Xeon Platinum 8480+ processor. The build was an optimized Release build
with debug information. The capture was launched through the temporary
wrapper; its expanded simulator command was equivalent to:

```bash
SIMULATOR=/path/to/simulator
perf record -F 999 -g --call-graph dwarf -- \
  "$SIMULATOR" 22_06_scheduling_trace.csv \
  --priority_policy fcfs \
  --queue_impl circular \
  --total_nodes 142167 \
  --max_jobs 50000 \
  --trace_format simple \
  --timestamp_format epoch \
  --run_time_mode actual \
  --backfill_policy easy \
  --outfile /tmp/circular_output.csv \
  --resource_trace /tmp/circular_resources.csv
perf report --stdio -i perf.data > analysis.txt
```

The recording covered 3.442 seconds, collected approximately 3,000 `cycles`
samples representing 12.97 billion cycles, and lost no samples.

Percentages below use all recorded cycles as the denominator. `Children` is
inclusive cost and therefore overlaps with callees; `Self` is time sampled in
the named function itself. Inclusive rows must not be added indiscriminately.

## Scheduler-level breakdown

`FCFSEasyCircularScheduler::schedule()` accounted for 84.41% of all
recorded cycles. Its call tree divides into three parts:

| Scheduler work | All recorded cycles | Share of scheduler |
| --- | ---: | ---: |
| Copy and destroy the effective running-job tree | 45.96% | 54.4% |
| Calculate the FCFS reservation | 36.27% | 43.0% |
| Direct scheduler work | 2.18% | 2.6% |

The 45.96% branch is displayed as unresolved inlined frames, but its identity
is supported by the source and the sampled standard-library symbols:

- `std::_Rb_tree<...>::_M_copy` has 7.20% self cost;
- `std::_Rb_tree<...>::_M_erase` has 5.26% self cost; and
- allocation and deallocation routines together account for approximately
  38% of all self samples, although some of those allocations also belong to
  temporary vectors elsewhere in the scheduler.

The source of this work is the full copy of `running_jobs` into
`effective_running_jobs` on every scheduling pass, followed by insertion of
jobs selected during the same pass. Destruction of the local map then frees
all copied tree nodes before returning:

```cpp
running_jobs_t effective_running_jobs = running_jobs;
// ...
effective_running_jobs[job.job_id] = {
    current_time, job.run_time_estimate, job.nodes_requested};
```

See `src/sim/scheduler_fcfs_easy_circular.cpp`, lines 70-86.

The remaining 2.18% direct cost includes consuming runnable jobs from the queue,
scanning eligible jobs for EASY backfill candidates, maintaining lazy-removal
state, and growing the returned job vector. The profile does not resolve those
inlined operations finely enough to assign reliable individual percentages.

## Reservation calculation

`SchedulerBase::calculate_fcfs_reservation()` accounts for 36.91% inclusive
across the report; 36.27% is reached from the circular scheduler. Its measured
cost is:

| Reservation work | All recorded cycles | Share of reservation |
| --- | ---: | ---: |
| Sort completion events (`std::__introsort_loop`) | 19.10% | 51.7% |
| Traverse the running-job red-black tree | 7.89% | 21.4% |
| Build and scan the event vector and other direct work | 9.92% | 26.9% |

The function traverses the job-ID-keyed `std::map`, creates a vector containing
every future completion, sorts the entire vector by completion time, and then
scans until enough nodes have been released:

```cpp
std::vector<std::pair<sim_time_t, num_nodes_t>> end_events;
end_events.reserve(running_jobs.size());

for (const auto &[job_idx, job] : running_jobs) {
  const sim_time_t end_time = job.start_time + job.run_time;
  if (end_time > current_time) {
    end_events.push_back({end_time, job.nodes});
  }
}

std::sort(end_events.begin(), end_events.end());
```

See `src/sim/scheduler_base.cpp`, lines 84-116. Because this work is repeated
whenever the FCFS head is blocked, its cost grows with both the number of
running jobs and the number of scheduling passes.

## Other observed costs

Trace completion and job-output processing each accounted for approximately
1%:

- `BasicTrace::process_single_event`: 1.05% inclusive;
- `BasicTrace::flush_completed_jobs_impl`: 1.02% inclusive; and
- `BasicTrace::write_job_range`: 1.05% inclusive.

Trace initialization accounted for 0.73%. For this
workload, parsing and output are not useful first optimization targets.

## Optimization priorities

1. Eliminate the full `running_jobs` map copy. Reservation calculation can
   consume the authoritative running-job collection by const reference and a
   short-lived range of jobs selected during the current scheduling pass.
   This avoids duplicating ownership and targets the 45.96% copy/destruction
   branch. That percentage is an upper bound, not an expected speedup.
2. Avoid fully sorting every completion event for every blocked scheduling
   pass. Potential designs should first determine whether release ordering can
   be maintained by the existing running-job lifecycle owner without adding
   independently synchronized state. This targets the 19.10% sorting cost.
3. Reuse temporary vector capacity where ownership and reentrancy permit it.
   This can reduce allocator pressure but will not remove the sorting and tree
   traversal costs.
4. Re-profile before optimizing the backfill scan. Only 2.18% remains as
   direct scheduler work after the two dominant branches, and that percentage
   includes more than the scan itself.

Together, map-copy/destruction and reservation calculation account for 82.23%
of all recorded cycles. They are therefore the appropriate first targets, but
their percentages overlap with allocator and STL implementation symbols and
must not be added to those lower-level rows.

Compared with the preceding limit-runtime capture of the same trace prefix,
the scheduler's total share is similar (84.41% versus 85.61%), but the
copy/destruction branch is larger (45.96% versus 37.31%) and reservation
calculation is smaller (36.27% versus 39.11%). Actual job durations change the
set and lifetime of concurrent jobs, so hotspot proportions from one runtime
mode should not be used as estimates for the other.

## Interpretation limits

This is one short sample of one trace prefix and scheduler configuration.
Results may change with queue depth, concurrent running-job count, backfill
frequency, compiler, allocator, and CPU. Approximately 3,000 samples are
enough to distinguish the dominant paths but not sub-percent differences.
Several optimized inlined frames appear as `??`; a longer profile plus
`perf annotate` should be used to evaluate source-line changes.

After an optimization, compare multiple alternating baseline and candidate
runs using the same trace prefix and configuration. Report wall time as well as
cycles, and retain sample counts and lost-sample counts with the result.


## Unified dispatch candidate-prefix optimization (November 6, 2026)

The baseline was `dae396c`, after compile-time trigger specialization.
The optimization changes only `FCFSUnifiedCircularScheduler`; event-driven
and separate periodic-backfill scheduler implementations are unchanged.
The build was Release (`-O3`) on macOS arm64 with
`DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`.

The saturated workload has four nodes, an initial four-node job with a
200,000-second time limit, and 10,000 four-node one-second jobs, all submitted
at zero. It uses limit runtimes, a one-second dispatch interval, and
`--dispatch_queue_depth 10000`. Repeated ticks keep the same blocked queue
until the initial job finishes. This deliberately exposes prefix-selection
cost; it is not a representative production trace.

A macOS `sample` capture found 881 of 902 application-thread samples in the
candidate-counting loop for fixed conservative dispatch. The requested
capture used a 1 ms sampling interval and 2 s duration; the process finished
before that duration. The FCFS capture collected no useful call stacks and
is not used to estimate hotspot percentages.

Previously, every bounded unified pass walked the candidate prefix to count
live entries, even when no entries had been removed. The optimized path:

- Uses the queue index directly when there are no removed entries.
- Avoids bounding/restoring a prefix when all eligible live jobs already fit
  within the configured candidate limit.
- Retains counting through removed entries when necessary, including the
  original compaction rules and future-arrival eligibility boundary.

Measurements below exclude profiler overhead. Each comparison uses one warm-up
pair and five measured alternating before/after pairs, including process
startup, input, scheduling, and CSV output. Median wall times are:

| Unified mode | Before | After | Speedup |
|---|---:|---:|---:|
| Fixed FCFS | 1.107 s | 0.072 s | 15.3× |
| Fixed conservative | 1.121 s | 0.081 s | 13.8× |
| Timer plus arrivals, conservative | 1.120 s | 0.082 s | 13.6× |

A separate seeded mixed workload has 20,000 jobs submitted four per second,
1–8 requested nodes and 1–30 second limits, on 128 nodes (Python random seed
2718). With the same interval, candidate limit, and conservative policy,
fixed dispatch measured 8.903 s before and 8.855 s after. With arrival
notifications enabled, the same workload measured 9.297 s before and
9.273 s after. Neither comparison shows a material change in these runs.
Their schedules were byte-identical. The integer-second arrivals coincide
with ticks, so this workload does not measure frequent extra between-tick
notification passes. This broader case is
much less sensitive to prefix counting than the saturated probe.

Each before/after schedule was byte-identical. The speedup applies to repeated
large-prefix scans without removed entries; it does not imply that reservation
planning or event processing is faster by the same factor. Small prefixes,
queues with many removed entries, and workloads dominated by reservation
planning need separate measurements.

A second 2 s mixed-workload capture collected 1,450 application-thread
samples after optimization. Of those, 515 were at one capacity-segment skip
instruction in the protected-job earliest-fit search, and 230 were in
`Reservation_Timeline::reserve` itself. Several search frames appeared as
`???`; local symbols and disassembly map them to the `protect_job` lambda
in `FCFSHybridCircularScheduler::schedule`, with `earliest_fit` inlined.
The next measured opportunity is reducing repeated linear timeline searches
and reservation updates. This optimization does not change that shared planner.
The short capture identifies a target; it does not provide whole-run cost
percentages for every search or reservation operation.

The unified-dispatch regression script also checks removed entries, finite
candidate limits, future arrivals, and compaction for both
notification settings. Streaming, checkpoint, and warm-start checks pass.
Local raw profiles, generated inputs, benchmark scripts, and results are under
`/tmp/dr_evt_unified_profile` and `/tmp/dr_evt_unified_optimization_benchmark.py`;
these paths are local evidence, not files shipped with the repository.
