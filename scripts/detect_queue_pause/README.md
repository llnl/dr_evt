# Scheduler capacity and queue-pause inference

This toolkit converts historical batch-scheduler job traces into hourly
normal-queue capacity schedules. It detects sustained behavior consistent with:

- a full system shutdown;
- a paused normal queue while limited/special work continues; or
- operation at reduced capacity.

It distinguishes maintenance-like behavior from ordinary idle time by requiring
queued demand: jobs must have been submitted but not yet started.  Node occupancy
is integrated exactly within each time bin, including jobs crossing file and
month boundaries.

## Input format

The complete workflow accepts one CSV path or glob. Every input row requires:

- `job_submit_time`, `begin_time`, and `end_time` as Unix epoch seconds;
- `num_nodes` as a positive integer node request; and
- `time_limit` as the requested runtime in seconds.

An optional `avgpcon` column improves workload-family separation. Other
columns are preserved by the source files but are not required. Multiple input
files may overlap; choose the overlap policy appropriate for how those exports
were produced.

## Complete workflow

Install the Python dependencies first:

```bash
python3 -m pip install -r requirements.txt
```

Configure the machine and trace semantics through environment variables:

```bash
CAPACITY_NODES=10000 \
TRACE_TIMEZONE=UTC \
OVERLAP_POLICY=combine \
GRACE_MINUTES=60 \
RELEASE_DELAY_MINUTES=10 \
./generate_capacity_schedule.sh 'traces/*.csv' output
```

`OVERLAP_POLICY=combine` treats overlapping files as distinct concurrent
records. Use `filename-month` only for files named
`YY_MM_scheduling_trace.csv` whose adjacent exports duplicate coverage. Under
either policy a row that was already read from a different input file is
skipped and counted as `duplicate_rows` in the report; identical rows within
one file are kept as distinct jobs. Run
`./generate_capacity_schedule.sh --help` for all wrapper settings.

The workflow produces replay evidence, an hourly timeline, an all-state JSON
report, two capacity-schedule CSVs, and two silhouette plots.

### Components

| Script | General role |
| --- | --- |
| `generate_capacity_schedule.sh` | Runs the complete pipeline with explicit machine and trace settings. |
| `discover_maintenance.py` | Reconstructs hourly allocation and demand, then classifies anomalous periods. |
| `backfill_opportunity_audit.py` | Replays conservative EASY scheduling opportunities to filter policy-like delays. |
| `build_resource_capacity_trace.py` | Converts the report into hourly scheduler-capacity scenarios. |
| `plot_capacity_silhouette.py` | Draws non-interpolated capacity-change cases with local-date labels. |

The Python programs can be used independently. The shell driver is the normal
entry point when starting from raw trace files.

### Fugaku example

The included Fugaku traces use JST month boundaries, duplicated adjacent-file
coverage, and a configured capacity of 158,976 nodes:

```bash
CAPACITY_NODES=158976 \
TRACE_TIMEZONE=JST \
OVERLAP_POLICY=filename-month \
./generate_capacity_schedule.sh '*_scheduling_trace.csv' .
```

These are dataset-specific values, not general requirements.

## Running the detector directly

```bash
python3 discover_maintenance.py '*_scheduling_trace.csv' \
  --overlap-policy combine --timezone UTC \
  --output maintenance_report.json \
  --bins-output capacity_timeline.csv
```

Only periods classified as queue pauses are written by default. This filters
the JSON `periods` array but deliberately keeps `capacity_timeline.csv`
complete. `--only-queue-pause` may still be supplied explicitly:

```bash
python3 discover_maintenance.py 'traces/*.csv' \
  --overlap-policy combine --timezone UTC \
  --known-capacity 10000 \
  --backfill-evidence backfill_opportunities.csv \
  --only-queue-pause \
  --output queue_pause_report.json \
  --bins-output capacity_timeline.csv
```

Use `--all-states` to include reduced-capacity, backfill-suppression, and
shutdown classifications in addition to queue pauses.

For monthly exports with duplicated coverage, `--overlap-policy
filename-month` prevents double counting by assigning each
`YY_MM_scheduling_trace.csv` file ownership of only its filename month in the
selected timezone. Jobs, pending intervals, and event counts are clipped at
those month boundaries before the files are stitched together.

