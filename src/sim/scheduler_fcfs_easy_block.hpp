/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULER_FCFS_EASY_BLOCK_HPP
#define DR_EVT_SIM_SCHEDULER_FCFS_EASY_BLOCK_HPP

#include "sim/block_wait_queue.hpp"
#include "sim/scheduler_base.hpp"
#include <deque>

namespace dr_evt {

/** \addtogroup dr_evt_sim
 *  @{ */

/**
 * @brief Event-driven FCFS dispatch and EASY backfill using a block queue.
 * @details
 * Uses BlockWaitQueue for O(1) insert/remove and O(blocks) backfill search.
 * Maintains separate eligibility tracking (identical to
 * FCFSEasyDequeScheduler).
 *
 * Key optimization over FCFSEasyDequeScheduler: BlockWaitQueue's
 * find_backfill_candidate() uses block-level metadata pre-filtering for faster
 * backfill search.
 * @note Selected by --priority_policy fcfs --queue_impl block
 * --backfill_policy easy, with --dispatch_interval 0 and --backfill_interval 0.
 * --block_size selects 4, 8, 16, 32, 64, 128, or 256 entries per block.
 * --priority_policy fcfs_conservative with EASY also selects this queue.
 * --backfill_policy none disables backfill.
 *
 * @tparam BlockSize Block size (must be power of 2)
 */
template <size_t BlockSize>
class FCFSEasyBlockScheduler : public SchedulerBase {
private:
  /// Block-indexed queue used for efficient candidate searches.
  BlockWaitQueue<BlockSize> m_wait_queue;

  // Eligibility tracking (identical to FCFSEasyDequeScheduler)
  struct JobArrival {
    /// Stable identifier of the Trace job represented by this arrival.
    job_no_t job_id;
    /// Arrival time used to determine eligibility.
    sim_time_t submit_time;
    /// True when this arrival has been scheduled and removed.
    bool removed;

    /** @brief Initialize an unscheduled arrival record.
     * @param[in] id Trace job identifier.
     * @param[in] submit Arrival time. */
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
  /// Jobs examined per backfill pass, head included; zero is unlimited.
  size_t m_backfill_queue_depth;

public:
  /** @brief Construct a block-indexed FCFS scheduler.
   * @param[in] total_nodes Cluster capacity available for allocations.
   * @param[in] bf_policy Rule governing jobs considered behind the FCFS head.
   * @param[in] backfill_queue_depth Jobs examined per backfill pass, head
   * included; zero is unlimited.
   */
  FCFSEasyBlockScheduler(num_nodes_t total_nodes, BackfillPolicy bf_policy,
                         size_t backfill_queue_depth = 0)
      : SchedulerBase(total_nodes, bf_policy), m_wait_queue(),
        m_eligible_end_idx(0), m_current_tracked_time(0.0), m_removed_count(0),
        m_backfill_queue_depth(backfill_queue_depth) {}

  /** @copydoc SchedulerBase::insert_job */
  void insert_job(job_no_t job_id, sim_time_t submit_time,
                  tdiff_t run_time_estimate,
                  num_nodes_t nodes_requested) override;

  /** @copydoc SchedulerBase::schedule */
  std::vector<job_no_t> schedule(num_nodes_t free_nodes,
                                 const running_jobs_t &running_jobs,
                                 sim_time_t current_time) override;

  /** @copydoc SchedulerBase::sync_to */
  void sync_to(sim_time_t current_time) override;

  /**
   * @brief Write block-queue search statistics to a stream.
   * @param[in,out] os Destination stream receiving the statistics report.
   */
  void print_block_stats(std::ostream &os) const {
    auto stats = m_wait_queue.get_stats();
    os << "Block Queue Statistics:" << std::endl;
    os << "  Blocks skipped (empty): " << stats.blocks_skipped_empty
       << std::endl;
    os << "  Blocks skipped (time): " << stats.blocks_skipped_time << std::endl;
    os << "  Blocks skipped (resource): " << stats.blocks_skipped_resource
       << std::endl;
    os << "  Blocks checked (scanned): " << stats.blocks_checked << std::endl;
    os << "  Jobs scanned: " << stats.jobs_scanned << std::endl;

    size_t total_lookups = stats.blocks_skipped_empty +
                           stats.blocks_skipped_time +
                           stats.blocks_skipped_resource + stats.blocks_checked;
    if (total_lookups > 0) {
      os << "  Skip rate: "
         << (100.0 *
             (stats.blocks_skipped_empty + stats.blocks_skipped_time +
              stats.blocks_skipped_resource) /
             total_lookups)
         << "%" << std::endl;
    }
  }

  /** @copydoc SchedulerBase::active_job_count */
  size_t active_job_count() const override {
    return m_eligible_end_idx - m_removed_count;
  }

  /** @copydoc SchedulerBase::get_next_arrival_time */
  sim_time_t get_next_arrival_time() override;

  /** @copydoc SchedulerBase::has_eligible_jobs */
  bool has_eligible_jobs() override { return active_job_count() > 0; }

  /** @copydoc SchedulerBase::pending_job_ids */
  std::vector<job_no_t> pending_job_ids() const override {
    std::vector<job_no_t> result;
    for (const auto &job : m_job_order) {
      if (!job.removed) {
        result.push_back(job.job_id);
      }
    }
    return result;
  }

  std::optional<tdiff_t> waiting_resource_area() const override {
    return m_wait_queue.waiting_resource_area(m_current_tracked_time);
  }

protected:
  /** @copydoc SchedulerBase::wait_queue_size */
  size_t wait_queue_size() const override { return m_wait_queue.size(); }

private:
  /**
   * @brief Backfill under --backfill_queue_depth by scanning arrival order.
   * @param[in] head_planned Whether the head's reservation is inside the
   * planning horizon and therefore consumes a depth slot.
   * @param[in] planned_reservation Head reservation start, or the maximum
   * time when the head is not planned.
   * @param[in] current_time Current simulation time.
   * @param[in] reservation_plan Protected resource-time plan, including the
   * head reservation when one is planned; extended for every accepted
   * backfill job.
   * @param[in] available_nodes Free nodes before any backfill start.
   * @param[in] jobs_to_run Jobs already started in this pass.
   * @return jobs_to_run extended with the backfill starts.
   */
  std::vector<job_no_t> backfill_with_queue_depth(
      bool head_planned, sim_time_t planned_reservation,
      sim_time_t current_time, Reservation_Timeline reservation_plan,
      num_nodes_t available_nodes, std::vector<job_no_t> jobs_to_run);
};

// Explicit template instantiations for common block sizes
extern template class FCFSEasyBlockScheduler<32>;
extern template class FCFSEasyBlockScheduler<64>;
extern template class FCFSEasyBlockScheduler<128>;
extern template class FCFSEasyBlockScheduler<256>;

/**@}*/

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_FCFS_EASY_BLOCK_HPP
