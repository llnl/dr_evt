/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include "sim/scheduler_fcfs_periodic_hybrid_circular.hpp"
#include <algorithm>

namespace dr_evt {

namespace {
/**
 * @brief Timeline fit policy for Slurm-style periodic hybrid backfill.
 * @details Both immediate admission and future placement require capacity at
 * the requested end time. Inherited reserve() still uses [start, end).
 */
class EndpointInclusiveTimeline : public Reservation_Timeline {
public:
  /**
   * @brief Take ownership of the pass's free-node plan.
   * @param[in] plan Baseline capacity timeline to extend during this pass.
   */
  explicit EndpointInclusiveTimeline(Reservation_Timeline plan)
      : Reservation_Timeline(std::move(plan)) {}

  /**
   * @brief Find the earliest fit, including capacity at the requested end.
   * @param[in] nodes Required node count.
   * @param[in] duration Requested duration; nonpositive values query capacity.
   * @param[in] start Earliest permitted start time.
   * @return Earliest fitting start, or std::nullopt when no fit exists.
   */
  std::optional<sim_time_t> earliest_fit(num_nodes_t nodes, tdiff_t duration,
                                         sim_time_t start) const {
    return Reservation_Timeline::earliest_fit<true>(nodes, duration, start);
  }
};
} // namespace

template <bool ProtectReservations>
std::vector<job_no_t>
FCFSPeriodicHybridCircularSchedulerImpl<ProtectReservations>::schedule(
    num_nodes_t free_nodes, const running_jobs_t &running_jobs,
    sim_time_t current_time) {
  if constexpr (ProtectReservations) {
    // Slurm includes the endpoint in both admission and reservation placement.
    return schedule_with_protection<EndpointInclusiveTimeline>(
        free_nodes, running_jobs, current_time);
  } else {
    // Private dummy nodes do not constrain this diagnostic capacity pool.
    return schedule_event(free_nodes, running_jobs, current_time);
  }
}

template <bool ProtectReservations>
std::vector<job_no_t>
FCFSPeriodicHybridCircularSchedulerImpl<ProtectReservations>::schedule_event(
    num_nodes_t free_nodes, const running_jobs_t &running_jobs,
    sim_time_t current_time) {
  sync_to(current_time);

  if (m_removed_count > 0 && m_removed_count * 2 > m_wait_queue.size()) {
    size_t eligible_removed = 0;
    for (size_t idx = 0; idx < m_eligible_end_idx && idx < m_wait_queue.size();
         ++idx) {
      if (m_wait_queue[idx].removed) {
        ++eligible_removed;
      }
    }
    auto new_end =
        std::remove_if(m_wait_queue.begin(), m_wait_queue.end(),
                       [](const JobEntry &entry) { return entry.removed; });
    m_wait_queue.erase(new_end, m_wait_queue.end());
    m_eligible_end_idx -= eligible_removed;
    m_removed_count = 0;
  }

  std::vector<job_no_t> jobs_to_run;
  if (m_eligible_end_idx == 0) {
    return jobs_to_run;
  }

  num_nodes_t available_nodes = free_nodes;
  if constexpr (!ProtectReservations) {
    // Slurm with bf_licenses off continues past jobs lacking current capacity.
    for (size_t idx = 0; idx < m_eligible_end_idx; ++idx) {
      auto &job = m_wait_queue[idx];
      if (job.removed || job.nodes_requested > available_nodes) {
        continue;
      }
      jobs_to_run.push_back(job.job_id);
      available_nodes -= job.nodes_requested;
      job.removed = true;
      ++m_removed_count;
    }
    while (m_eligible_end_idx > 0 && m_wait_queue.front().removed) {
      m_wait_queue.pop_front();
      --m_eligible_end_idx;
      --m_removed_count;
    }
    m_fcfs_reservation_time = std::numeric_limits<sim_time_t>::max();
    return jobs_to_run;
  }
  projected_starts_t projected_starts;
  while (m_eligible_end_idx > 0 && !m_wait_queue.empty() &&
         (m_wait_queue.front().removed ||
          m_wait_queue.front().nodes_requested <= available_nodes)) {
    if (!m_wait_queue.front().removed) {
      const auto &job = m_wait_queue.front();
      jobs_to_run.push_back(job.job_id);
      available_nodes -= job.nodes_requested;
      projected_starts.push_back(
          {current_time, release_estimate(current_time, job.run_time_estimate),
           job.nodes_requested});
    } else {
      --m_removed_count;
    }
    m_wait_queue.pop_front();
    --m_eligible_end_idx;
  }

  if (active_job_count() != 0) {
    m_fcfs_reservation_time = calculate_fcfs_reservation(
        m_wait_queue.front().nodes_requested, available_nodes, running_jobs,
        current_time, projected_starts);
  }
  return jobs_to_run;
}

template class FCFSPeriodicHybridCircularSchedulerImpl<true>;
template class FCFSPeriodicHybridCircularSchedulerImpl<false>;

} // namespace dr_evt
