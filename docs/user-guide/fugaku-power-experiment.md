# Fugaku Power-Usage Simulation Experiment

:::{admonition} Experiment at a glance
:class: note

1. Start with the real April 2024 Fugaku operational log containing 420,450
   jobs.
2. Replay its recorded start and end times, without scheduling, to reconstruct
   historical resource and power usage.
3. Use the replay's 142,167-node maximum allocation as the DR_EVT simulation's
   configured node count. This is an observed workload high-water mark, not
   the machine's full capacity.
4. Simulate the corresponding trace without historical start and end times;
   DR_EVT computes a new schedule from the original submissions, node requests,
   and runtimes.
5. Compare replay with simulation. They are not expected to match because
   DR_EVT does not reproduce all Fugaku production scheduling policies and
   constraints.
6. Compare loading the original 420,450-job CSV as one file with loading the
   same data split into nine pieces solely to test progressive loading.
7. Compare Python and C++ simulator scaling as the workload grows, through the
   combined 928,736-job March-April input.
8. Present aligned replay and simulation resource plots together with a
   dual-axis implementation-scaling plot.
:::

This experiment runs DR_EVT's experimental power-usage data model on the
April 2024 F-DATA Fugaku workload. It produces one resource curve for
allocated nodes and a second curve for the aggregate minimum, average, and
maximum power-usage indicators of running jobs.

The experiment-specific scripts and full reproduction notes are kept in
[experimental/fugaku-power](../../experimental/fugaku-power/README.md).

## Input

The simulation input used locally is:

```text
f-data/traces_no_times/24_04_scheduling_trace.csv
```

It contains 420,450 jobs and these columns:

```text
job_submit_time,time_limit,num_nodes,duration,avgpcon,minpcon,maxpcon,exit_status
```

First, the historical production trace is replayed to measure its maximum
resource use: 142,167 allocated nodes. The simulation is then run with
`--total_nodes 142167`. This is an observed workload high-water mark, not the
machine's published 158,976-node capacity; neither trace stores total system
capacity.

## Replay the Historical Trace First

Measure the maximum allocation from the real Fugaku start/end times before
choosing the simulator size:

```bash
RESULTS=/tmp/dr_evt_fugaku_power
mkdir -p "$RESULTS"
TRACER=/path/to/tracer

"$TRACER" \
  f-data/traces/24_04_scheduling_trace.csv \
  --total_nodes 142167 \
  --resource_trace "$RESULTS/24_04_replay_nodes.csv" \
  --outfile "$RESULTS/24_04_replayed_jobs.csv" \
  --subfile "$RESULTS/24_04_replay_submissions.csv" \
  --subsumf "$RESULTS/24_04_replay_submission_summary.csv"

awk -F, 'NR > 1 && $3 > max { max = $3 } END { print max }' \
  "$RESULTS/24_04_replay_nodes.csv"
```

This replay processes 420,450 logged jobs and finds a maximum allocation of
142,167 nodes. `--total_nodes` only derives the replay output's `free_nodes`
column; it does not constrain historical allocation. The measured maximum is
used by the simulation command below.

## Run the Simulation

For this 420,450-job trace, passing the CSV directly is the simplest approach
and uses a modest amount of memory on the tested system:

```bash
SIMULATOR=/path/to/simulator

"$SIMULATOR" \
  f-data/traces_no_times/24_04_scheduling_trace.csv \
  --trace_type pcon \
  --trace_format simple \
  --timestamp_format epoch \
  --run_time_mode actual \
  --total_nodes 142167 \
  --outfile "$RESULTS/24_04_simulated.csv" \
  --resource_trace "$RESULTS/24_04_resources.csv"
```

The options have the following experiment-specific roles:

| Option | Purpose |
|---|---|
| `--trace_type pcon` | Reads and tracks the three power-usage input fields. |
| `--run_time_mode actual` | Uses `duration` as actual runtime while retaining `time_limit` for scheduling. |
| `--total_nodes 142167` | Uses the peak found by replaying the real April operational log. |