The JSON report includes inferred normal capacity, monthly observed-capacity
estimates, detected periods, evidence metrics, confidence, and limitations.  If
physical capacity is known, prefer `--known-capacity N`; trace-only inference is
necessarily a lower bound when the workload never fills the machine.

Timestamps are Unix epoch seconds by default. For readable timestamps, select
the appropriate timezone, for example:

```bash
python3 discover_maintenance.py 'traces/*.csv' \
  --time-format iso --timezone America/Los_Angeles
```

Fixed abbreviations such as `PST` (UTC-08:00) and `JST` (UTC+09:00), numeric
offsets such as `+09:00`, and installed IANA zones are accepted. Prefer
`America/Los_Angeles` over `PST` when daylight-saving transitions should be
applied, and `Asia/Tokyo` is the IANA equivalent of JST.

The optional timeline CSV has these workload fields:

- `pending_jobs`: time-weighted average number of submitted jobs that had not
  begun during the bin;
- `pending_nodes`: time-weighted average sum of requested nodes for those jobs;
- `jobs_submitted`: number of jobs submitted during the bin; and
- `nodes_started`: sum of `num_nodes` for jobs whose execution began in the bin.
- `instantaneous_running_nodes`: exact allocated nodes at the bin's start,
  reconstructed from job start and end events rather than averaged over the
  hour.

Because the first two values are time-weighted averages, they can be fractional.

## Detection method

### 1. Reconstruct the timeline

Each job contributes `num_nodes` to `running_nodes` from `begin_time` up to, but
not including, `end_time`. It contributes one job and `num_nodes` to the pending
values from `job_submit_time` up to `begin_time`. These contributions are
integrated over each bin, so jobs that begin or end partway through a bin are
represented proportionally. The default bin width is one hour.

`jobs_submitted` is an event count: it counts jobs whose `job_submit_time` falls
inside the bin. It does **not** mean submitted and still waiting; that quantity
is represented by `pending_jobs`.

### 2. Estimate normal capacity

Unless `--known-capacity` is provided, normal capacity is estimated as the
99.5th percentile of nonzero `running_nodes` values:

```text
normal_capacity = quantile(running_nodes, 0.995)
```

This estimates the observed schedulable envelope, not necessarily the physical
machine size. It is a lower bound when the workload never fills the machine.
Supplying the known physical or normal-queue capacity is preferable.

The report also calculates a capacity estimate for each calendar month. It uses
bins with queue pressure when at least 24 such bins exist, and otherwise marks
the monthly estimate as a low-demand or partial-window lower bound.

### 3. Require queue pressure

A bin is considered to have demand when either condition is true:

```text
pending_jobs  >= 10
pending_nodes >= normal_capacity * 0.02
```

Requiring demand is important: a machine with no work to run is ordinarily idle,
not undergoing maintenance. Both thresholds are configurable.

### 4. Classify individual bins

For each bin, the detector calculates:

```text
capacity_fraction = running_nodes / normal_capacity
```

It then applies the following default rules:

| Candidate state | Conditions while queue pressure exists |
| --- | --- |
| Full shutdown | Capacity fraction is at most 0.5% and no jobs start |
| Reduced capacity | Capacity fraction is at most 60% |
| Queue pause or maintenance | Capacity fraction is below 85% and the job-start rate is at most 10% of its typical rate |

The typical start rate is the median positive number of job starts in pressured
bins operating at or above the reduced-capacity threshold. If that sample is
empty, the median of all nonzero start bins is used.

### 5. Merge and label periods

Adjacent candidate bins are merged. One intervening normal bin is bridged by
default, which avoids splitting a maintenance period because of a brief burst of
activity. A merged period must last at least one hour.

The merged period is labeled as follows:

- `full_shutdown` when at least 60% of its bins meet the shutdown rule and no
  jobs start in the period;
- `queue_pause_or_maintenance` when at least 60% meet the queue-pause rule; or
- `reduced_capacity` when at least 60% meet the reduced-capacity rule; or
- `backfill_suppression` when replay evidence exists without a sufficiently low
  allocation level for either preceding classification.

Confidence is a heuristic from 0 to 0.99. It increases with queue pressure,
severity of the allocation reduction, and duration. It is not a statistical
probability.

