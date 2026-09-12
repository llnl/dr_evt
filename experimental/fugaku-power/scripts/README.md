# Fugaku Experiment Scripts

Run commands from the DR_EVT repository root unless a script's notes say
otherwise.

## Requirements

Use Python 3.7 or newer. The trace splitter and copied reference scheduler use
only the Python standard library. Plotting and Parquet analysis require:

```bash
python3 -m pip install pandas pyarrow numpy matplotlib
```

## Simulation and Plotting

### `python_reference_scheduler.py`

This is an experiment-specific copy of DR_EVT's Python EASY reference
scheduler. It leaves `scripts/python_reference_scheduler.py` unchanged.

The copy adds two Fugaku behaviors:

- when both fields exist, `time_limit` is used for scheduling reservations
  and `duration` is used as the actual runtime;
- resource output includes aggregate power-usage values in the `avgpcon`,
  `minpcon`, and `maxpcon` columns for currently running jobs.

```bash
python3 experimental/fugaku-power/scripts/python_reference_scheduler.py \
  TRACE.csv --nodes 158976 --outdir OUTPUT_DIR
```

The implementation is intended as a readable reference. It can run the full
April trace on a sufficiently large node, but measured substantially higher
memory use and runtime than C++; prefer C++ for routine full-trace runs.

### `analysis/plot_resource_trace.py`

Reads a power-usage resource trace with columns `time`, `allocated_nodes`,
`avgpcon`, `minpcon`, and `maxpcon`. It produces separate node-allocation
and power-usage PNG figures.

```bash
python3 experimental/fugaku-power/scripts/analysis/plot_resource_trace.py \
  RESOURCE_TRACE.csv --total-nodes 158976 --output-dir docs/_static
```

Use `--max-points` to control plot downsampling; the default is 50,000.

### `analysis/plot_simulator_implementation_scaling.py`

Plots measured Python-reference and C++ simulator wall time and peak-memory
scaling from the April cumulative-batch and combined March-April benchmark
data:

```bash
python3 experimental/fugaku-power/scripts/analysis/plot_simulator_implementation_scaling.py \
  experimental/fugaku-power/simulator_implementation_scaling.csv \
  --output docs/_static/simulator_implementation_scaling.png
```

## Progressive-Loading Preparation

### `trace_tools/split_progressive_trace.py`

Splits one submit-time-sorted simulation CSV into header-preserving batches and
writes an absolute-path list suitable for `simulator --infile_list`. Jobs
with the same `job_submit_time` remain in the same batch.

```bash
python3 experimental/fugaku-power/scripts/trace_tools/split_progressive_trace.py \
  INPUT.csv OUTPUT_DIR --rows-per-file 50000
```

The default list is `OUTPUT_DIR/file_list.txt`. Override it with
`--list-file PATH`.

## Trace Preparation

The following scripts use configuration constants near the top of each file.
Run them from the external F-DATA directory so their relative input and output
paths resolve there:

```bash
cd /p/vast1/f-data/RIKEN
```

### `trace_tools/create_scheduling_trace.py`

Converts configured monthly Parquet inputs into replay CSV files under
  `traces/`. It maps timestamps to epoch seconds, includes power-usage data, converts
exit state, and repairs the known corrupted March 2021 average-power value.

```bash
python3 /path/to/dr_evt/experimental/fugaku-power/scripts/trace_tools/create_scheduling_trace.py
```

### `trace_tools/create_traces_without_times.py`

Removes `begin_time` and `end_time` from `traces/*.csv` and writes
simulation inputs under `traces_no_times/`.

```bash
python3 /path/to/dr_evt/experimental/fugaku-power/scripts/trace_tools/create_traces_without_times.py
```

### `trace_tools/select_trace_period.py`

Selects a date range by `job_submit_time`. Dates use `MM-DD-YYYY`; pass
`None` for an open endpoint.

```bash
python3 experimental/fugaku-power/scripts/trace_tools/select_trace_period.py \
  INPUT.csv OUTPUT.csv 04-01-2024 04-08-2024
```

## Dataset Analysis

These scripts currently use configuration constants at the top of each file
rather than command-line arguments.

### `trace_tools/analyze_nodes.py`

Compares requested, allocated, and used node counts in the configured Parquet
file.

### `trace_tools/analyze_time_limits.py`

Analyzes time-limit utilization across `traces_no_times/*.csv` and creates
linear- and log-scale histograms.

### `analysis/analyze_time_fields.py`

Checks relationships among arrival, queue, scheduling, start, end, deletion,
and duration fields in the configured Parquet file.

### `analysis/analyze_power_per_node.py`

Computes `avgpcon / nnumu` from the configured Parquet inputs and creates
linear- and log-scale power-per-node histograms.
