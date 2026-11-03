/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_circular_fcfs_conservative.cpp
 * @brief Circular-buffer conservative FCFS scheduling implementation.
 */

#include "sim/scheduler_circular_fcfs_conservative.hpp"
#include <algorithm>

namespace dr_evt {

void CircularBufferFCFSConservativeScheduler::insert_job(
    job_no_t job_id, sim_time_t submit_time, tdiff_t run_time_estimate,
    num_nodes_t nodes_requested) {
  if (m_wait_queue.full()) {
    if (m_overflow_policy == CircularOverflowPolicy::ABORT) {
      throw std::runtime_error(
          "CircularBufferFCFSConservativeScheduler: wait queue capacity (" +
          std::to_string(m_wait_queue.capacity()) +
          ") exceeded; use --wait_queue_overflow grow or a larger "
          "--wait_queue_capacity");
    }
    m_wait_queue.set_capacity(std::max<size_t>(m_wait_queue.capacity() * 2, 1));
  }
  m_wait_queue.push_back(
      JobEntry(job_id, submit_time, run_time_estimate, nodes_requested));
  if (submit_time <= m_current_tracked_time) {
    m_eligible_end_idx = m_wait_queue.size();
  }
}

void CircularBufferFCFSConservativeScheduler::sync_to(sim_time_t current_time) {
  if (current_time <= m_current_tracked_time) {
    return;
  }
  while (m_eligible_end_idx < m_wait_queue.size() &&
         m_wait_queue[m_eligible_end_idx].submit_time <= current_time) {
    ++m_eligible_end_idx;
  }
  m_current_tracked_time = current_time;
}

sim_time_t CircularBufferFCFSConservativeScheduler::get_next_arrival_time() {
  for (size_t i = m_eligible_end_idx; i < m_wait_queue.size(); ++i) {
    if (!m_wait_queue[i].removed) {
      return m_wait_queue[i].submit_time;
    }
  }
  return std::numeric_limits<sim_time_t>::max();
}

std::vector<job_no_t>
CircularBufferFCFSConservativeScheduler::pending_job_ids() const {
  std::vector<job_no_t> result;
  for (const auto &job : m_wait_queue) {
    if (!job.removed) {
      result.push_back(job.job_id);
    }
  }
  return result;
}

sim_time_t
CircularBufferFCFSConservativeScheduler::calculate_conservative_window(
    size_t job_index, num_nodes_t available_nodes,
    const running_jobs_t &running_jobs, sim_time_t current_time) {
  sim_time_t earliest_conflict = std::numeric_limits<sim_time_t>::max();
  const size_t depth =
      m_backfill_policy == BackfillPolicy::HYBRID ? m_reservation_depth : 0;
  size_t reserved = 0;
  running_jobs_t planned_jobs = running_jobs;
  for (size_t i = 0; i < job_index; ++i) {
    const auto &job = m_wait_queue[i];
    if (job.removed) {
      continue;
    }
    if (depth != 0 && reserved >= depth) {
      break;
    }
    const sim_time_t job_reservation = calculate_fcfs_reservation(
        job.nodes_requested, available_nodes, planned_jobs, current_time);
    if (!reservation_within_horizon(job_reservation, current_time)) {
      continue;
    }
    ++reserved;
    planned_jobs[job.job_id] = {
        job_reservation,
        release_estimate(job_reservation, job.run_time_estimate),
        job.nodes_requested};
    earliest_conflict = std::min(earliest_conflict, job_reservation);
  }
  return earliest_conflict;
}

std::vector<job_no_t> CircularBufferFCFSConservativeScheduler::schedule(
    num_nodes_t free_nodes, const running_jobs_t &running_jobs,
    sim_time_t current_time) {
  sync_to(current_time);
  if (m_removed_count > 0 && m_removed_count * 2 > m_wait_queue.size()) {
    size_t eligible_removed = 0;
    for (size_t i = 0; i < m_eligible_end_idx && i < m_wait_queue.size(); ++i) {
      if (m_wait_queue[i].removed) {
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
  if (m_eligible_end_idx == 0) {
    return {};
  }

  std::vector<job_no_t> jobs_to_run;
  num_nodes_t available_nodes = free_nodes;
  running_jobs_t effective_running_jobs = running_jobs;
  while (m_eligible_end_idx > 0 && !m_wait_queue.empty() &&
         (m_wait_queue.front().removed ||
          m_wait_queue.front().nodes_requested <= available_nodes)) {
    if (!m_wait_queue.front().removed) {
      const auto &job = m_wait_queue.front();
      jobs_to_run.push_back(job.job_id);
      available_nodes -= job.nodes_requested;
      effective_running_jobs[job.job_id] = {
          current_time, release_estimate(current_time, job.run_time_estimate),
          job.nodes_requested};
    } else {
      --m_removed_count;
    }
    m_wait_queue.pop_front();
    --m_eligible_end_idx;
  }
  if (active_job_count() == 0 || m_backfill_policy == BackfillPolicy::NONE) {
    return jobs_to_run;
  }

  // The blocked head's projection is kept for every policy so that
  // get_fcfs_head_shadow_time() reports it whether or not it is planned.
  m_fcfs_reservation_time = calculate_fcfs_reservation(
      m_wait_queue.front().nodes_requested, available_nodes,
      effective_running_jobs, current_time);

  if (m_backfill_policy == BackfillPolicy::EASY) {
    const bool head_reservation_within_horizon = reservation_within_horizon(
        m_fcfs_reservation_time, current_time);
    if (available_nodes == 0) {
      return jobs_to_run;
    }
    for (size_t i = 1; i < m_eligible_end_idx; ++i) {
      const auto &job = m_wait_queue[i];
      if (job.removed || job.nodes_requested > available_nodes) {
        continue;
      }
      if (!head_reservation_within_horizon ||
          current_time + release_estimate(current_time, job.run_time_estimate) <
              m_fcfs_reservation_time) {
        jobs_to_run.push_back(job.job_id);
        available_nodes -= job.nodes_requested;
        m_wait_queue[i].removed = true;
        ++m_removed_count;
        if (available_nodes == 0) {
          break;
        }
      }
    }
    return jobs_to_run;
  }

  if (available_nodes == 0) {
    return jobs_to_run;
  }

  // Out-of-horizon jobs stay queued but do not consume a scan slot. They are
  // projected again on the next pass instead of requiring blocked-list state.
  size_t examined = reservation_within_horizon(m_fcfs_reservation_time,
                                                current_time)
                        ? 1
                        : 0;
  for (size_t i = 1; i < m_eligible_end_idx; ++i) {
    const auto &job = m_wait_queue[i];
    if (job.removed) {
      continue;
    }
    const sim_time_t job_reservation = calculate_fcfs_reservation(
        job.nodes_requested, available_nodes, effective_running_jobs,
        current_time);
    if (!reservation_within_horizon(job_reservation, current_time)) {
      continue;
    }
    if (m_backfill_queue_depth != 0 && examined >= m_backfill_queue_depth) {
      break;
    }
    ++examined;
    if (job.nodes_requested > available_nodes) {
      continue;
    }
    const sim_time_t conservative_window = calculate_conservative_window(
        i, available_nodes, effective_running_jobs, current_time);
    if (current_time + release_estimate(current_time, job.run_time_estimate) <
        conservative_window) {
      jobs_to_run.push_back(job.job_id);
      available_nodes -= job.nodes_requested;
      m_wait_queue[i].removed = true;
      ++m_removed_count;
      effective_running_jobs[job.job_id] = {
          current_time, release_estimate(current_time, job.run_time_estimate),
          job.nodes_requested};
      if (available_nodes == 0) {
        break;
      }
    }
  }
  return jobs_to_run;
}

} // namespace dr_evt
