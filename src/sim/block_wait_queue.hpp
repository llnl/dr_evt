/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_BLOCK_WAIT_QUEUE_HPP
#define DR_EVT_SIM_BLOCK_WAIT_QUEUE_HPP

#include <deque>
#include <optional>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>

#include "common.hpp"

namespace dr_evt {

/**
 * Block-based wait queue with O(1) operations and efficient backfill search.
 *
 * Groups jobs into blocks and maintains block-level metadata (min run_time,
 * min nodes) for fast pre-filtering before scanning.
 *
 * @tparam BlockSize Maximum jobs per block (must be power of 2)
 */
template<size_t BlockSize>
class BlockWaitQueue {
public:
    /**
     * @brief Construct an empty block-based wait queue.
     * @details The block size is fixed by the BlockSize template parameter.
     */
    BlockWaitQueue();

    /** @brief Return the compile-time number of jobs per block. @return Block capacity as size_t. */
    static constexpr size_t block_size() { return BlockSize; }

    /** @brief Return log2(block_size()), used to map job IDs to blocks. @return Bit shift as size_t. */
    static constexpr size_t block_size_shift() {
        return compute_log2_constexpr(BlockSize);
    }

    /**
     * @brief Insert a waiting job in FCFS order.
     * @details
     * Appends to the current fixed-size block, creating a block when the
     * current one is full. Job identifiers are expected to be monotonically
     * increasing so remove() can locate their block by offset.
     * @param[in] job_id Trace job identifier.
     * @param[in] submit_time Job arrival time.
     * @param[in] run_time_estimate Time-limit estimate used for backfill fitting.
     * @param[in] nodes_requested Nodes requested by the job.
     * @note Amortized complexity is O(1).
     */
    void insert_job(job_no_t job_id,
                    sim_time_t submit_time,
                    tdiff_t run_time_estimate,
                    num_nodes_t nodes_requested);

    /**
     * @brief Remove a job immediately if it remains in the queue.
     * @details
     * A missing or already removed identifier is a no-op. Removing an entry
     * updates both per-block and queue-wide active counts.
     * @param[in] job_id Trace job identifier.
     * @note Complexity is O(log n), where n is jobs in the affected block.
     */
    void remove(job_no_t job_id);

    /**
     * @brief Find and remove the first FCFS-ordered job that can backfill.
     * @details
     * Blocks are first rejected using their minimum estimated runtime and
     * node request. Remaining blocks are searched in FCFS order; the first
     * arrived job that fits available nodes and finishes strictly before the
     * FCFS reservation is removed and returned.
     *
     * Pre-filters blocks by:
     * 1. Time constraint: block.min_run_time must fit in window
     * 2. Resource constraint: block.min_nodes must fit available resources
     *
     * Then scans qualifying blocks in FCFS (sequential) order.
     * When found, IMMEDIATELY removes the job (we have the iterator already).
     *
     * @param[in] available_nodes Resources currently available.
     * @param[in] current_time Current simulation time.
     * @param[in] reservation_time FCFS head reservation time.
     * @return Optional job_no_t: selected-and-removed job ID, or std::nullopt.
     *
     * Complexity: O(B × S) where B = blocks scanned, S = jobs scanned per block
     *             Typically B = 1-2, S = 10-50 due to pre-filtering
     */
    std::optional<job_no_t> find_and_remove_backfill_candidate(
        num_nodes_t available_nodes,
        sim_time_t current_time,
        sim_time_t reservation_time);

    /**
     * @brief Invoke a callable for every active job in FCFS order.
     * @details
     * Iteration visits each nonempty block in insertion order and then each
     * block's sequenced index. The callable receives only a Trace job ID.
     * @tparam Func Callable accepting a job_no_t.
     * @param[in] func Callable to invoke.
     */
    template<typename Func>
    void for_each_active(Func&& func) const;

    /** @brief Return jobs ever inserted, including jobs later removed. @return Count as size_t. */
    size_t size() const { return m_total_jobs; }