## Optional Progressive Loading

The single-file path loads every job before simulation. To reduce memory use,
progressive mode accepts a list of sorted CSV batches, loads each batch when
the simulation reaches it, and reclaims eligible completed records.

Prepare batches without splitting jobs that share a submission timestamp:

```bash
BATCH_DIR="$RESULTS/24_04_batches"

python3 experimental/fugaku-power/scripts/trace_tools/split_progressive_trace.py \
  f-data/traces_no_times/24_04_scheduling_trace.csv \
  "$BATCH_DIR" \
  --rows-per-file 50000
```

The command writes `$BATCH_DIR/file_list.txt`, with one absolute CSV path per
line. Every file is internally sorted, and the sequence is nondecreasing by
`job_submit_time`. For this experiment, the splitter divided the original
420,450-job April CSV into nine ordered pieces. Those nine files collectively
contain exactly the same jobs as the single input file; they are not nine
different workloads. The split exists only to test progressive loading. Run
the resulting batches with:

```bash
"$SIMULATOR" \
  --infile_list "$BATCH_DIR/file_list.txt" \
  --trace_type pcon \
  --trace_format simple \
  --timestamp_format epoch \
  --run_time_mode actual \
  --total_nodes 142167 \
  --job_store_capacity 50000 \
  --job_store_overflow grow \
  --check_memory_pressure 0.8 \
  --outfile "$RESULTS/24_04_simulated.csv" \
  --resource_trace "$RESULTS/24_04_resources.csv"
```

A capacity of 50,000 is not a strict upper bound with `grow`. Use
`--job_store_overflow abort` to make it strict, then reduce batch size or
increase capacity if a batch cannot fit with outstanding records.

Progressive input must be simulation format. The files under
`f-data/traces/` are derived from the real Fugaku operational log and contain
its recorded start and end times. They are suitable for historical replay
analysis, but replay mode is rejected by
`--infile_list`.

## Plot the Resource Trace

```bash
MPLCONFIGDIR=/tmp/matplotlib-cache \
python3 experimental/fugaku-power/scripts/analysis/plot_resource_trace.py \
  "$RESULTS/24_04_resources.csv" \
  --total-nodes 142167 \
  --output-dir docs/_static
```

This creates:

- `fugaku-node-allocation.png`: allocated nodes over elapsed simulation time,
  with the configured 142,167-node simulation limit marked.
- `fugaku-power-usage.png`: aggregate `minpcon`, `avgpcon`, and `maxpcon` for
  jobs running at each resource sample.

The power-usage resource columns are sums across running jobs. Despite their field
names, the plotted resource-level values are not per-node minima, means, or
maxima.

## Plot the Historical Replay

The replay input is derived from the real Fugaku operational log, not from a
DR_EVT simulation. The simulation computes new start times; the replay instead
uses the log's recorded `begin_time` and `end_time` values without invoking a
scheduler. The native replay above establishes the node high-water mark. Run
the power-aware replay helper over the same recorded intervals to add aggregate
power-usage columns for the comparable plots:

```bash
python3 experimental/fugaku-power/scripts/analysis/replay_power_trace.py \
  f-data/traces/24_04_scheduling_trace.csv \
  "$RESULTS/24_04_replay_resources.csv" \
  --total-nodes 142167

MPLCONFIGDIR=/tmp/matplotlib-cache \
python3 experimental/fugaku-power/scripts/analysis/plot_resource_trace.py \
  "$RESULTS/24_04_replay_resources.csv" \
  --total-nodes 142167 \
  --mode replay \
  --output-dir docs/_static
```

This produces `fugaku-replay-node-allocation.png` and
`fugaku-replay-power-usage.png`. The simulation and replay plotting paths use
the same fixed figure size and axes rectangle. Consequently, the day axes of
the node-allocation and power-usage figures align when stacked vertically.

