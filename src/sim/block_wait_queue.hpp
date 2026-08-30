/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_SIM_BLOCK_WAIT_QUEUE_HPP
#define DR_EVT_SIM_BLOCK_WAIT_QUEUE_HPP

#include <vector>
#include <optional>
#include <unordered_map>
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
 */
class BlockWaitQueue {
public:
    /**
     * Constructor
     * @param block_size Maximum jobs per block (tuning parameter)
     * @param immediate_erase If true, erase from multi_index immediately on removal.
     *                        If false (default), use lazy deletion with periodic compaction.
     */
    explicit BlockWaitQueue(size_t block_size = 128, bool immediate_erase = false);

    /**
     * Insert a job into the queue
     * Complexity: O(1) amortized
     */
    void insert_job(job_no_t job_id,
                    sim_time_t submit_time,
                    tdiff_t runtime_estimate,
                    num_nodes_t nodes_requested);

    /**
     * Mark a job as removed (lazy deletion)
     * Complexity: O(1) with global job_id -> block mapping
     */
    void mark_removed(job_no_t job_id);

    /**
     * Find a backfill candidate job
     *
     * Pre-filters blocks by:
     * 1. Time constraint: block.min_runtime must fit in window
     * 2. Resource constraint: block.min_nodes must fit available resources
     *
     * Then scans qualifying blocks in FCFS (sequential) order.
     *
     * @param available_nodes Resources currently available
     * @param current_time Current simulation time
     * @param reservation_time FCFS head reservation time
     * @return job_id if found, nullopt otherwise
     *
     * Complexity: O(B × S) where B = blocks scanned, S = jobs scanned per block
     *             Typically B = 1-2, S = 10-50 due to pre-filtering
     */
    std::optional<job_no_t> find_backfill_candidate(
        num_nodes_t available_nodes,
        sim_time_t current_time,
        sim_time_t reservation_time);

    /**
     * Iterate over all active (non-removed) jobs in FCFS order
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
        bool removed;
    };

    // Boost multi-index container with 4 indexes
    struct by_runtime {};
    struct by_nodes {};
    struct by_job_id {};

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
            >,

            // Index 3: Hashed by job_id for O(1) lookup
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<by_job_id>,
                boost::multi_index::member<JobEntry, job_no_t, &JobEntry::job_id>
            >
        >
    >;

    struct BlockInfo {
        JobBlock block;

        // Block-level metadata for pre-filtering
        tdiff_t min_runtime;
        num_nodes_t min_nodes;
        num_nodes_t max_nodes;
        size_t active_count;
        bool metadata_dirty;

        BlockInfo() : min_runtime(0), min_nodes(0), max_nodes(0),
                     active_count(0), metadata_dirty(true) {}

        void update_metadata();
        tdiff_t get_min_runtime();
        num_nodes_t get_min_nodes();
    };

    void compact_block(BlockInfo& block_info);

    const size_t m_block_size;                        // Tuning parameter
    const bool m_immediate_erase;                     // Deletion policy
    std::vector<BlockInfo> m_blocks;                  // All blocks
    size_t m_current_block_idx;                       // Block being filled
    bool m_needs_resort;                              // Blocks need re-sorting
    std::unordered_map<job_no_t, size_t> m_job_to_block;  // O(1) removal

    size_t m_total_jobs;                              // Including removed
    size_t m_active_count;                            // Non-removed only

    mutable Stats m_stats;                            // Performance stats
};

// Template implementation
template<typename Func>
void BlockWaitQueue::for_each_active(Func&& func) const {
    for (const auto& block_info : m_blocks) {
        const auto& seq = block_info.block.get<0>();  // Sequential index
        for (const auto& job : seq) {
            if (!job.removed) {
                func(job.job_id);
            }
        }
    }
}

} // namespace dr_evt

#endif // DR_EVT_SIM_BLOCK_WAIT_QUEUE_HPP