    /** @brief Return jobs currently waiting in the queue. @return Active-job count as size_t. */
    size_t active_count() const { return m_active_count; }

    /** @brief Report whether no active jobs remain. @return true when active_count() is zero. */
    bool empty() const { return m_active_count == 0; }

    /** @brief Return accumulated backfill-search instrumentation. @return Stats value snapshot. */
    struct Stats {
        /// Number of blocks currently allocated.
        size_t num_blocks;
        /// Blocks skipped because their active count is zero.
        size_t blocks_skipped_empty;
        /// Nonempty blocks inspected during a candidate search.
        size_t blocks_checked;
        /// Blocks skipped because no job can finish before the reservation.
        size_t blocks_skipped_time;
        /// Blocks skipped because no job can fit current resources.
        size_t blocks_skipped_resource;
        /// Individual jobs examined after block-level filtering.
        size_t jobs_scanned;
    };
    /** @brief Return accumulated backfill-search instrumentation. */
    Stats get_stats() const { return m_stats; }
    /** @brief Clear accumulated backfill-search instrumentation. */
    void reset_stats() { m_stats = {}; }

private:
    struct JobEntry {
        /// Stable identifier of the Trace job represented by this entry.
        job_no_t job_id;
        /// Arrival time used to reject future jobs during a search.
        sim_time_t submit_time;
        /// Requested time limit used to test whether a job fits a reservation.
        tdiff_t run_time_estimate;
        /// Nodes requested by the job.
        num_nodes_t nodes_requested;
    };

    // Boost multi-index container with 3 indexes (removed job_id hash - unnecessary!)
    struct by_run_time {};
    struct by_nodes {};

    using JobBlock = boost::multi_index::multi_index_container<
        JobEntry,
        boost::multi_index::indexed_by<
            // Index 0: Sequential (FCFS/arrival order)
            boost::multi_index::sequenced<>,

            // Index 1: Ordered by run_time (ascending)
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<by_run_time>,
                boost::multi_index::member<JobEntry, tdiff_t, &JobEntry::run_time_estimate>
            >,

            // Index 2: Ordered by nodes (ascending)
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<by_nodes>,
                boost::multi_index::member<JobEntry, num_nodes_t, &JobEntry::nodes_requested>
            >
        >
    >;

    struct BlockInfo {
        /// Multi-index container for this block's active jobs.
        JobBlock block;
        /// Number of active entries currently held in block.
        size_t active_count;

        BlockInfo() : active_count(0) {}

        // Query min values directly from multi_index (O(1) - first element in sorted index)
        tdiff_t get_min_run_time() const {
            if (active_count == 0) return std::numeric_limits<tdiff_t>::max();
            auto& idx = block.template get<by_run_time>();
            return idx.empty() ? std::numeric_limits<tdiff_t>::max() : idx.begin()->run_time_estimate;
        }

        num_nodes_t get_min_nodes() const {
            if (active_count == 0) return std::numeric_limits<num_nodes_t>::max();
            auto& idx = block.template get<by_nodes>();
            return idx.empty() ? std::numeric_limits<num_nodes_t>::max() : idx.begin()->nodes_requested;
        }
    };

    static constexpr size_t compute_log2_constexpr(size_t value) {
        return (value <= 1) ? 0 : 1 + compute_log2_constexpr(value >> 1);
    }

    /// Blocks in FCFS order; they are never reordered.
    std::deque<BlockInfo> m_blocks;
    /// Index of the block currently receiving newly inserted jobs.
    size_t m_current_block_idx;
    /// First inserted job ID, used to compute a block offset.
    job_no_t m_first_job_id;

    /// Jobs ever inserted, including entries subsequently removed.
    size_t m_total_jobs;
    /// Jobs currently active across all blocks.
    size_t m_active_count;

