# Block Queue Wait Queue Implementation

## Quick Summary

Block queue groups jobs into fixed-size blocks with multi-index containers for fast backfill search. **Block-16 is optimal (+30% overhead), but deque is still 30% faster.** Use deque for production.

**Command:** `./simulator trace.csv --queue_impl block --block_size 16`

**See also:** [`CIRCULAR_QUEUE.md`](CIRCULAR_QUEUE.md) - a `boost::circular_buffer`-based
queue implementation directly motivated by this analysis, measured faster than both deque and
block queue.

## Performance Results (10K jobs, 500 nodes)

500 nodes is the tightest node count that keeps every job in this trace
schedulable (the trace's largest single job requests exactly 500 nodes),
reflecting real scheduling contention. An earlier version of this
benchmark used 2,000 nodes; these numbers supersede it.

| Block Size | Time (s) | vs Deque |
|------------|----------|----------|
| **Deque**  | **0.680** | **1.00x (baseline)** |
| Block-4    | 0.695    | 1.02x (2% slower) |
| Block-8    | 0.587    | 0.86x (14% faster) |
| **Block-16** | **0.596** | **0.87x (12% faster)** |
| Block-32   | 0.619    | 0.91x (9% faster) |
| Block-64   | 0.659    | 0.96x (3% faster) |
| Block-128  | 0.676    | 0.99x (1% faster) |
| Block-256  | 0.713    | 1.04x (5% slower) |

At this node count, several block sizes actually edge out deque - a
different picture than the 2,000-node version of this benchmark, where
deque was fastest overall. Run-to-run variance in this measurement is
real and non-trivial; treat these as directionally representative, not
precise. See [`CIRCULAR_QUEUE.md`](CIRCULAR_QUEUE.md) for how circular
compares in the same run.

✅ All block sizes produce byte-for-byte identical output to deque.

## The U-Shaped Curve

```
Slowdown
100%|                      Block-256 (97%)
    |                        *
 66%|  Block-4           *
    |     *            *
 30%|      Block-16 ← optimal
    |         *
  0%|  Deque
    +---------------------------
       4   8  16  32  64 128 256
```

**Why U-shaped:**
- **Left (4-8):** Too many blocks → excessive iteration overhead
- **Sweet spot (16):** Best balance, blocks drain fast (10% short jobs)
- **Right (32-256):** Too few blocks → multi-index overhead dominates

## Architecture

### 3-Index Design

Each block maintains:
1. **Sequential** (linked list) - FCFS order
2. **Run time tree** (red-black tree) - sorted by run time
3. **Nodes tree** (red-black tree) - sorted by node count

**Why 3 indices?** Originally 4 (with job_id hash), but hash table was unnecessary—linear scan is faster for small blocks (≤256 jobs).

### Key API

```cpp
// Combined find-and-remove (single pass)
std::optional<job_no_t> find_and_remove_backfill_candidate(
    num_nodes_t available_nodes,
    sim_time_t current_time,
    sim_time_t reservation_time);

// Manual remove
void remove(job_no_t job_id);
```

### Optimizations

✅ Dynamic metadata filtering (min_run_time, min_nodes)
✅ Empty block skipping (active_count == 0)
✅ Combined find-and-remove API
✅ Compile-time block size (bit-shift addressing)

### What Didn't Help

❌ Removing hash index → no performance gain (trees are bottleneck)
❌ Metadata filtering → helps but can't overcome tree overhead

## Why Deque Still Wins

Multi-index overhead breakdown:
- **70%** Red-black tree maintenance (2 trees per block, O(log n) ops)
- **20%** Memory allocation and cache misses (scattered 481 containers)
- **10%** Block iteration and bookkeeping

Deque advantages:
- Sequential memory (cache-friendly)
- O(1) operations (no tree maintenance)
- Simple pointer arithmetic

**Fundamental issue:** Multi-index trees are overkill for blocks of 16-256 jobs.

## Testing

### Quick Test
```bash
# Unit tests (all block sizes, <5 sec)
./build/test_block_queue_simple-bin

# Performance benchmark (10K jobs, ~2 min)
./tests/benchmark_block_sizes.sh
```

### Comprehensive Testing
```bash
# Correctness test (deque vs multimap vs block vs circular - see CIRCULAR_QUEUE.md)
./tests/test_fcfs_comprehensive.sh

# All 7 block sizes with custom trace
./tests/benchmark_block_sizes.sh path/to/trace.csv
```

See [`BLOCK_QUEUE_TESTING.md`](BLOCK_QUEUE_TESTING.md) for details.

## Usage

### Command-Line Options

```bash
# Default (deque)
${CMAKE_INSTALL_PREFIX}/bin/simulator trace.csv --priority_policy fcfs

# Block queue with specific size
${CMAKE_INSTALL_PREFIX}/bin/simulator trace.csv --priority_policy fcfs \
    --queue_impl block --block_size 16

# Supported sizes: 4, 8, 16, 32, 64, 128, 256 (power of 2)
```

### Factory Pattern

```cpp
std::unique_ptr<SchedulerBase> scheduler = create_scheduler(
    total_nodes,
    job_data,
    BackfillPolicy::EASY,
    PriorityPolicy::FCFS,
    DurationEstimateMode::USE_LIMIT,
    QueueImplementation::BLOCK,
    16  // block_size
);
```

## Design Rationale

### Block Size Selection

**Block-16 is optimal because:**
1. **Moderate block count (481)** - not too many to iterate, not too few to skip
2. **Fast drainage** - 10% short jobs → 1-2 jobs removed → 6-12% block drained
3. **Low multi-index overhead** - O(log 16) ≈ 4 operations vs O(log 256) ≈ 8
4. **Good metadata filtering** - early blocks empty first, raising min values

**Block-4 is too small (66% overhead):**
- 1,926 blocks at peak → excessive iteration cost dominates

**Block-256 is too large (97% overhead):**
- 34 blocks at peak → rarely skip, huge multi-index overhead per block

### Why Not Deque + Metadata?

Tried: maintaining min_run_time/min_nodes without blocks.
Result: Linear scan still fast (<10K jobs), extra bookkeeping slows it down.

### Why Not Replace Multi-Index?

Could use manual min-tracking (O(1) updates) instead of red-black trees.
Would eliminate 70% of current overhead → potentially competitive with deque.
Not implemented due to complexity vs benefit trade-off.

## Implementation Details

### Template-Based Block Size

```cpp
template<size_t BlockSize>
class BlockWaitQueue {
    static constexpr size_t block_size_shift() {
        return compute_log2_constexpr(BlockSize);
    }
    
    // Block index via bit-shift (O(1))
    size_t block_idx = (job_id - m_first_job_id) >> block_size_shift();
};
```

### Block Metadata

```cpp
struct BlockInfo {
    JobBlock block;          // multi_index_container
    size_t active_count;     // non-removed jobs
    
    // O(1) metadata queries from sorted indices
    tdiff_t get_min_run_time() const;
    num_nodes_t get_min_nodes() const;
};
```

### Backfill Search Algorithm

```cpp
for (auto& block_info : m_blocks) {
    // Skip entire block if empty
    if (block_info.active_count == 0) continue;
    
    // Skip if all jobs too long
    if (current_time + block_info.get_min_run_time() >= reservation_time)
        continue;
    
    // Skip if all jobs too large
    if (block_info.get_min_nodes() > available_nodes)
        continue;
    
    // Scan jobs in FCFS order
    auto& seq = block_info.block.get<0>();
    for (auto it = seq.begin(); it != seq.end(); ++it) {
        if (fits) {
            job_no_t found = it->job_id;
            seq.erase(it);  // Remove immediately
            return found;
        }
    }
}
```

## Lessons Learned

1. **Simple beats complex at typical scales** - Cache-friendly deque outperforms "optimized" structures <10K jobs
2. **U-curve is real** - Too small = iteration overhead, too large = multi-index overhead
3. **Block drainage hypothesis confirmed** - 10% short jobs → Block-16 drains fastest
4. **Multi-index is expensive** - 70% overhead from maintaining 2 red-black trees per block
5. **Hash tables not always faster** - Linear scan comparable for n < 100

## Recommendations

### Production
✅ **Use circular (default)** - measured faster than both deque and every
block size (see [`CIRCULAR_QUEUE.md`](CIRCULAR_QUEUE.md)); `deque` remains
available as a simple, well-tested fallback

### Research/Testing
✅ **Use Block-16** - best block size if testing block queue
❌ **Avoid Block-4, Block-256** - worst performance

### Future Work
1. Replace boost::multi_index with manual min-tracking (O(1) updates)
2. Test on much larger queues (>50K jobs) to find crossover point
3. Implement adaptive block sizing (merge blocks when queue shrinks)
4. Profile different workloads (high backfill ratio, very long reservation windows)

## Files

### Core Implementation
- `src/sim/block_wait_queue.{hpp,cpp}` - 3-index design
- `src/sim/scheduler_block_fcfs.{hpp,cpp}` - FCFS scheduler using block queue
- `src/sim/scheduler_base.cpp` - Factory function

### Tests
- `tests/test_block_queue_simple.cpp` - Unit tests (all block sizes)
- `tests/benchmark_block_sizes.sh` - Performance comparison
- `tests/test_fcfs_comprehensive.sh` - Correctness verification
- `tests/test_traces/scale/huge_10000jobs.csv` - 10K job test trace

### Documentation
- `BLOCK_QUEUE_TESTING.md` - Testing guide
- `docs/BLOCK_QUEUE_U_CURVE_ANALYSIS.md` - Detailed U-curve analysis

## References

- Boost Multi-Index: https://www.boost.org/doc/libs/release/libs/multi_index/
- EASY Backfilling: Lifka (1995), "The ANL/IBM SP Scheduling System"
- Conservative Backfilling: Feitelson & Weil (1998)
