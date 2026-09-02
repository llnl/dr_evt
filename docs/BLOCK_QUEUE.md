# Block Queue Wait Queue Implementation

## Quick Summary

Block queue groups jobs into fixed-size blocks with multi-index containers for fast backfill search. **Block-16 is optimal (+30% overhead), but deque is still 30% faster.** Use deque for production.

**Command:** `./simulator trace.csv --queue_impl block --block_size 16`

## Performance Results (10K jobs, 2K nodes)

| Block Size | Time (s) | vs Deque | Blocks | Notes |
|------------|----------|----------|--------|-------|
| **Deque**  | **1.40** | **1.00x** | N/A    | **fastest** |
| Block-4    | 2.33     | 1.66x    | 1,926  | too many blocks |
| Block-8    | 1.91     | 1.36x    | 963    | |
| **Block-16** | **1.82** | **1.30x** | **481** | **optimal** |
| Block-32   | 2.04     | 1.46x    | 271    | |
| Block-64   | 2.17     | 1.55x    | 136    | |
| Block-128  | 2.52     | 1.80x    | 68     | |
| Block-256  | 2.75     | 1.96x    | 34     | too few blocks |

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
2. **Runtime tree** (red-black tree) - sorted by runtime
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

✅ Dynamic metadata filtering (min_runtime, min_nodes)
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
# Correctness test (deque vs multimap vs block)
./tests/test_fcfs_comprehensive.sh

# All 7 block sizes with custom trace
./tests/benchmark_block_sizes.sh path/to/trace.csv
```

See [`BLOCK_QUEUE_TESTING.md`](BLOCK_QUEUE_TESTING.md) for details.

## Usage

### Command-Line Options

```bash
# Default (deque)
./simulator trace.csv --priority_policy fcfs

# Block queue with specific size
./simulator trace.csv --priority_policy fcfs \
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
    RuntimeEstimateMode::USE_LIMIT,
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

Tried: maintaining min_runtime/min_nodes without blocks.
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
    tdiff_t get_min_runtime() const;
    num_nodes_t get_min_nodes() const;
};
```

### Backfill Search Algorithm

```cpp
for (auto& block_info : m_blocks) {
    // Skip entire block if empty
    if (block_info.active_count == 0) continue;
    
    // Skip if all jobs too long
    if (current_time + block_info.get_min_runtime() >= reservation_time)
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
✅ **Use deque (default)** - 30% faster, simpler code

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
- `docs/BLOCK_QUEUE_TESTING.md` - Testing guide
- `docs/BLOCK_QUEUE_U_CURVE_ANALYSIS.md` - Detailed U-curve analysis

## References

- Boost Multi-Index: https://www.boost.org/doc/libs/release/libs/multi_index/
- EASY Backfilling: Lifka (1995), "The ANL/IBM SP Scheduling System"
- Conservative Backfilling: Feitelson & Weil (1998)
