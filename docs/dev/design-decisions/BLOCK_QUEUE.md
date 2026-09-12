# Block Wait Queue Implementation

The block wait queue groups FCFS entries into fixed-size blocks. Each block
uses multiple indices so a backfill search can reject unsuitable blocks before
scanning their jobs.

Measured performance and the benchmark procedure are maintained in
[Wait Queues](../WAIT_QUEUES.md#benchmark-record). User-facing selection and
block-size options are defined in
[Command-Line Options](../../user-guide/command-line.md).

## Data structure

Each block maintains:

1. a sequential index for FCFS order;
2. a runtime index; and
3. a node-count index.

The latter two provide the minimum runtime and node request in each block.
Empty blocks and blocks whose minima cannot fit the current backfill window
are skipped.

```cpp
std::optional<job_no_t> find_and_remove_backfill_candidate(
    num_nodes_t available_nodes,
    sim_time_t current_time,
    sim_time_t reservation_time);
```

## Search

For each block, the implementation:

1. skips the block when it has no active entries;
2. rejects it when every job is too long or too large;
3. scans the remaining entries in FCFS order; and
4. removes and returns the first eligible job.

Block size is a compile-time template parameter selected through the scheduler
factory. Power-of-two sizes allow block indices to use shifts.

## Tradeoffs

Smaller blocks increase block-iteration overhead. Larger blocks reduce the
number of blocks that metadata can eliminate and increase multi-index
maintenance. The measured comparison with `circular`, `deque`, and `multimap` queues
is kept in [Wait Queues](../WAIT_QUEUES.md).

## Implementation

- `src/sim/block_wait_queue.{hpp,cpp}` — block storage and search
- `src/sim/scheduler_block_fcfs.{hpp,cpp}` — FCFS scheduler integration
- `src/sim/scheduler_base.cpp` — scheduler factory

Test commands and fixtures are maintained in the
[wait-queue tests section of the Test Suite README](https://github.com/LLNL/dr_evt/blob/main/tests/README.md#wait-queue-tests).

## References

- [Boost Multi-Index](https://www.boost.org/doc/libs/release/libs/multi_index/)
- Lifka (1995), “The ANL/IBM SP Scheduling System”
- Feitelson and Weil (1998), conservative backfilling
