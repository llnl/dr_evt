# Wait Queues

The FCFS scheduler can use `circular`, `deque`, `multimap`, or `block` wait
queues. The default is **`circular`**. This section collects the implementation
guides, testing instructions, and the evidence behind that default in one
place.

## Default queue and selection

Use the default **`circular`** queue for FCFS workloads unless a workload-specific
benchmark says otherwise:

```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator trace.csv --priority_policy fcfs
```

`deque` remains the simplest fallback. The `block` and `multimap` queues remain available
for comparison, testing, and research.

## Benchmark record

The November 12, 2026 record was collected from the current `main` build on
LLNL's `dane` system (Intel(R) Xeon(R) Platinum
8480+ CPUs; 112 cores and 256 GB memory) using
[`tests/test_traces/scale/huge_10000jobs.csv`](https://github.com/LLNL/dr_evt/blob/main/tests/test_traces/scale/huge_10000jobs.csv)
(10,001 jobs), 500 nodes, and
FCFS with EASY backfilling. Values are the mean of 10 end-to-end runs; `±` is
the population standard deviation.

The [captured samples and summary](../_static/wait-queue-benchmark-2026-11-12.json)
retain each run's timing and workload settings. The contributor confirmed the
machine and branch; the logs do not record the exact revision, compiler, or
build flags. Times were recorded to 0.001-second precision.

**Benchmark script:** [`tests/benchmark_wait_queues.sh`](../../tests/benchmark_wait_queues.sh)

The timing includes trace parsing, scheduling and backfilling, event handling,
and trace output. It is not an isolated wait-queue microbenchmark. The queue
implementation is the intended variable, but the complete difference cannot be
attributed solely to queue operations.

These measurements characterize this workload and configuration only. Results
can change with the trace's arrival pattern, job sizes and durations, queue
depth, backfill policy, and machine.

For sampled hot-path measurements within the circular FCFS scheduler, see
[Optimization History](performance-analysis/OPTIMIZATION_HISTORY.md). That historical profile complements this
end-to-end queue comparison; the two measurements use different workloads and
should not be compared numerically.

![Mean end-to-end runtime relative to deque; lower is better.](../_static/wait-queue-benchmark.svg)

| Implementation | Time (s) | vs deque | Change | Peak queue | Correctness |
| --- | ---: | ---: | ---: | ---: | --- |
| `deque` | 0.3546 ± 0.0009 | 1.00× | baseline | 8,586 | baseline |
| `multimap` | 4.3524 ± 0.0060 | 12.27× | +1,127.4% | 8,586 | PASS |
| **`circular`** | **0.3303 ± 0.0033** | **0.93×** | **−6.9%** | **8,586** | **PASS** |
| Python reference | 12.4554 ± 0.0199 | 35.13× | +3,412.5% | N/A | PASS |
| `block`-4 | 1.3667 ± 0.0025 | 3.85× | +285.4% | 8,586 | PASS |
| `block`-8 | 1.4145 ± 0.0023 | 3.99× | +298.9% | 8,586 | PASS |
| `block`-16 | 1.5071 ± 0.0018 | 4.25× | +325.0% | 8,586 | PASS |
| `block`-32 | 1.7656 ± 0.0022 | 4.98× | +397.9% | 8,586 | PASS |
| `block`-64 | 2.5954 ± 0.0014 | 7.32× | +631.9% | 8,586 | PASS |
| `block`-128 | 4.3110 ± 0.0023 | 12.16× | +1,115.7% | 8,586 | PASS |
| `block`-256 | 7.3226 ± 0.0044 | 20.65× | +1,965.0% | 8,586 | PASS |

`circular` was fastest overall, and `block`-4 was the fastest block size. Compared
with the previously published means, EASY deque and circular runtimes fell
15.8% and 16.3%, respectively; multimap fell 23.9%. Circular
uses 6.9% less time than deque, so the default queue recommendation is unchanged.

The 10 runs produced identical simulated-job output across every measured C++
queue variant, including `multimap`. The Python reference is an end-to-end
baseline. Its CSV schema is normalized before its job order and start/end times
are compared with the C++ `deque` schedule using a `0.001` tolerance.

### Conservative backfill result

Five end-to-end runs on November 12 of `huge_2000jobs.csv` with 500 nodes,
full conservative backfilling (queue depth 2,000), and `run_time_mode=limit`
produced identical C++ schedules and matching Python schedules. This is a
separate workload from the EASY table above. Values are means; `±` is the
population standard deviation.

| Implementation | Time (s) | vs deque | Change | Correctness |
| --- | ---: | ---: | ---: | --- |
| `deque` | 1.2064 ± 0.0020 | 1.00× | baseline | baseline |
| **`circular`** | **1.1468 ± 0.0031** | **0.95×** | **−4.9%** | **PASS** |
| Python hybrid reference | 59.6378 ± 0.0535 | 49.43× | +4,843.5% | PASS |

### Hybrid backfill result

Ten end-to-end runs on November 12 of `huge_10000jobs.csv` with 500 nodes,
`run_time_mode=limit`, reservation depth 32, and queue depth 32 produced
matching C++ and Python schedules. Values are means; `±` is the population
standard deviation.

| Implementation | Time (s) | vs deque | Change | Correctness |
| --- | ---: | ---: | ---: | --- |
| `deque` | 0.0896 ± 0.0012 | 1.00× | baseline | baseline |
| **`circular`** | **0.0863 ± 0.0013** | **0.96×** | **−3.7%** | **PASS** |
| Python hybrid reference | 2.9752 ± 0.0095 | 33.21× | +3,220.5% | PASS |

The new circular mean is 2.5% above the previously published 0.0842 s;
it still uses 3.7% less time than deque in the same capture.

### Periodic hybrid backfill result

Ten end-to-end runs on November 12 of `huge_10000jobs.csv` with 500 nodes,
`run_time_mode=limit`, a 30-second backfill interval, reservation depth 32,
and queue depth 500 measured the periodic circular scheduler below. The Python
reference was intentionally skipped. There is no periodic deque variant, so
this is not a queue-to-queue speedup comparison.

| Implementation | Time (s) | Correctness |
| --- | ---: | --- |
| `circular` | 0.2858 ± 0.0018 | periodic scheduler only; Python SKIP |

This is 33.0% slower than the previously published queue-depth-500 mean of
0.2149 ± 0.0046 s. The captures do not isolate the cause of the difference.

The earlier queue-depth-32 capture below is retained as a separate Python
comparison. It used ten runs, the same trace and node count, a 30-second
interval, and reservation depth 32; it was not rerun on November 12.

| Implementation | Time (s) | vs circular | Correctness |
| --- | ---: | ---: | --- |
| `circular` | 0.2455 ± 0.0866 | 1.00× | periodic scheduler only |
| Python hybrid reference | 77.7686 ± 0.4932 | 316.78× | PASS |

Reproduce the current benchmark modes with:

```bash
# EASY and hybrid, using the default trace
tests/benchmark_wait_queues.sh

# EASY only, using the 2,000-job trace
tests/benchmark_wait_queues.sh --backfill-policy easy \
  tests/test_traces/scale/huge_2000jobs.csv

# Hybrid only, with the recorded depth settings
tests/benchmark_wait_queues.sh --backfill-policy hybrid \
  --reservation-depth 32 \
  --backfill-queue-depth 32 \
  tests/test_traces/scale/huge_10000jobs.csv

# Periodic hybrid backfill. This measures the circular C++ scheduler; the
# Python oracle is skipped by default because it is impractically slow on
# long periodic traces.
tests/benchmark_wait_queues.sh --backfill-policy hybrid \
  --backfill-interval 30 \
  --reservation-depth 32 \
  --backfill-queue-depth 500 \
  tests/test_traces/scale/huge_10000jobs.csv

# Optional periodic Python differential check on a smaller trace. It can
# still take substantially longer than the C++ run.
tests/benchmark_wait_queues.sh --backfill-policy hybrid \
  --backfill-interval 30 \
  --reservation-depth 32 \
  --backfill-queue-depth 32 \
  --with-python-reference \
  tests/test_traces/scale/huge_2000jobs.csv

# Periodic conservative benchmark: 30 s, protect planned waiting jobs,
# and examine a 500-job prefix
tests/benchmark_wait_queues.sh --backfill-policy conservative \
  --backfill-interval 30 \
  --backfill-queue-depth 500
```

For every positive `--backfill-interval`, the benchmark skips the Python
reference unless `--with-python-reference` is supplied. This keeps the normal
10,000-job periodic benchmark practical; use the opt-in check only with a
small trace.

Run the same benchmark with:

```bash
tests/benchmark_wait_queues.sh
```

By default the script runs both comparisons. The EASY comparison tests `deque`,
`multimap`, `circular`, every supported `block` size, and the Python reference.
The hybrid comparison tests the deque and circular implementations plus the
Python oracle at queue and reservation depth 32. Each C++ comparison is
byte-for-byte against its deque baseline; Python schedules use a `0.001` time
tolerance.

To reproduce only the historical EASY table:

```bash
tests/benchmark_wait_queues.sh --backfill-policy easy
```

### Hybrid backfill comparison

Hybrid backfilling protects the first 32 waiting jobs considered in a pass.
The block and multimap queues do not implement this reservation algorithm, so
benchmark the deque and circular C++ implementations plus the Python oracle
separately:

```bash
tests/benchmark_wait_queues.sh --backfill-policy hybrid
```

Pass `--reservation-depth N` or `--backfill-queue-depth N` to model a
deployment-specific setting. Keep the reservation depth no larger than queue
depth: jobs outside the examined prefix cannot be protected. A depth of `1`
protects the head; a depth covering the eligible queue gives conservative
protection. Supported policy combinations are in
[Scheduling Loops](../user-guide/SCHEDULER_LOOPS.md#supported-policy-combinations).

The command byte-compares the C++ deque and circular schedules, then compares
the Python hybrid schedule with deque using a `0.001` time tolerance. It does
not replace the EASY results above; record the hybrid results in a separate
table with the workload, node count, run count, reservation depth, mean, and
standard deviation.

The unit and differential coverage is listed under
[Wait-queue tests](https://github.com/LLNL/dr_evt/blob/main/tests/README.md#wait-queue-tests).

## Queue guides

```{toctree}
:maxdepth: 2

BLOCK_WAIT_QUEUE
OUTPUT_TRACE_BUFFERS
```
