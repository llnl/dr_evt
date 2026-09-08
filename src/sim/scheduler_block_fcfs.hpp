/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULER_BLOCK_FCFS_HPP
#define DR_EVT_SIM_SCHEDULER_BLOCK_FCFS_HPP

#include <deque>
#include "sim/scheduler_base.hpp"
#include "sim/block_wait_queue.hpp"

namespace dr_evt {

/**
 * FCFS (First-Come-First-Served) scheduler using BlockWaitQueue.
 *
 * Uses BlockWaitQueue for O(1) insert/remove and O(blocks) backfill search.
 * Maintains separate eligibility tracking (identical to FCFSScheduler).
 *
 * Key optimization over FCFSScheduler: BlockWaitQueue's find_backfill_candidate()
 * uses block-level metadata pre-filtering for faster backfill search.
 *
 * @tparam BlockSize Block size (must be power of 2)
 */
template<size_t BlockSize>
class BlockQueueFCFSScheduler : public SchedulerBase {
private:
    /// Block-indexed queue used for efficient candidate searches.
    BlockWaitQueue<BlockSize> m_wait_queue;

    // Eligibility tracking (identical to FCFSScheduler)
    struct JobArrival {
        /// Stable identifier of the Trace job represented by this arrival.
        job_no_t job_id;
        /// Arrival time used to determine eligibility.
        sim_time_t submit_time;
        /// True when this arrival has been scheduled and removed.
        bool removed;

        JobArrival(job_no_t id, sim_time_t submit)
            : job_id(id), submit_time(submit), removed(false) {}
    };
    /// Arrival records in FCFS order; separate from the block queue.
    std::deque<JobArrival> m_job_order;
    /// First arrival index with submit_time later than m_current_tracked_time.
    size_t m_eligible_end_idx;
    /// Latest time to which arrival eligibility has been synchronized.
    sim_time_t m_current_tracked_time;
    /// Removed entries inside the eligible prefix of m_job_order.
    size_t m_removed_count;

public:
    /** @copydoc SchedulerBase::SchedulerBase */
    BlockQueueFCFSScheduler(num_nodes_t total_nodes,
                            const Trace& job_data,
                            BackfillPolicy bf_policy)
        : SchedulerBase(total_nodes, job_data, bf_policy)
        , m_wait_queue()
        , m_eligible_end_idx(0)
        , m_current_tracked_time(0.0)
        , m_removed_count(0)
    {}

    /** @copydoc SchedulerBase::insert_job */
    void insert_job(job_no_t job_id, sim_time_t submit_time,
                   tdiff_t run_time_estimate, num_nodes_t nodes_requested) override;

    /** @copydoc SchedulerBase::schedule */
    std::vector<job_no_t> schedule(
        num_nodes_t free_nodes,
        const std::map<job_no_t, sim_time_t>& running_jobs,
        sim_time_t current_time) override;

    /** @copydoc SchedulerBase::sync_to */
    void sync_to(sim_time_t current_time) override;

    /**
     * @brief Write block-queue search statistics to a stream.
     * @param[in,out] os Destination stream receiving the statistics report.
     */
    void print_block_stats(std::ostream& os) const {
        auto stats = m_wait_queue.get_stats();
        os << "Block Queue Statistics:" << std::endl;
        os << "  Blocks skipped (empty): " << stats.blocks_skipped_empty << std::endl;
        os << "  Blocks skipped (time): " << stats.blocks_skipped_time << std::endl;
        os << "  Blocks skipped (resource): " << stats.blocks_skipped_resource << std::endl;
        os << "  Blocks checked (scanned): " << stats.blocks_checked << std::endl;
        os << "  Jobs scanned: " << stats.jobs_scanned << std::endl;

        size_t total_lookups = stats.blocks_skipped_empty + stats.blocks_skipped_time +
                               stats.blocks_skipped_resource + stats.blocks_checked;
        if (total_lookups > 0) {
            os << "  Skip rate: " << (100.0 * (stats.blocks_skipped_empty + stats.blocks_skipped_time +
                                               stats.blocks_skipped_resource) / total_lookups)
               << "%" << std::endl;
        }
    }

    /** @copydoc SchedulerBase::active_job_count */
    size_t active_job_count() override {
        return m_eligible_end_idx - m_removed_count;
    }

    /** @copydoc SchedulerBase::get_next_arrival_time */
    sim_time_t get_next_arrival_time() override;

    /** @copydoc SchedulerBase::has_eligible_jobs */
    bool has_eligible_jobs() override {
        return active_job_count() > 0;
    }

protected:
    /** @copydoc SchedulerBase::wait_queue_size */
    size_t wait_queue_size() const override {
        return m_wait_queue.size();
    }
};

// Explicit template instantiations for common block sizes
extern template class BlockQueueFCFSScheduler<32>;
extern template class BlockQueueFCFSScheduler<64>;
extern template class BlockQueueFCFSScheduler<128>;
extern template class BlockQueueFCFSScheduler<256>;

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_BLOCK_FCFS_HPP