    /// Mutable instrumentation updated during const read/query operations.
    mutable Stats m_stats;
};

// Template implementation - moved to header for template instantiation
template<size_t BlockSize>
BlockWaitQueue<BlockSize>::BlockWaitQueue()
    : m_current_block_idx(0)
    , m_first_job_id(0)
    , m_total_jobs(0)
    , m_active_count(0)
    , m_stats{}
{
    static_assert(BlockSize > 0 && (BlockSize & (BlockSize - 1)) == 0,
                  "BlockSize must be a power of 2");
}

template<size_t BlockSize>
void BlockWaitQueue<BlockSize>::insert_job(job_no_t job_id,
                                            sim_time_t submit_time,
                                            tdiff_t run_time_estimate,
                                            num_nodes_t nodes_requested)
{
    if (m_total_jobs == 0) {
        m_first_job_id = job_id;
    }

    if (m_blocks.empty() || m_blocks[m_current_block_idx].block.size() >= BlockSize) {
        m_blocks.emplace_back();
        m_current_block_idx = m_blocks.size() - 1;
    }

    auto& current = m_blocks[m_current_block_idx];
    current.block.push_back({job_id, submit_time, run_time_estimate, nodes_requested});
    current.active_count++;

    m_total_jobs++;
    m_active_count++;
}

template<size_t BlockSize>
void BlockWaitQueue<BlockSize>::remove(job_no_t job_id)
{
    if (job_id < m_first_job_id) {
        return;
    }

    // Compile-time constant shift!
    constexpr size_t shift = block_size_shift();
    size_t block_idx = (job_id - m_first_job_id) >> shift;

    if (block_idx >= m_blocks.size()) {
        return;
    }

    auto& block_info = m_blocks[block_idx];
    auto& seq = block_info.block.template get<0>();

    // Linear scan - fast for small blocks, no hash table needed
    for (auto it = seq.begin(); it != seq.end(); ++it) {
        if (it->job_id == job_id) {
            seq.erase(it);
            block_info.active_count--;
            m_active_count--;
            return;
        }
    }
}

template<size_t BlockSize>
std::optional<job_no_t> BlockWaitQueue<BlockSize>::find_and_remove_backfill_candidate(
    num_nodes_t available_nodes,
    sim_time_t current_time,
    sim_time_t reservation_time)
{
    for (auto& block_info : m_blocks) {
        if (block_info.active_count == 0) {
            m_stats.blocks_skipped_empty++;
            continue;
        }

        m_stats.blocks_checked++;

        tdiff_t min_run_time = block_info.get_min_run_time();
        if (current_time + min_run_time >= reservation_time) {
            m_stats.blocks_skipped_time++;
            continue;
        }

        num_nodes_t min_nodes = block_info.get_min_nodes();
        if (min_nodes > available_nodes) {
            m_stats.blocks_skipped_resource++;
            continue;
        }

        auto& seq = block_info.block.template get<0>();
        for (auto it = seq.begin(); it != seq.end(); ++it) {
            m_stats.jobs_scanned++;

            if (it->submit_time > current_time) continue;
            if (it->nodes_requested > available_nodes) continue;

            if (current_time + it->run_time_estimate < reservation_time) {
                // Found a candidate! Remove it immediately (we have the iterator!)
                job_no_t found_job = it->job_id;
                seq.erase(it);  // Erase from all 3 indices (not 4!)
                block_info.active_count--;
                m_active_count--;
                return found_job;
            }
        }
    }

    return std::nullopt;
}

template<size_t BlockSize>
template<typename Func>
void BlockWaitQueue<BlockSize>::for_each_active(Func&& func) const {
    for (const auto& block_info : m_blocks) {
        if (block_info.active_count == 0) {
            continue;
        }

        const auto& seq = block_info.block.template get<0>();
        for (const auto& job : seq) {
            func(job.job_id);
        }
    }
}

// Explicit template instantiations for common block sizes
extern template class BlockWaitQueue<4>;
extern template class BlockWaitQueue<8>;
extern template class BlockWaitQueue<16>;
extern template class BlockWaitQueue<32>;
extern template class BlockWaitQueue<64>;
extern template class BlockWaitQueue<128>;
extern template class BlockWaitQueue<256>;

} // namespace dr_evt

#endif // DR_EVT_SIM_BLOCK_WAIT_QUEUE_HPP
