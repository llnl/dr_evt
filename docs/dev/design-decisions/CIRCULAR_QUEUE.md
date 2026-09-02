# Circular Buffer Wait Queue Implementation

## Quick Summary

Circular queue is now DR_EVT's default FCFS wait-queue implementation.
It's structurally identical to the deque-based FCFSScheduler it replaced
as default - same job entry layout, same lazy-mark-and-compact removal,
same `pop_front()` head consumption, same indexed backfill scan - but
backed by `boost::circular_buffer` instead of `std::deque`. **Measured
14% faster than deque on average (10K jobs, 2K nodes).**

**Command:** `./simulator trace.csv` (default) or explicitly `./simulator trace.csv --queue_impl circular`

## Performance Results (10K jobs, 2K nodes, average of 3 trials)

| Implementation | Time (s) | vs Deque |
|----------------|----------|----------|
| Deque          | 0.723    | 1.00x (baseline) |
| Block-16       | 0.814    | 1.13x (12.6% slower) |
| **Circular**   | **0.619** | **0.86x (14.4% faster)** |

Circular is also 23.9% faster than block-16 on the same trace.

✅ Produces byte-for-byte identical output to deque, in every one of the 34
comprehensive differential test cases (`tests/test_fcfs_comprehensive.sh`),
and separately confirmed on the 10K-job benchmark trace itself.

Exact numbers will vary by hardware; re-run `tests/benchmark_block_sizes.sh`
(which also benchmarks circular alongside every block size) to measure on
your own machine.

## Why It's Faster Than Deque

`boost::circular_buffer` stores its elements in one contiguous block of
memory. `std::deque` is typically implemented as a sequence of fixed-size
chunks - indexed access (`operator[]`, used throughout the backfill scan)
requires a division/modulo to find the right chunk, then an offset within
it. A circular buffer's indexed access is a single, direct offset into that
one contiguous block. Both containers give O(1) `push_back`/`pop_front`, so
this difference in indexed-access cost is the main source of the speedup.

## Why It's Faster Than Block Queue

Block queue's overhead is dominated by maintaining two red-black trees per
block (see `BLOCK_QUEUE.md`: ~70% of its overhead). Circular queue has
no per-block bookkeeping at all - it's structurally the same linear scan
deque already does, just over faster-to-index storage.

## The Fixed-Capacity Trade-off

Unlike `std::deque`, `boost::circular_buffer` has a capacity fixed at
construction. A `push_back()` on a full buffer overwrites the oldest
element rather than growing - silently dropping a job would be a serious
correctness bug, so this needed explicit handling:

- **Default capacity (0):** sized to the job trace's own length at
  construction. Since `insert_job()` is called at most once per entry in
  the trace over the scheduler's lifetime (see
  `Simulation::submit_job()`), this guarantees the buffer can never
  overflow - the same "always correct, uses the worst-case amount of
  memory upfront" trade-off as sizing any fixed buffer to the largest
  possible input.
- **`--circular_capacity SIZE`:** an explicit, smaller capacity, trading
  that guarantee for a smaller initial allocation.
- **`--circular_overflow {abort|grow}`:** what happens if an insert
  exceeds the capacity actually chosen.
  - `abort` throws `std::runtime_error`, caught by the top-level handler
    (or reported back to the gRPC client), ending the simulation cleanly
    with exit code 1.
  - `grow` (default) reallocates to double the current capacity via
    `boost::circular_buffer::set_capacity()`, which preserves every
    existing entry - confirmed directly against Boost's own
    documentation and behavior: `set_capacity()` only drops elements
    when shrinking below the current size, never when growing.

## Testing

### Quick Test
```bash
# Correctness test (deque vs multimap vs block vs circular)
./tests/test_fcfs_comprehensive.sh --correctness

# Performance benchmark (10K jobs, includes circular alongside all block sizes)
./tests/benchmark_block_sizes.sh
```

### Manually Testing Capacity/Overflow
```bash
# Force growth: capacity 10 is far smaller than most real traces
./simulator trace.csv --priority_policy fcfs --queue_impl circular \
    --circular_capacity 10 --circular_overflow grow

# Force a clean abort instead
./simulator trace.csv --priority_policy fcfs --queue_impl circular \
    --circular_capacity 10 --circular_overflow abort
```

## Usage

### Command-Line Options

```bash
# Default (deque)
./simulator trace.csv --priority_policy fcfs

# Circular queue, default capacity (sized to the job trace)
./simulator trace.csv --priority_policy fcfs --queue_impl circular

# Circular queue with an explicit capacity and abort-on-overflow
./simulator trace.csv --priority_policy fcfs --queue_impl circular \
    --circular_capacity 500 --circular_overflow abort
```

### Factory Pattern

```cpp
std::unique_ptr<SchedulerBase> scheduler = create_scheduler(
    total_nodes,
    job_data,
    BackfillPolicy::EASY,
    PriorityPolicy::FCFS,
    RuntimeEstimateMode::USE_LIMIT,
    QueueImplementation::CIRCULAR,
    128,   // block_size (unused for circular)
    0,     // circular_capacity: 0 = size of job_data
    CircularOverflowPolicy::GROW
);
```

### Also Reachable via Protobuf

Both protobuf-based configuration paths support `queue_impl`,
`circular_capacity`, and `circular_overflow` alongside their existing
`block_size` field:

- The gRPC service's `InitRequest` message (`src/proto/dr_evt_service.proto`)
- The `.prototext` config file format read via `--config`
  (`src/proto/dr_evt_params.proto`)

An empty `queue_impl` string, empty `circular_overflow` string, or a `0`
`circular_capacity` keeps `Sim_Params`' own defaults (circular; grow;
sized to the job trace) in both paths.

## Recommendations

### Production
✅ **circular is the default** - no action needed for most FCFS workloads;
measured faster than both deque and block queue.

✅ **Use `--queue_impl deque`** if you'd rather not depend on Boost, or want
the simplest, most battle-tested option; the performance difference
measured here is real but not dramatic.

### Research/Testing
✅ Use the default capacity (0) unless you specifically want to test the
overflow/grow path or bound memory usage - a wrong guess at capacity with
`--circular_overflow grow` costs nothing but a doubling reallocation partway
through the run.

## Files

### Core Implementation
- `src/sim/scheduler_circular_fcfs.{hpp,cpp}` - the scheduler itself
- `src/sim/scheduler_base.cpp` - factory function
- `src/params/sim_params.{hpp,cpp}` - CLI parsing, `QueueImplementation::CIRCULAR`,
  `CircularOverflowPolicy`

### Tests
- `tests/test_fcfs_comprehensive.sh` - correctness verification (deque vs
  multimap vs block vs circular)
- `tests/benchmark_block_sizes.sh` - performance comparison (all block
  sizes plus circular)

### Protobuf Wiring
- `src/proto/dr_evt_service.proto`, `src/proto/dr_evt_server.cpp` - gRPC
  service path
- `src/proto/dr_evt_params.proto`, `src/proto/dr_evt_params.cpp` -
  `.prototext` config file path

## References

- Boost Circular Buffer: https://www.boost.org/doc/libs/release/libs/circular_buffer/
- `BLOCK_QUEUE.md` - the queue implementation this one was written to
  outperform, and the source of the multi-index overhead analysis referenced
  above
