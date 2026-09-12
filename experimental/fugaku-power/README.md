# Fugaku Power-Usage Experiments

This directory contains the trace-preparation, simulation, validation, and
plotting workflow for DR_EVT's experimental power-usage trace model. The
source data is the F-DATA Fugaku workload dataset described in
[Scientific Data](https://doi.org/10.1038/s41597-025-05633-1).

## Dataset and Capacity

The local dataset used by this workflow is outside the repository:

```text
f-data/
├── *.parquet
├── traces/                  # real-log replay traces with begin/end times
└── traces_no_times/         # simulation traces without begin/end times
```

The April 2024 simulation input,
`traces_no_times/24_04_scheduling_trace.csv`, contains 420,450 jobs. A full
event replay of the corresponding real operational log found a peak allocation
of 142,167 nodes, or 89.427% of Fugaku's published 158,976-node capacity. The
simulation is then run with that observed replay peak as its configured node
count. This is a workload high-water mark, not an independent measurement of
the machine's full capacity; neither CSV declares total system capacity.

The simulation trace contains:

```text
job_submit_time,time_limit,num_nodes,duration,avgpcon,minpcon,maxpcon,exit_status
```

Select the power-usage data model explicitly with `--trace_type pcon`. In
standard mode the three power-usage columns are ignored.

Determine the simulation node count by replaying the historical trace first:

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

The native replay produces `142167`: a maximum of 142,167 allocated nodes.
Its `--total_nodes` value only derives `free_nodes`; it does not limit the
recorded allocation. The April simulation below uses the measured maximum for
`--total_nodes`.

## Scripts

See [scripts/README.md](scripts/README.md) for the complete script inventory,
requirements, and commands. The main experiment tools are:

- `scripts/trace_tools/create_scheduling_trace.py`: convert source Parquet
  data into replay CSV traces.
- `scripts/trace_tools/create_traces_without_times.py`: derive simulation
  inputs by removing historical start/end times.
- `scripts/trace_tools/split_progressive_trace.py`: split one sorted CSV into
  safe progressive-loading batches and write its `--infile_list` file.
- `scripts/python_reference_scheduler.py`: experiment-specific copy of the
  Python EASY scheduler with `duration` and power-usage resource tracking.
- `scripts/analysis/replay_power_trace.py`: replay historical start/end times
  without scheduling and produce a power-aware resource trace.
- `scripts/analysis/plot_resource_trace.py`: create the node-allocation and
  power-usage figures from a six-column resource trace.
- `scripts/analysis/plot_simulator_implementation_scaling.py`: compare
  measured Python-reference and C++ simulator wall-time and memory scaling as
  cumulative batches are added.

The repository's shared `scripts/python_reference_scheduler.py` remains
unchanged.

## April 2024 Simulation

A normal single-file run loads all 420,450 jobs before simulation:

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

`time_limit` remains the scheduler's planning estimate. With
`--run_time_mode actual`, the input `duration` alias supplies each job's
simulated execution time.

## Progressive Batch Loading

Progressive loading accepts a text file containing one simulation-trace path
per line. It loads one entire file at a time, advances through that file's last
submission time, reclaims eligible completed records, and then loads the next
file. It is not adaptive streaming within one CSV.

For the single April trace, first create smaller sorted files:

```bash
BATCH_DIR=/tmp/dr_evt_fugaku_power/24_04_batches

python3 experimental/fugaku-power/scripts/trace_tools/split_progressive_trace.py \
  f-data/traces_no_times/24_04_scheduling_trace.csv \
  "$BATCH_DIR" \
  --rows-per-file 50000
```

The splitter preserves the header, validates nondecreasing
`job_submit_time`, and never separates jobs with the same submission time.
It writes absolute paths to `$BATCH_DIR/file_list.txt`. With the command above,
the original 420,450-job April CSV becomes nine ordered files that collectively
contain the exact same data. The nine files are used only to compare
progressive loading with reading the original CSV as a whole; they do not
represent nine different workloads.

Run those batches with:

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

`--job_store_capacity` is an initial capacity, not a strict maximum when
`--job_store_overflow grow` is selected. A batch must fit together with any
records that are not yet reclaimable. Use `abort` instead of `grow` when
exceeding the selected capacity must fail, and adjust the batch size to match.

For a multi-month experiment, the existing monthly no-times files can be the
batches directly:

```bash
find f-data/traces_no_times -maxdepth 1 -type f \
  -name '*_scheduling_trace.csv' -print | LC_ALL=C sort \
  > /tmp/dr_evt_fugaku_power/monthly_file_list.txt

# Replace this with the maximum found by replaying the selected months.
REPLAY_MAX_NODES=142167

"$SIMULATOR" \
  --infile_list /tmp/dr_evt_fugaku_power/monthly_file_list.txt \
  --trace_type pcon \
  --trace_format simple \
  --timestamp_format epoch \
  --run_time_mode actual \
  --total_nodes "$REPLAY_MAX_NODES" \
  --resource_trace /tmp/dr_evt_fugaku_power/all_months_resources.csv
```

Progressive mode supports simulation input only. Files under `traces/` are
derived from the real Fugaku operational log and contain its historical
`begin_time` and `end_time` values. They are replay inputs, so they cannot be
supplied to `--infile_list`.

## Plotting

Generate the two documentation plots after either simulation command:

```bash
MPLCONFIGDIR=/tmp/matplotlib-cache \
python3 experimental/fugaku-power/scripts/analysis/plot_resource_trace.py \
  "$RESULTS/24_04_resources.csv" \
  --total-nodes 142167 \
  --output-dir docs/_static
```

The outputs are `fugaku-node-allocation.png` and `fugaku-power-usage.png`.
The power-usage columns in a resource trace are sums of the corresponding
values for all running jobs at each sample; they are not per-node averages.

## Reference and Replay Checks

The copied Python reference scheduler provides an independent, small-scale
comparison and power-usage resource-trace generation:

```bash
python3 experimental/fugaku-power/scripts/python_reference_scheduler.py \
  /path/to/a/manageable_trace.csv \
  --nodes 142167 \
  --outdir /tmp/dr_evt_fugaku_power/reference
```

It intentionally favors clarity over performance. It completed the full
420,450-job April trace on LLNL's Dane system, but used 14.25 GiB and took
425.97 seconds, versus 169.0 MiB and 220.09 seconds for C++. Prefer the C++
simulator for routine full-trace runs. On a combined 928,736-job March-April
input, Python used 50.88 GiB and took 849.63 seconds; C++ used 298.8 MiB and
took 353.70 seconds.

The memory values are peak resident set size (peak RSS) from
`/usr/bin/time -v`: the maximum physical memory held by the process, not the
input-file size or total virtual address space.

Replay the real Fugaku log's historical start/end times separately to compare
observed resource use with the simulated schedule. The replay input is not
simulator output: this is an event replay of recorded operation and does not
invoke a scheduler. The native `tracer` command above establishes the node
maximum. The following helper replays the same recorded intervals while adding
the power-usage columns needed by the plots:

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

The replay figures are `fugaku-replay-node-allocation.png` and
`fugaku-replay-power-usage.png`. All node-allocation and power-usage figures
use the same fixed axes rectangle, so their elapsed-day axes align when the
figures are stacked vertically.

The full April replay processed 420,450 jobs at 533,015 unique resource
timestamps over 39.446 days. Peak allocation was 142,167 nodes (89.427% of
the published capacity), and peak aggregate average power usage was
14,625,141.205 W. The native C++ replay also completed all 420,450 records
without a crash in 3.29 seconds with 87.2 MiB peak RSS and independently found
the same 142,167-node peak from its event-level resource trace.

The replay is a historical baseline, not an expected match for DR_EVT's
schedule. An exact match would require DR_EVT to reproduce Fugaku's production
scheduler, policies, constraints, and runtime environment exactly. Here the
142,167-node simulation instead spans 33.585 days, reaches that configured
limit, and peaks at 15,648,631.199 W aggregate average power usage. It
completed in 296.183 seconds with 144.6 MiB peak RSS on `dane.llnl.gov`.

## Data Notes

- `avgpcon` is total average power across the allocated nodes for one job.
- `minpcon` and `maxpcon` are the corresponding summed node minima/maxima.
- `avgpcon = econ × 3600 / duration`.
- The trace-generation script repairs the known corrupted negative
  `avgpcon` value in the March 2021 source data.
- Fugaku has 48 compute cores and 32 GiB HBM per node.
