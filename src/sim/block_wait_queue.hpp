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
 * Groups jobs into blocks and maintains block-level metadata (min runtime,
 * min nodes) for fast pre-filtering before scanning.
 *
 * @tparam BlockSize Maximum jobs per block (must be power of 2)
 */
template<size_t BlockSize>
class BlockWaitQueue {
public:
    /**
     * Constructor
     * Block size is compile-time constant (template parameter)
     */
    BlockWaitQueue();

    static constexpr size_t block_size() { return BlockSize; }
    static constexpr size_t block_size_shift() {
        return compute_log2_constexpr(BlockSize);
    }

    /**
     * Insert a job into the queue
     * Complexity: O(1) amortized
     */
    void insert_job(job_no_t job_id,
                    sim_time_t submit_time,
                    tdiff_t runtime_estimate,
                    num_nodes_t nodes_requested);

    /**
     * Remove a job from the queue (immediate deletion)
     * Complexity: O(log n) where n = jobs in the block (multi_index erase)
     */
    void remove(job_no_t job_id);

    /**
     * Find AND REMOVE a backfill candidate job (combined operation - no double search!)
     *
     * Pre-filters blocks by:
     * 1. Time constraint: block.min_runtime must fit in window
     * 2. Resource constraint: block.min_nodes must fit available resources
     *
     * Then scans qualifying blocks in FCFS (sequential) order.
     * When found, IMMEDIATELY removes the job (we have the iterator already).
     *
     * @param available_nodes Resources currently available
     * @param current_time Current simulation time
     * @param reservation_time FCFS head reservation time
     * @return job_id if found and removed, nullopt otherwise
     *
     * Complexity: O(B × S) where B = blocks scanned, S = jobs scanned per block
     *             Typically B = 1-2, S = 10-50 due to pre-filtering
     */
    std::optional<job_no_t> find_and_remove_backfill_candidate(
        num_nodes_t available_nodes,
        sim_time_t current_time,
        sim_time_t reservation_time);

    /**
     * Iterate over all jobs in FCFS order
     * Used for FCFS head selection and general queue operations
     */
    template<typename Func>
    void for_each_active(Func&& func) const;

    /**
     * Get total number of jobs (including removed)
     */
    size_t size() const { return m_total_jobs; }

    /**
     * Get number of active (non-removed) jobs
     */
    size_t active_count() const { return m_active_count; }

    /**
     * Check if queue is empty (no active jobs)
     */
    bool empty() const { return m_active_count == 0; }

    /**
     * Get statistics for tuning/debugging
     */
    struct Stats {
        size_t num_blocks;
        size_t blocks_skipped_empty;      // active_count == 0
        size_t blocks_checked;
        size_t blocks_skipped_time;
        size_t blocks_skipped_resource;
        size_t jobs_scanned;
    };
    Stats get_stats() const { return m_stats; }
    void reset_stats() { m_stats = {}; }

private:
    struct JobEntry {
        job_no_t job_id;
        sim_time_t submit_time;
        tdiff_t runtime_estimate;
        num_nodes_t nodes_requested;
    };

    // Boost multi-index container with 3 indexes (removed job_id hash - unnecessary!)
    struct by_runtime {};
    struct by_nodes {};

    using JobBlock = boost::multi_index::multi_index_container<
        JobEntry,
        boost::multi_index::indexed_by<
            // Index 0: Sequential (FCFS/arrival order)
            boost::multi_index::sequenced<>,

            // Index 1: Ordered by runtime (ascending)
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<by_runtime>,
                boost::multi_index::member<JobEntry, tdiff_t, &JobEntry::runtime_estimate>
            >,

            // Index 2: Ordered by nodes (ascending)
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<by_nodes>,
                boost::multi_index::member<JobEntry, num_nodes_t, &JobEntry::nodes_requested>
            >
        >
    >;

    struct BlockInfo {
        JobBlock block;
        size_t active_count;

        BlockInfo() : active_count(0) {}

        // Query min values directly from multi_index (O(1) - first element in sorted index)
        tdiff_t get_min_runtime() const {
            if (active_count == 0) return std::numeric_limits<tdiff_t>::max();
            auto& idx = block.template get<by_runtime>();
            return idx.empty() ? std::numeric_limits<tdiff_t>::max() : idx.begin()->runtime_estimate;
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

    std::deque<BlockInfo> m_blocks;                   // All blocks in FCFS order (never sort!)
    size_t m_current_block_idx;                       // Block being filled
    job_no_t m_first_job_id;                          // First job ID for offset calc

    size_t m_total_jobs;                              // Including removed
    size_t m_active_count;                            // Non-removed only

    mutable Stats m_stats;                            // Performance stats
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
                                            tdiff_t runtime_estimate,
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
    current.block.push_back({job_id, submit_time, runtime_estimate, nodes_requested});
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

        tdiff_t min_runtime = block_info.get_min_runtime();
        if (current_time + min_runtime >= reservation_time) {
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

            if (current_time + it->runtime_estimate < reservation_time) {
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
