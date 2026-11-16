/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_hybrid.cpp
 * @brief FCFS starts and hybrid/conservative backfill over a circular or
 * deque queue.
 * @details Hybrid protects up to R waiting jobs; conservative protects every
 * examined waiting job. Later jobs may start only when they preserve those
 * capacity reservations. Default examination accounting is shared across
 * calls at the same timestamp.
 * @note CLI: --priority_policy fcfs --backfill_policy hybrid
 * --reservation_depth R --queue_impl circular or deque, with R > 0. For
 * conservative use --priority_policy fcfs_conservative --backfill_policy
 * conservative.
 * Use --dispatch_interval 0 and --backfill_interval 0; omit
 * --per_pass_accounting.
 * Protobuf: Simulation_Params and InitRequest use the same field names
 * without the leading --; see dr_evt_params.proto and dr_evt_service.proto.
 */

#include "sim/scheduler_fcfs_hybrid.hpp"
#include <algorithm>

namespace dr_evt {

template <typename Queue>
void FCFSHybridScheduler<Queue>::insert_job(job_no_t job_id,
                                            sim_time_t submit_time,
                                            tdiff_t run_time_estimate,
                                            num_nodes_t nodes_requested) {
  Queue::push_back(
      m_wait_queue,
      JobEntry(job_id, submit_time, run_time_estimate, nodes_requested),
      m_overflow_policy,
      std::string("FCFSHybrid") + Queue::label + "Scheduler");
  if (submit_time <= m_current_tracked_time) {
    m_eligible_end_idx = m_wait_queue.size();
  }
}

template <typename Queue>
void FCFSHybridScheduler<Queue>::sync_to(sim_time_t current_time) {
  if (current_time <= m_current_tracked_time) {
    return;
  }
  while (m_eligible_end_idx < m_wait_queue.size() &&
         m_wait_queue[m_eligible_end_idx].submit_time <= current_time) {
    ++m_eligible_end_idx;
  }
  m_current_tracked_time = current_time;
}

template <typename Queue>
sim_time_t FCFSHybridScheduler<Queue>::get_next_arrival_time() {
  for (size_t i = m_eligible_end_idx; i < m_wait_queue.size(); ++i) {
    if (!m_wait_queue[i].removed) {
      return m_wait_queue[i].submit_time;
    }
  }
  return std::numeric_limits<sim_time_t>::max();
}

template <typename Queue>
std::vector<job_no_t> FCFSHybridScheduler<Queue>::pending_job_ids() const {
  std::vector<job_no_t> result;
  for (const auto &job : m_wait_queue) {
    if (!job.removed) {
      result.push_back(job.job_id);
    }
  }
  return result;
}

template <typename Queue>
sim_time_t FCFSHybridScheduler<Queue>::calculate_hybrid_window(
    size_t job_index, num_nodes_t available_nodes,
    const running_jobs_t &running_jobs, sim_time_t current_time,
    const projected_starts_t &projected_starts) {
  sim_time_t earliest_conflict = std::numeric_limits<sim_time_t>::max();
  const size_t depth =
      m_backfill_policy == BackfillPolicy::HYBRID ? m_reservation_depth : 0;
  size_t reserved = 0;
  auto plan = reservation_timeline(available_nodes, running_jobs, current_time,
                                   projected_starts);
  for (size_t i = 0; i < job_index; ++i) {
    const auto &job = m_wait_queue[i];
    if (job.removed) {
      continue;
    }
    if (depth != 0 && reserved >= depth) {
      break;
    }
    const tdiff_t duration =
        release_estimate(current_time, job.run_time_estimate);
    const auto reservation =
        plan.earliest_fit(job.nodes_requested, duration, current_time);
    if (!reservation) {
      continue;
    }
    const sim_time_t job_reservation = *reservation;
    if (!reservation_within_horizon(job_reservation, current_time)) {
      continue;
    }
    ++reserved;
    plan.reserve(job_reservation, job.nodes_requested,
                 release_estimate(job_reservation, job.run_time_estimate));
    earliest_conflict = std::min(earliest_conflict, job_reservation);
  }
  return earliest_conflict;
}

template <typename Queue>
std::vector<job_no_t>
FCFSHybridScheduler<Queue>::schedule(num_nodes_t free_nodes,
                                     const running_jobs_t &running_jobs,
                                     sim_time_t current_time) {
  return schedule_with_protection<Reservation_Timeline>(
      free_nodes, running_jobs, current_time);
}

template class FCFSHybridScheduler<CircularWaitQueue>;
template class FCFSHybridScheduler<DequeWaitQueue>;

} // namespace dr_evt