The full April replay processed 420,450 logged job records corresponding to
the records used as simulation input. It preserved their real start/end times
and produced 533,015 historical resource timestamps over 39.446 days. It
measured:

| Replay metric | Result |
|---|---:|
| Peak allocated nodes | 142,167 |
| Share of published 158,976-node capacity | 89.427% |
| Peak aggregate average power usage | 14,625,141.205 W |
| Peak aggregate minimum power usage | 12,129,245.598 W |
| Peak aggregate maximum power usage | 15,092,094.726 W |

The plotted 142,167-node line is the replay-derived high-water mark used for
the simulation. The observed peak does not independently establish the
machine's full capacity; the published capacity is 158,976 nodes.

The replay and simulation do not match, nor should an exact match be assumed.
Such a match would require DR_EVT to reproduce Fugaku's production scheduler,
policies, constraints, and runtime environment exactly. In this experiment,
the 142,167-node DR_EVT simulation spans 33.585 days and reaches that configured
limit, with a peak aggregate average power usage of 15,648,631.199 W. The
real-log replay spans 39.446 days, peaks at 142,167 nodes, and reaches
14,625,141.205 W. The replay is the historical baseline against which the
simulated behavior is compared.

The historical curve also contains three conspicuous low-allocation troughs
near elapsed days 11.4, 15.8, and 19.0. Using 30,000 allocated nodes as a
visual threshold, their approximate spans are April 15 10:08 to April 16 00:58,
April 19 18:23 to 23:55, and April 23 00:26 to 05:21 PDT; their minima are 1,138,
13,513, and 17,938 nodes, respectively. The job log alone does not establish
that these were system downtimes: allocation never reaches zero, and 1,073,
311, and 769 jobs begin during the respective windows. They could reflect
reduced availability, maintenance, production scheduling constraints, or the
workload mix. This simulation used a constant 142,167-node pool and no
maintenance or time-varying-capacity input, so it cannot reproduce any
externally imposed capacity reductions. Low-allocation periods in the
simulation are instead consequences of its workload and scheduling decisions.

The submission timestamps are identical in the two inputs and span 33.515
days. The difference in overall duration is a queue-drain effect, not a
six-day job runtime. In particular, the job that finished last historically
waited 12.309 days before starting, although its runtime was only 1.320 days.
DR_EVT started that same job after 15.7 hours and therefore completed it much
earlier. The last-submitted job also started immediately in simulation but
waited 23.7 hours in the historical record.

The verified timings are shown below in Pacific Daylight Time. The heavier
divider separates the historical last-finishing job from the last-submitted
job.

<table class="docutils align-default">
  <thead>
    <tr>
      <th>Job</th>
      <th>Run</th>
      <th>Start</th>
      <th>Finish</th>
      <th>Wait</th>
      <th>Runtime</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td rowspan="2">Historical last-finishing job<br>Submitted Apr 29, 19:32</td>
      <td>Replay</td>
      <td>May 12, 02:56</td>
      <td>May 13, 10:38</td>
      <td>12.309 days</td>
      <td>31.7 h</td>
    </tr>
    <tr>
      <td>Simulation</td>
      <td>Apr 30, 11:13</td>
      <td>May 1, 18:54</td>
      <td>15.7 h</td>
      <td>31.7 h</td>
    </tr>
  </tbody>
  <tbody style="border-top: 3px solid #7f8c8d;">
    <tr>
      <td rowspan="2">Last-submitted job<br>Submitted May 7, 11:29</td>
      <td>Replay</td>
      <td>May 8, 11:11</td>
      <td>May 8, 12:51</td>
      <td>23.7 h</td>
      <td>1.67 h</td>
    </tr>
    <tr>
      <td>Simulation</td>
      <td>May 7, 11:29</td>
      <td>May 7, 13:09</td>
      <td>0</td>
      <td>1.67 h</td>
    </tr>
  </tbody>
