/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_periodic_easy.cpp
 * @brief FCFS starts after events and EASY backfill on separate timer ticks.
 * @details Arrivals and resource releases can start fitting queue-head jobs
 * immediately. Later jobs are considered for backfill only on configured
 * ticks, while preserving the waiting head reservation. This is separate
 * periodic backfill, not periodic dispatch of the entire queue.
 * @note CLI: --priority_policy fcfs --queue_impl circular or deque
 * --backfill_policy easy --backfill_interval I, with I > 0 and
 * --dispatch_interval 0. Omit
 * --per_pass_accounting. --backfill_queue_depth limits backfill
 * examinations.
 * Protobuf: Simulation_Params and InitRequest use the same field names
 * without the leading --; see dr_evt_params.proto and dr_evt_service.proto.
 */

#include "sim/scheduler_fcfs_periodic_easy.hpp"

#include <algorithm>

namespace dr_evt {

template <typename Queue>
std::vector<job_no_t>
FCFSPeriodicEasyScheduler<Queue>::schedule(num_nodes_t free_nodes,
                                           const running_jobs_t &running_jobs,
                                           sim_time_t current_time) {
  if (m_backfill_evaluation_time == current_time) {
    return schedule_event(free_nodes, running_jobs, current_time);
  }
  m_backfill_evaluation_time = current_time;
  return Base::schedule(free_nodes, running_jobs, current_time);
}

template <typename Queue>
std::vector<job_no_t> FCFSPeriodicEasyScheduler<Queue>::schedule_event(
    num_nodes_t free_nodes, const running_jobs_t &running_jobs,
    sim_time_t current_time) {
  this->sync_to(current_time);

  if (this->m_removed_count > 0 &&
      this->m_removed_count * 2 > this->m_wait_queue.size()) {
    size_t eligible_removed = 0;
    for (size_t index = 0;
         index < this->m_eligible_end_idx && index < this->m_wait_queue.size();
         ++index) {
      if (this->m_wait_queue[index].removed) {
        ++eligible_removed;
      }
    }
    const auto new_end =
        std::remove_if(this->m_wait_queue.begin(), this->m_wait_queue.end(),
                       [](const JobEntry &entry) { return entry.removed; });
    this->m_wait_queue.erase(new_end, this->m_wait_queue.end());
    this->m_eligible_end_idx -= eligible_removed;
    this->m_removed_count = 0;
  }

  std::vector<job_no_t> jobs_to_run;
  if (this->m_eligible_end_idx == 0) {
    return jobs_to_run;
  }

  num_nodes_t available_nodes = free_nodes;
  projected_starts_t projected_starts;
  while (this->m_eligible_end_idx > 0 && !this->m_wait_queue.empty() &&
         (this->m_wait_queue.front().removed ||
          this->m_wait_queue.front().nodes_requested <= available_nodes)) {
    if (!this->m_wait_queue.front().removed) {
      const auto &job = this->m_wait_queue.front();
      jobs_to_run.push_back(job.job_id);
      available_nodes -= job.nodes_requested;
      projected_starts.push_back(
          {current_time,
           this->release_estimate(current_time, job.run_time_estimate),
           job.nodes_requested});
    } else {
      --this->m_removed_count;
    }
    this->m_wait_queue.pop_front();
    --this->m_eligible_end_idx;
  }

  if (this->active_job_count() != 0) {
    this->m_fcfs_reservation_time = this->calculate_fcfs_reservation(
        this->m_wait_queue.front().nodes_requested, available_nodes,
        running_jobs, current_time, projected_starts);
  }
  return jobs_to_run;
}

template class FCFSPeriodicEasyScheduler<CircularWaitQueue>;
template class FCFSPeriodicEasyScheduler<DequeWaitQueue>;

} // namespace dr_evt
