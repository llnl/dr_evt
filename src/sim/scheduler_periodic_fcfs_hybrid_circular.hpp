/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_periodic_fcfs_hybrid_circular.hpp
 * @brief Periodic FCFS dispatch and hybrid backfill using a circular queue.
 */

#ifndef DR_EVT_SIM_SCHEDULER_PERIODIC_FCFS_HYBRID_CIRCULAR_HPP
#define DR_EVT_SIM_SCHEDULER_PERIODIC_FCFS_HYBRID_CIRCULAR_HPP

#include "sim/reservation_search_cache.hpp"
#include "sim/scheduler_fcfs_hybrid_circular.hpp"
#include <algorithm>
#include <array>
#include <functional>

namespace dr_evt {

/** @brief Periodic FCFS dispatch and hybrid backfill using a circular queue.
 * @details Selected by create_scheduler() only when dispatch_interval > 0.
 * Used for fixed periodic dispatch and, with dispatch_on_events enabled,
 * arrival-triggered unified passes. Ordinary event-driven scheduling and
 * separate periodic backfill use their existing scheduler implementations.
 * The driver invokes schedule() once after processing all same-time
 * events. EASY uses the shared reservation implementation with one protected
 * job. Every candidate in the prefix counts, including immediate FCFS starts
 * and jobs whose projected starts fall outside the planning horizon.
 * @note Selected by --dispatch_interval I (I > 0), --backfill_interval 0,
 * --queue_impl circular, and FCFS ordering. Supports --backfill_policy none,
 * easy, hybrid, or conservative. EASY maps to hybrid with depth 1; hybrid
 * requires --reservation_depth N (N > 0). --dispatch_queue_depth Q bounds the
 * full pass; --dispatch_on_events also requests passes on job arrivals.
 */
class PeriodicFCFSHybridCircularScheduler : public FCFSHybridCircularScheduler {
  /// Maximum live candidates per wake; zero leaves the prefix unlimited.
  size_t m_dispatch_queue_depth;

public:
  /**
   * @brief Construct a periodic FCFS dispatch and hybrid backfill scheduler
   * using a circular queue.
   * @param[in] total_nodes Cluster capacity available for allocations.
   * @param[in] initial_job_count Initially known jobs, used when capacity is
   * zero.
   * @param[in] policy Backfill policy; EASY protects one waiting job.
   * @param[in] initial_capacity Initial circular queue capacity.
   * @param[in] overflow_policy Action when the circular queue is full.
   * @param[in] reservation_depth Waiting jobs protected by hybrid backfill.
   * @param[in] dispatch_queue_depth Live candidates examined per wake; zero
   * leaves the prefix unlimited.
   */
  PeriodicFCFSHybridCircularScheduler(
      num_nodes_t total_nodes, size_t initial_job_count, BackfillPolicy policy,
      size_t initial_capacity, CircularOverflowPolicy overflow_policy,
      size_t reservation_depth, size_t dispatch_queue_depth)
      : FCFSHybridCircularScheduler(
            total_nodes, initial_job_count,
            policy == BackfillPolicy::EASY ? BackfillPolicy::HYBRID : policy,
            initial_capacity, overflow_policy,
            policy == BackfillPolicy::EASY ? 1 : reservation_depth, 0),
        m_dispatch_queue_depth(dispatch_queue_depth) {}