</table>

## Measured Results

### Historical replay and simulation

The native C++ replay of all 420,450 real-log records completed without a
crash in 3.29 seconds with 87.2 MiB peak RSS. Its 840,901 event-level resource
samples found the same 142,167-node peak as the power-aware replay script,
which combines simultaneous events into 533,015 unique timestamps.

The single-file simulation then used that measured peak as
`--total_nodes 142167`. On `dane.llnl.gov` it completed all 420,450 jobs in
296.183 seconds (4:56.18 process wall time) with 144.6 MiB peak RSS:

| Metric | Result |
|---|---:|
| Resource samples | 840,901 |
| Simulated interval | 33.585 days |
| Peak allocated nodes | 142,167 (100.000% of configured limit) |
| Peak aggregate average power usage | 15,648,631.199 W |
| Peak aggregate minimum power usage | 14,615,637.813 W |
| Peak aggregate maximum power usage | 16,022,775.620 W |
| Average wait time | 3,214.97 seconds |
| Average turnaround time | 14,811.2 seconds |
| Peak queue length | 6,797 jobs |

### Earlier loading-mode comparison

Single-file and nine-batch progressive runs completed successfully on LLNL's
Dane system (`dane.llnl.gov`) on September 11, 2026. Both runs processed the
same 420,450 April jobs: one read the original CSV as a whole, while the other
read the same data split into nine ordered pieces to exercise progressive
loading. Only the input-loading mode changed; all simulation options were
identical. These earlier runs used the published 158,976-node capacity, so
their timing and queue statistics are a separate baseline from the
142,167-node simulation above.

Peak resident set size (peak RSS) is the largest amount of physical memory
held by the process during a run, as reported by `/usr/bin/time -v`. It does
not mean input-file size or total virtual address space.

| Metric | Single file | Progressive |
|---|---:|---:|
| Batch preparation wall time | Not required | 1.51 seconds |
| Simulator wall time | 220.117 seconds | 219.28 seconds |
| Process wall time | 220.15 seconds | 219.57 seconds |
| Total workflow wall time | 220.15 seconds | 221.08 seconds |
| Peak resident memory | 169.0 MiB | 55.6 MiB |
| Completed jobs | 420,450 | 420,450 |

The 0.58-second process-time difference is only 0.264% and should not be
treated as a meaningful performance difference from one run of each mode.
Progressive loading reduced peak memory by 113.4 MiB, or about threefold, but
the single-file run's 169.0 MiB peak remained modest. Including batch
preparation, the single-file workflow was 0.93 seconds faster and is the
simpler choice for this trace when memory is available.

<!--
The single-file and progressive simulation runs produced byte-for-byte
identical output files. Their SHA-256
digests were:

| Trace | SHA-256 |
|---|---|
| Scheduled jobs | `3d194d549dc57da1a6647370543e53faca47017e7261afd6acc2d736cfa04502` |
| Resource history | `695c1f833ddccce02c4127c7f99bae2fbbad62cce8a136d1c6b00fea939525` |
-->

The common output and scheduling results were:

| Metric | Result |
|---|---:|
| Resource samples | 840,901 |
| Simulated interval | 33.585 days |
| Peak allocated nodes | 158,976 (100.000% of capacity) |
| Peak aggregate average power usage | 17,540,017.803 W |
| Average wait time | 1,394.87 seconds |
| Average turnaround time | 12,991.1 seconds |
| Peak queue length | 4,022 jobs |

The simulator's separately reported average queue length differed slightly:
361.249 jobs for single-file input and 361.254 jobs for progressive input.
This did not affect either output trace.

### Simulator Implementation Scaling

The Python reference scheduler and C++ simulator were also run on cumulative
prefixes of those nine April pieces. The row labeled `1` uses only the first
piece, row `2` uses the first two pieces, and so on; row `9` reconstructs the
same complete 420,450-job April dataset used by the single-file run. These are
input-size scaling points, not nine distinct workloads. A final larger point
combines the unsplit March and April 2024 traces. Each point used the same jobs
and simulation options on the LLNL Dane system.