### Interpretation limitations

Pending jobs are not necessarily ready to run. Dependencies, reservations,
resource-shape constraints, or policy can produce low allocation despite a
large pending queue. Canceled jobs and jobs omitted from the traces are also
invisible. The reported periods should therefore be treated as candidates for
validation against maintenance logs rather than definitive maintenance records.

Useful tuning controls include `--bin-minutes`, `--min-duration-hours`,
`--reduced-fraction`, and the pending-demand thresholds. Run
`python3 discover_maintenance.py --help` for all options.

When an input has no queue or partition column, the script can identify a
behavioral queue pause but cannot name or prove which queue caused it. Adding
such a field to future traces would allow direct per-queue analysis.

## Resource-capacity scenario traces

`build_resource_capacity_trace.py` produces two hourly normal-queue capacity
scenarios while keeping `structural_capacity_nodes` fixed at the configured
machine capacity. For the Fugaku example:

```bash
python3 build_resource_capacity_trace.py \
  --report maintenance_report.json \
  --timeline capacity_timeline.csv \
  --capacity 158976
```

By default these files retain the detailed hourly analysis columns. Add
`--simulator-format` to write compact files accepted directly by
`simulator --capacity_schedule`:

```bash
python3 build_resource_capacity_trace.py \
  --report maintenance_report.json \
  --timeline capacity_timeline.csv \
  --capacity 158976 \
  --simulator-format
```

That mode writes only `time,total_nodes`, removes consecutive duplicate
capacities, and restores full capacity at the end of the analyzed timeline if
its final interval was reduced or paused. The timestamps are Unix epoch
seconds. Pass the same value used for `--capacity` to simulator
`--total_nodes`.

For the complete wrapper, set `SIMULATOR_FORMAT=1` to select this output mode.

- `resource_capacity_with_reduced_capacity.csv` sets normal-queue capacity to
  zero during inferred queue pauses or shutdowns and applies the report's
  evidence-derived capacity value during inferred reduced-capacity periods.
- `resource_capacity_without_reduced_capacity.csv` sets normal-queue capacity
  to zero during inferred queue pauses or shutdowns but assumes full capacity
  during periods labeled `reduced_capacity`.
- Both assume full capacity during normal time and `backfill_suppression`.

These are scheduler-facing scenario traces, not claims that a machine's
physical node count changed. The reduced-capacity version is explicitly a
sensitivity case derived from behavioral evidence; the without-reduction
version reflects the assumption that structural capacity remained fully
installed.

Silhouette plots:

```bash
python3 plot_capacity_silhouette.py \
  resource_capacity_with_reduced_capacity.csv \
  --output inferred_capacity_silhouette_labeled_jst.png \
  --title 'Inferred normal-queue capacity (reductions and queue pauses)' \
  --cases-per-row 21 --timezone JST

python3 plot_capacity_silhouette.py \
  resource_capacity_without_reduced_capacity.csv \
  --output queue_pause_capacity_silhouette_labeled_jst.png \
  --title 'Queue-pause-only normal-queue capacity' \
  --cases-per-row 21 --timezone JST
```

These plots collapse each constant-capacity interval to one equal-width,
contiguous rectangle. Changes are vertical, never interpolated; every case is
labeled by its start date in the selected timezone (JST in this example). The
second CSV is used here only as the queue-pause-only trace: full physical
capacity outside pauses and zero normal-queue capacity during pauses.

## Reference EASY backfill replay

For stronger evidence than queue pressure alone, first run the observed-schedule
replay derived from the Python reference EASY scheduler. This example uses the
included Fugaku files and settings:

```bash
python3 backfill_opportunity_audit.py '*_scheduling_trace.csv' \
  --timeline capacity_timeline.csv \
  --nodes 158976 --timezone JST \
  --grace-minutes 60 --release-delay-minutes 10 \
  --output backfill_opportunities.csv

python3 discover_maintenance.py '*_scheduling_trace.csv' \
  --overlap-policy filename-month --timezone JST \
  --known-capacity 158976 \
  --backfill-evidence backfill_opportunities.csv \
  --all-states \
  --output maintenance_report.json \
  --bins-output capacity_timeline.csv
```

### Conservative controls against scheduler-policy false positives