  /**
   * @brief Schedule the bounded eligible prefix for one unified wake.
   * @details Withheld queue eligibility is restored after the pass, including
   * when the planner throws. Immediate starts consume candidate slots.
   * @param[in] free_nodes Nodes available for immediate starts.
   * @param[in] running_jobs Active jobs used for release projection.
   * @param[in] current_time Scheduling timestamp.
   * @return Identifiers of jobs selected for immediate starts in queue order.
   */
  std::vector<job_no_t> schedule(num_nodes_t free_nodes,
                                 const running_jobs_t &running_jobs,
                                 sim_time_t current_time) override {
    sync_to(current_time);
    if (m_dispatch_queue_depth == 0 ||
        active_job_count() <= m_dispatch_queue_depth) {
      return schedule_unified_pass(free_nodes, running_jobs, current_time);
    }
    // Preserve the existing amortized compaction threshold. Compact before
    // bounding eligibility so the base pass cannot remove a withheld entry.
    if (m_removed_count > 0 && m_removed_count * 2 > m_wait_queue.size()) {
      m_eligible_end_idx -= m_removed_count;
      const auto end =
          std::remove_if(m_wait_queue.begin(), m_wait_queue.end(),
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
      auto result =
          schedule_unified_pass(free_nodes, running_jobs, current_time);
      m_eligible_end_idx += withheld;
      m_removed_count += withheld_removed;
      return result;
    } catch (...) {
      m_eligible_end_idx += withheld;
      m_removed_count += withheld_removed;
      throw;
    }
  }

private:
#ifndef DR_EVT_LEGACY_RESERVATION_BARRIER
  /**
   * @brief Run the cached planner without expanding the fallback's stack frame.
   * @param[in] free_nodes Nodes available for immediate starts.
   * @param[in] running_jobs Active jobs used for release projection.
   * @param[in] current_time Scheduling timestamp.
   * @return Identifiers of jobs selected for immediate starts.
   */
  [[gnu::noinline]] std::vector<job_no_t>
  schedule_cached_pass(num_nodes_t free_nodes,
                       const running_jobs_t &running_jobs,
                       sim_time_t current_time) {
    return schedule_with_protection<Reservation_Search_Cache>(
        free_nodes, running_jobs, current_time);
  }

  /**
   * @brief Check whether a bounded sample repeats enough interval shapes.
   * @return true when the first 128 live eligible jobs have at most 112
   * distinct node-count/duration pairs; queue order and eligibility are intact.
   */
  bool prefix_reuses_interval_shapes() const {
    // A half-full stack hash table avoids sorting or allocating for mostly
    // distinct requests. It never changes the eligible prefix or job order.
    std::array<const JobEntry *, 256> shapes{};
    size_t count = 0;
    size_t distinct = 0;
    for (size_t i = 0; i < m_eligible_end_idx && count < 128; ++i) {
      const auto &job = m_wait_queue[i];
      if (job.removed) {
        continue;
      }
      ++count;
      size_t slot =
          (std::hash<tdiff_t>{}(job.run_time_estimate) ^
           (static_cast<size_t>(job.nodes_requested) * size_t{0x9e3779b9u})) &
          (shapes.size() - 1);
      while (shapes[slot] &&
             (shapes[slot]->nodes_requested != job.nodes_requested ||
              shapes[slot]->run_time_estimate != job.run_time_estimate)) {
        slot = (slot + 1) & (shapes.size() - 1);
      }
      if (!shapes[slot]) {
        shapes[slot] = &job;
        if (++distinct > 112) {
          return false;
        }
      }
    }
    return true;
  }
#endif

  /**
   * @brief Select a protection planner for this unified dispatch pass.
   * @param[in] free_nodes Nodes available for immediate starts.
   * @param[in] running_jobs Active jobs used for release projection.
   * @param[in] current_time Scheduling timestamp.
   * @return Identifiers of jobs selected for immediate starts.
   */
  std::vector<job_no_t>
  schedule_unified_pass(num_nodes_t free_nodes,
                        const running_jobs_t &running_jobs,
                        sim_time_t current_time) {
#ifndef DR_EVT_LEGACY_RESERVATION_BARRIER
    // Small scans cannot amortize caching. Exact conservative plans retain
    // bounds across commits and repeat interval shapes throughout the prefix.
    if (m_backfill_policy == BackfillPolicy::CONSERVATIVE && free_nodes != 0 &&
        m_planning_time_resolution == 0.0 && active_job_count() >= 128 &&
        prefix_reuses_interval_shapes()) {
      return schedule_cached_pass(free_nodes, running_jobs, current_time);
    }
#endif
    return FCFSHybridCircularScheduler::schedule(free_nodes, running_jobs,
                                                 current_time);
  }

protected:
  /**
   * @brief Count horizon-skipped jobs in the unified candidate prefix.
   * @return true; a horizon skip still consumes a candidate slot.
   */
  bool horizon_skipped_job_uses_depth_slot() const override { return true; }
};

} // namespace dr_evt
#endif