| Input | Jobs | Python wall time | C++ wall time | Python peak RSS | C++ peak RSS |
|---:|---:|---:|---:|---:|---:|
| 1 | 50,329 | 20.30 s | 10.34 s | 0.79 GiB | 28.4 MiB |
| 2 | 100,333 | 76.58 s | 42.77 s | 2.58 GiB | 48.1 MiB |
| 3 | 150,333 | 122.62 s | 69.67 s | 4.47 GiB | 58.2 MiB |
| 4 | 200,333 | 208.67 s | 120.42 s | 6.23 GiB | 87.9 MiB |
| 5 | 250,333 | 263.64 s | 146.37 s | 8.08 GiB | 97.8 MiB |
| 6 | 301,983 | 287.16 s | 155.37 s | 9.38 GiB | 108.0 MiB |
| 7 | 352,055 | 341.43 s | 171.73 s | 11.80 GiB | 117.9 MiB |
| 8 | 402,055 | 399.67 s | 194.27 s | 13.49 GiB | 167.2 MiB |
| 9 | 420,450 | 425.97 s | 220.09 s | 14.25 GiB | 169.0 MiB |
| March + April | 928,736 | 849.63 s | 353.70 s | 50.88 GiB | 298.8 MiB |

Python completed the entire nine-batch trace, so no failure threshold was
reached within this dataset. The full Python run required 7.10 minutes and
14.25 GiB, versus 3.67 minutes and 169.0 MiB for C++. Python was therefore
feasible on Dane, while C++ was 1.94 times faster and used 86.3 times less
memory. The April prefix series ended because all nine April batches had been
included, not because Python encountered a resource limit. The raw
measurements are in
[`simulator_implementation_scaling.csv`](../../experimental/fugaku-power/simulator_implementation_scaling.csv).

The larger two-month input also completed in both implementations. Python
required 14.16 minutes and 50.88 GiB, while C++ required 5.90 minutes and
298.8 MiB. For that input, C++ was 2.40 times faster and used 174.4 times less
memory. Both implementations completed all 928,736 jobs and reported the same
final simulation time.

:::{figure} ../_static/simulator_implementation_scaling.png
:alt: Dual-axis implementation-scaling comparison with job count on the x-axis, wall time on the left y-axis, and peak resident memory on the right y-axis for Python-reference and C++ simulation through the combined March and April 2024 Fugaku traces on LLNL Dane.
:width: 100%
:::

### Simulation Resource Curves

:::{figure} ../_static/fugaku-node-allocation.png
:alt: Simulated Fugaku allocated-node count over 33.585 days, reaching the replay-derived 142,167-node configured limit repeatedly before draining at the end.
:width: 100%
:::

:::{figure} ../_static/fugaku-power-usage.png
:alt: Simulated aggregate minimum, average, and maximum Fugaku power usage over 33.585 days.
:width: 100%
:::

### Historical Replay Resource Curves

:::{figure} ../_static/fugaku-replay-node-allocation.png
:alt: Historically replayed Fugaku allocated-node count over 39.446 days, peaking at the marked 142,167-node replay-derived high-water line.
:width: 100%
:::

:::{figure} ../_static/fugaku-replay-power-usage.png
:alt: Historically replayed aggregate minimum, average, and maximum Fugaku power usage over 39.446 days.
:width: 100%
:::

## Reference Scheduler

An adapted copy of the readable Python EASY reference scheduler is included at
`experimental/fugaku-power/scripts/python_reference_scheduler.py`. It
understands the Fugaku `duration` and power-usage fields and can generate the same
six resource columns for manageable subsets. The full April workload should
use the C++ simulator because the reference implementation prioritizes clarity
over large-trace performance. Use progressive input when reducing memory is
more important than minimizing preparation steps.