The replay does not treat every reference-EASY opportunity that starts late as
evidence of reduced resources. At each hourly snapshot it applies these
controls:

1. A candidate that actually starts within `--grace-minutes` is successful,
   not missed. The default grace window is 60 minutes.
2. A candidate is also discounted when another job that was already pending at
   the same snapshot, requested at least as many nodes, and starts within the
   grace window. This equal-or-larger successful job is evidence that the node
   count was dispatchable and that policy or unobserved eligibility may explain
   the candidate's delay. The audit exports `size_controlled_*` counts.
3. Nodes remain unavailable for `--release-delay-minutes` after recorded job
   end. The default is a conservative 10-minute reclaim interval. This delay is
   included both in current free-node accounting and in EASY reservation safety
   tests. The audit exports `reclaiming_nodes` and `unavailable_nodes`.
4. Stable source-qualified job identifiers are exported for opportunity and
   miss sets. Merged-period miss fractions use distinct jobs, so one job seen
   waiting in several hourly snapshots is counted only once for the period.

The size control is deliberately one-sided: the trace has no queue, partition,
user, project, application, dependency, reservation, or node-topology fields.
Consequently it can disqualify weak evidence but cannot prove that two jobs had
identical scheduler eligibility. A detected period remains behavioral evidence,
not proof that physical nodes were removed.

### Workload-family diversity

A workload family is the fingerprint
`(num_nodes, time_limit_seconds, rounded average power per node)`. Average power
per node is rounded to the nearest 5 units. It is not an identity for a user,
project, queue, or executable. Requiring two families prevents several
identical-shaped jobs from satisfying the evidence gate by themselves.

Families with at least three regularly spaced submissions are discounted when
their inter-arrival-gap coefficient of variation is at most 0.20 and the mean
gap is at least 30 minutes. A family is also discounted as habitually held when
at least three distinct jobs miss opportunities and at least half of that
family's jobs do so. These filters reduce recurring-policy artifacts; they do
not recover scheduler fields absent from the input.

At each candidate hour, the audit reconstructs the jobs actually running and
waiting. It applies the reference scheduler's EASY rules without changing the
observed schedule:

1. Start consecutive FCFS head jobs while they fit.
2. Reserve enough nodes for the first blocked head using running jobs' requested
   time limits.
3. Find later jobs whose node request fits currently free nodes and whose time
   limit ends before that reservation.
4. Count an opportunity as missed only when the observed job does not start
   within the one-hour grace period.

Repeated holds are discounted in two ways. A workload family is fingerprinted
from node count, time limit, and a coarse per-node power bucket. Regularly
submitted families are treated as recurring. A repeated family is also treated
as habitually held when at least three distinct instances miss opportunities and
at least half of that family's instances do so. Neither family type contributes
to the non-recurring evidence count.

When `--backfill-evidence` is supplied, a reduced-capacity bin must have at least
three missed non-recurring EASY backfills from at least two distinct workload
families, and strictly more than half of its adjusted eligible backfills must be
missed. The adjusted denominator is all replayed backfill opportunities minus
misses attributed to recurring or habitually held families. Successful starts
remain in the denominator. Full-shutdown and queue-pause bins use the
corresponding combined direct-start and backfill evidence. These defaults are
configurable with `--min-missed-jobs`, `--min-missed-families`, and
`--min-backfill-miss-fraction`.

With replay evidence enabled, any hourly bin meeting that multi-job,
multi-family, strict-majority backfill rule is treated as a maintenance
candidate. A candidate period must last at least one hour by default. Its state
describes the observed effect: shutdown, queue pause, reduced capacity, or
backfill suppression without a large allocation reduction.

For every resulting period, the report also gives potential effective-capacity
bounds. For each qualifying evidence hour, it calculates
`instantaneous_running_nodes + smallest_missed_backfill_job_nodes - 1`. The
period lower bound is the maximum of those values and the maximum observed
occupancy. The trace does not establish a meaningful effective-capacity upper
bound, so none is reported; a user-supplied physical capacity is shown only as
context. The lower bound is conditional on the replay assumptions and is not a
physical node-health measurement.

This remains a conservative behavioral inference. The trace has no explicit
hold, queue eligibility, dependency, reservation, or node-topology fields, so
the replay cannot prove that a job was operationally eligible to run.
