/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULER_FCFS_UNIFIED_CIRCULAR_HPP
#define DR_EVT_SIM_SCHEDULER_FCFS_UNIFIED_CIRCULAR_HPP

#include "sim/scheduler_fcfs_hybrid_circular.hpp"
#include <algorithm>

namespace dr_evt {

/** FCFS and backfill over one bounded candidate prefix per unified wake.
 * The driver invokes schedule() once after processing all same-time events.
 * EASY uses the shared reservation implementation with one protected job.
 * Every candidate in the prefix counts, including immediate FCFS starts and
 * jobs whose projected starts fall outside the planning horizon.
 */
class FCFSUnifiedCircularScheduler : public FCFSHybridCircularScheduler {
  size_t m_dispatch_queue_depth;

public:
  FCFSUnifiedCircularScheduler(
      num_nodes_t total_nodes, size_t initial_job_count, BackfillPolicy policy,
      size_t initial_capacity, CircularOverflowPolicy overflow_policy,
      size_t reservation_depth, size_t dispatch_queue_depth)
      : FCFSHybridCircularScheduler(
            total_nodes, initial_job_count,
            policy == BackfillPolicy::EASY ? BackfillPolicy::HYBRID : policy,
            initial_capacity, overflow_policy,
            policy == BackfillPolicy::EASY ? 1 : reservation_depth, 0),
        m_dispatch_queue_depth(dispatch_queue_depth) {}

  std::vector<job_no_t> schedule(num_nodes_t free_nodes,
                                 const running_jobs_t &running_jobs,
                                 sim_time_t current_time) override {
    sync_to(current_time);
    if (m_dispatch_queue_depth == 0 ||
        active_job_count() <= m_dispatch_queue_depth) {
      return FCFSHybridCircularScheduler::schedule(free_nodes, running_jobs,
                                                   current_time);
    }
    // Preserve the existing amortized compaction threshold. Compact before
    // bounding eligibility so the base pass cannot remove a withheld entry.
    if (m_removed_count > 0 && m_removed_count * 2 > m_wait_queue.size()) {
      m_eligible_end_idx -= m_removed_count;
      const auto end = std::remove_if(
          m_wait_queue.begin(), m_wait_queue.end(),
          [](const JobEntry &entry) { return entry.removed; });
      m_wait_queue.erase(end, m_wait_queue.end());
      m_removed_count = 0;
    }
    size_t prefix_end = std::min(m_eligible_end_idx, m_dispatch_queue_depth);
    size_t prefix_removed = 0;
    // With no removed entries, queue indices already count live candidates.
    // Only holes require walking the prefix to enforce the candidate limit.
    if (m_removed_count != 0) {
      prefix_end = 0;
      size_t candidates = 0;
      while (prefix_end < m_eligible_end_idx &&
             candidates < m_dispatch_queue_depth) {
        if (m_wait_queue[prefix_end].removed) {
          ++prefix_removed;
        } else {
          ++candidates;
        }
        ++prefix_end;
      }
    }
    const size_t withheld = m_eligible_end_idx - prefix_end;
    const size_t withheld_removed = m_removed_count - prefix_removed;
    m_eligible_end_idx = prefix_end;
    m_removed_count = prefix_removed;
    try {
      auto result = FCFSHybridCircularScheduler::schedule(
          free_nodes, running_jobs, current_time);
      m_eligible_end_idx += withheld;
      m_removed_count += withheld_removed;
      return result;
    } catch (...) {
      m_eligible_end_idx += withheld;
      m_removed_count += withheld_removed;
      throw;
    }
  }

protected:
  bool horizon_skipped_job_uses_depth_slot() const override { return true; }
};

} // namespace dr_evt
#endif
