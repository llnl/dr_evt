# Wait Queues

The FCFS scheduler can use circular, deque, multimap, or block-based wait
queues. The default is circular. This section collects the implementation
guides, testing instructions, and the evidence behind that default in one
place.

## Default queue and selection

Use the default circular queue for FCFS workloads unless a workload-specific
benchmark says otherwise:

```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator trace.csv --priority_policy fcfs
```

Deque remains the simplest fallback. Block and multimap queues remain available
for comparison, testing, and research.

## Benchmark record

This record was collected in September 2026 using the default 10K-job
benchmark trace with 500 nodes on an Intel Sapphire Rapids node (112 cores,
256 GB memory). Values are the mean of 10 end-to-end runs; `±` is the
population standard deviation.

The timing includes trace parsing, scheduling and backfilling, event handling,
and trace output. It is not an isolated wait-queue microbenchmark. The queue
implementation is the intended variable, but the complete difference cannot be
attributed solely to queue operations.

| Implementation | Time (s) | Relative to deque |
| --- | ---: | --- |
| Deque | 3.142 ± 0.052 | baseline |
| Multimap | not yet measured | — |
| **Circular** | **0.786 ± 0.002** | **75% faster (0.25×)** |
| Block-4 | 4.739 ± 0.003 | 51% slower (1.51×) |
| Block-8 | 4.282 ± 0.007 | 36% slower (1.36×) |
| Block-16 | 3.956 ± 0.001 | 26% slower (1.26×) |
| Block-32 | 4.057 ± 0.005 | 29% slower (1.29×) |
| Block-64 | 4.352 ± 0.004 | 38% slower (1.39×) |
| Block-128 | 4.383 ± 0.007 | 40% slower (1.40×) |
| Block-256 | 4.712 ± 0.005 | 50% slower (1.50×) |
| Python reference | not yet measured | — |

The 10 runs produced identical simulated-job output across the measured C++
queue variants. The multimap and Python entries are intentionally retained as
unmeasured rows so future results can be added using the same methodology. The
Python reference is useful as an end-to-end baseline, but is not byte-compared
with C++ because it emits a different CSV schema.

Run the benchmark with:

```bash
tests/benchmark_block_sizes.sh
```

The script tests deque, multimap, circular, every supported block size, and
the Python reference. It compares the C++ queue outputs with deque for
correctness.

## Queue guides

```{toctree}
:maxdepth: 2

design-decisions/CIRCULAR_QUEUE
BLOCK_WAIT_QUEUE
OUTPUT_TRACE_BUFFERS
```
