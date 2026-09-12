# Fugaku Power-Usage Simulation Experiment

This experiment runs DR_EVT's experimental power-usage data model on the
April 2024 F-DATA Fugaku workload. It produces one resource curve for
allocated nodes and a second curve for the aggregate minimum, average, and
maximum power-usage indicators of running jobs.

The experiment-specific scripts and full reproduction notes are kept in
[experimental/fugaku-power](../../experimental/fugaku-power/README.md).

## Input

The simulation input used locally is:

```text
/p/vast1/f-data/RIKEN/traces_no_times/24_04_scheduling_trace.csv
```

It contains 420,450 jobs and these columns:

```text
job_submit_time,time_limit,num_nodes,duration,avgpcon,minpcon,maxpcon,exit_status
```

The run uses 158,976 nodes, the full Fugaku system size. The corresponding
historical replay trace peaks at 142,167 allocated nodes (89.427% of that
capacity), but total capacity is external machine metadata rather than a value
stored in either trace.

## Run the Simulation

For this 420,450-job trace, passing the CSV directly is the simplest approach
and uses a modest amount of memory on the tested system:

```bash
RESULTS=/tmp/dr_evt_fugaku_power
mkdir -p "$RESULTS"
SIMULATOR=/path/to/simulator

"$SIMULATOR" \
  /p/vast1/f-data/RIKEN/traces_no_times/24_04_scheduling_trace.csv \
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

The options have the following experiment-specific roles:

| Option | Purpose |
|---|---|
| `--trace_type pcon` | Reads and tracks the three power-usage input fields. |
| `--run_time_mode actual` | Uses `duration` as actual runtime while retaining `time_limit` for scheduling. |
| `--job_store_capacity 50000` | Sets the initial job-record capacity. |
| `--job_store_overflow grow` | Allows the job store to grow beyond its initial capacity. |
| `--check_memory_pressure 0.8` | Refuses growth projected to exceed 80% of currently available memory. |

## Optional Progressive Loading

The single-file path loads every job before simulation. To reduce memory use,
progressive mode accepts a list of sorted CSV batches, loads each batch when
the simulation reaches it, and reclaims eligible completed records.

Prepare batches without splitting jobs that share a submission timestamp:

```bash
BATCH_DIR="$RESULTS/24_04_batches"

python3 experimental/fugaku-power/scripts/trace_tools/split_progressive_trace.py \
  /p/vast1/f-data/RIKEN/traces_no_times/24_04_scheduling_trace.csv \
  "$BATCH_DIR" \
  --rows-per-file 50000
```

The command writes `$BATCH_DIR/file_list.txt`, with one absolute CSV path per
line. Every file is internally sorted, and the sequence is nondecreasing by
`job_submit_time`. Run the resulting batches with:

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

A capacity of 50,000 is not a strict upper bound with `grow`. Use
`--job_store_overflow abort` to make it strict, then reduce batch size or
increase capacity if a batch cannot fit with outstanding records.

Progressive input must be simulation format. The files under
`/p/vast1/f-data/RIKEN/traces/` contain historical start and end times and
are suitable for replay analysis, but replay mode is rejected by
`--infile_list`.

## Plot the Resource Trace

```bash
MPLCONFIGDIR=/tmp/matplotlib-cache \
python3 experimental/fugaku-power/scripts/analysis/plot_resource_trace.py \
  "$RESULTS/24_04_resources.csv" \
  --total-nodes 158976 \
  --output-dir docs/_static
```

This creates:

- `fugaku-node-allocation.png`: allocated nodes over elapsed simulation time,
  with the 158,976-node capacity marked.
- `fugaku-power-usage.png`: aggregate `minpcon`, `avgpcon`, and `maxpcon` for
  jobs running at each resource sample.

The power-usage resource columns are sums across running jobs. Despite their field
names, the plotted resource-level values are not per-node minima, means, or
maxima.

## Measured Results

Single-file and nine-batch progressive runs completed successfully on LLNL's
Dane system (`dane.llnl.gov`) on September 11, 2026. The comparison changed
only the input-loading mode; all simulation options were identical.

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

Both modes produced byte-for-byte identical output files. Their SHA-256
digests were:

| Trace | SHA-256 |
|---|---|
| Scheduled jobs | `3d194d549dc57da1a6647370543e53faca47017e7261afd6acc2d736cfa04502` |
| Resource history | `695c1f833ddccce02c4127c7f99bae2fbbad62cce8a136d1c6b00fea939525` |

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
prefixes of the April progressive batches. A final larger point combines the
unsplit March and April 2024 traces. Each point used the same jobs and
simulation options on the LLNL Dane system.

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

:::{figure} ../_static/fugaku-node-allocation.png
:alt: Simulated Fugaku allocated-node count over 33.585 days, reaching the 158,976-node system capacity repeatedly before draining at the end.
:width: 100%
:::

:::{figure} ../_static/fugaku-power-usage.png
:alt: Simulated aggregate minimum, average, and maximum Fugaku power usage over 33.585 days.
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
