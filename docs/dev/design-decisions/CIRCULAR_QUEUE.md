# Circular Buffer Wait Queue Implementation

The `circular` queue is the default FCFS wait queue. It preserves the same FCFS
and backfill behavior as the `deque` implementation while storing entries in one
contiguous `boost::circular_buffer`.

Measured performance and the benchmark procedure are maintained in
[Wait Queues](../WAIT_QUEUES.md#benchmark-record). User-facing selection and
capacity options are defined in
[Command-Line Options](../../user-guide/command-line.md).

## Storage and traversal

The scheduler uses constant-time insertion and head removal plus indexed
backfill scans. A circular buffer provides those operations over contiguous
storage, whereas a `deque` generally uses multiple storage blocks.

Removal is lazy: selected entries are marked during a scan and compacted when
they reach the front. This preserves stable job identifiers while avoiding
middle erasure.

## Capacity behavior

A `boost::circular_buffer` would overwrite its oldest element when full, so
the scheduler checks capacity before every insertion.

- A zero configured capacity is resolved from the loaded trace size.
- `grow` expands a full queue while preserving its entries.
- `abort` reports an error instead of expanding.

The corresponding flags are
`--wait_queue_capacity` and `--wait_queue_overflow`.

## Implementation

The scheduler is implemented in
`src/sim/scheduler_circular_fcfs.{hpp,cpp}` and constructed by the scheduler
factory in `src/sim/scheduler_base.cpp`. Configuration parsing lives in
`src/params/sim_params.{hpp,cpp}`.

## Related documentation

- [Wait Queues](../WAIT_QUEUES.md) — selection guidance and benchmark results
- [Output-Trace Buffers](../OUTPUT_TRACE_BUFFERS.md) — separate circular
  buffers used by trace output
- [Testing Guide](../../TESTING_GUIDE.md#validation-model) — validation approach
- [Boost Circular Buffer](https://www.boost.org/doc/libs/release/libs/circular_buffer/)
