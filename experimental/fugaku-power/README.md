# Fugaku Power-Usage Experiments

This directory contains the trace-preparation, simulation, validation, and
plotting workflow for DR_EVT's experimental power-usage trace model. The
source data is the F-DATA Fugaku workload dataset described in
[Scientific Data](https://doi.org/10.1038/s41597-025-05633-1).

## Dataset and Capacity

The local dataset used by this workflow is outside the repository:

```text
/p/vast1/f-data/RIKEN/
├── *.parquet
├── traces/                  # replay traces with begin_time and end_time
└── traces_no_times/         # simulation traces without begin/end times
```

The April 2024 simulation input,
`traces_no_times/24_04_scheduling_trace.csv`, contains 420,450 jobs. The
experiment uses 158,976 total nodes, the published full-system Fugaku node
count. A sweep of the corresponding replay trace found a peak historical
allocation of 142,167 nodes, or 89.427% of that capacity. The trace records
requests and allocations, but does not itself declare total system capacity.

The simulation trace contains:

```text
job_submit_time,time_limit,num_nodes,duration,avgpcon,minpcon,maxpcon,exit_status
```

Select the power-usage data model explicitly with `--trace_type pcon`. In
standard mode the three power-usage columns are ignored.

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
RESULTS=/tmp/dr_evt_fugaku_power
mkdir -p "$RESULTS"

"$SIMULATOR" \
  /p/vast1/f-data/RIKEN/traces_no_times/24_04_scheduling_trace.csv \
  --trace_type pcon \
  --trace_format simple \
  --timestamp_format epoch \
  --run_time_mode actual \
  --total_nodes 158976 \
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
  /p/vast1/f-data/RIKEN/traces_no_times/24_04_scheduling_trace.csv \
  "$BATCH_DIR" \
  --rows-per-file 50000
```

The splitter preserves the header, validates nondecreasing
`job_submit_time`, and never separates jobs with the same submission time.
It writes absolute paths to `$BATCH_DIR/file_list.txt`.

Run those batches with:

```bash
"$SIMULATOR" \
  --infile_list "$BATCH_DIR/file_list.txt" \
  --trace_type pcon \
  --trace_format simple \
  --timestamp_format epoch \
  --run_time_mode actual \
  --total_nodes 158976 \
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
find /p/vast1/f-data/RIKEN/traces_no_times -maxdepth 1 -type f \
  -name '*_scheduling_trace.csv' -print | LC_ALL=C sort \
  > /tmp/dr_evt_fugaku_power/monthly_file_list.txt

"$SIMULATOR" \
  --infile_list /tmp/dr_evt_fugaku_power/monthly_file_list.txt \
  --trace_type pcon \
  --trace_format simple \
  --timestamp_format epoch \
  --run_time_mode actual \
  --total_nodes 158976 \
  --resource_trace /tmp/dr_evt_fugaku_power/all_months_resources.csv
```

Progressive mode supports simulation input only. Files under `traces/`
contain historical `begin_time` and `end_time` and are replay inputs, so
they cannot be supplied to `--infile_list`.

## Plotting

Generate the two documentation plots after either simulation command:

```bash
MPLCONFIGDIR=/tmp/matplotlib-cache \
python3 experimental/fugaku-power/scripts/analysis/plot_resource_trace.py \
  "$RESULTS/24_04_resources.csv" \
  --total-nodes 158976 \
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
  --nodes 158976 \
  --outdir /tmp/dr_evt_fugaku_power/reference
```

It intentionally favors clarity over performance. It completed the full
420,450-job April trace on LLNL's Dane system, but used 14.25 GiB and took
425.97 seconds, versus 169.0 MiB and 220.09 seconds for C++. Prefer the C++
simulator for routine full-trace runs. On a combined 928,736-job March-April
input, Python used 50.88 GiB and took 849.63 seconds; C++ used 298.8 MiB and
took 353.70 seconds.

The replay trace can be analyzed separately to compare historical allocation
with simulated allocation:

```text
/p/vast1/f-data/RIKEN/traces/24_04_scheduling_trace.csv
```

## Data Notes

- `avgpcon` is total average power across the allocated nodes for one job.
- `minpcon` and `maxpcon` are the corresponding summed node minima/maxima.
- `avgpcon = econ × 3600 / duration`.
- The trace-generation script repairs the known corrupted negative
  `avgpcon` value in the March 2021 source data.
- Fugaku has 48 compute cores and 32 GiB HBM per node.
