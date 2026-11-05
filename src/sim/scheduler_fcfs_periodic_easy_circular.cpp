/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include "sim/scheduler_fcfs_periodic_easy_circular.hpp"

#include <algorithm>

namespace dr_evt {

std::vector<job_no_t>
FCFSPeriodicEasyCircularScheduler::schedule(num_nodes_t free_nodes,
                                            const running_jobs_t &running_jobs,
                                            sim_time_t current_time) {
  if (m_backfill_evaluation_time == current_time) {
    return schedule_event(free_nodes, running_jobs, current_time);
  }
  m_backfill_evaluation_time = current_time;
  return FCFSEasyCircularScheduler::schedule(free_nodes, running_jobs,
                                             current_time);
}

std::vector<job_no_t> FCFSPeriodicEasyCircularScheduler::schedule_event(
    num_nodes_t free_nodes, const running_jobs_t &running_jobs,
    sim_time_t current_time) {
  sync_to(current_time);

  if (m_removed_count > 0 && m_removed_count * 2 > m_wait_queue.size()) {
    size_t eligible_removed = 0;
    for (size_t index = 0;
         index < m_eligible_end_idx && index < m_wait_queue.size(); ++index) {
      if (m_wait_queue[index].removed) {
        ++eligible_removed;
      }
    }
    const auto new_end =
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

} // namespace dr_evt
