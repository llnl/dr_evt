/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_easy_circular.cpp
 * @brief Scheduling implementation for the circular-buffer FCFS queue.
 */

#include "sim/scheduler_fcfs_easy_circular.hpp"
#include <algorithm>

namespace dr_evt {

void FCFSEasyCircularScheduler::sync_to(sim_time_t current_time) {
  if (current_time <= m_current_tracked_time) {
    return;
  }

  while (m_eligible_end_idx < m_wait_queue.size() &&
         m_wait_queue[m_eligible_end_idx].submit_time <= current_time) {
    ++m_eligible_end_idx;
  }
  m_current_tracked_time = current_time;
}

void FCFSEasyCircularScheduler::mark_removed(job_no_t job_id) {
  for (auto &entry : m_wait_queue) {
    if (entry.job_id == job_id && !entry.removed) {
      entry.removed = true;
      ++m_removed_count;
      return;
    }
  }
}

std::vector<job_no_t>
FCFSEasyCircularScheduler::schedule(num_nodes_t free_nodes,
                                    const running_jobs_t &running_jobs,
                                    sim_time_t current_time) {
  sync_to(current_time);

  // Garbage collection: compact queue if >50% is garbage. Same
  // approach as FCFSEasyDequeScheduler, but no pointer-subtraction pitfall to
  // avoid here - operator[] is used throughout instead.
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

  if (m_eligible_end_idx == 0) {
    return {}; // No eligible jobs
  }

  std::vector<job_no_t> jobs_to_run;
  num_nodes_t available_nodes = free_nodes;
  running_jobs_t effective_running_jobs = running_jobs;

  // Step 1: consume the front of the eligible queue with pop_front()
  // (O(1) on a circular_buffer, same as on a deque) instead of
  // scanning by index. See FCFSEasyDequeScheduler::schedule() for the full
  // reasoning - identical here.
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

  if (active_job_count() == 0) {
    return jobs_to_run;
  }

  if (m_backfill_policy == BackfillPolicy::NONE) {
    return jobs_to_run;
  }

  // FCFS head is now genuinely at the front and blocked - calculate
  // reservation for backfilling. See FCFSEasyDequeScheduler::schedule() for
  // why effective_running_jobs must include jobs_to_run.
  m_fcfs_reservation_time = calculate_fcfs_reservation(
      m_wait_queue.front().nodes_requested, available_nodes,
      effective_running_jobs, current_time);
  // A head whose reservation starts beyond the planning horizon is not
  // planned: candidates are not tested against it (Slurm bf_window).
  const bool head_within_horizon =
      reservation_within_horizon(m_fcfs_reservation_time, current_time);

  // Preserve the head-reservation snapshot, then avoid a scan that cannot
  // start a backfill job without free nodes.
  if (available_nodes == 0) {
    return jobs_to_run;
  }

  // Try backfilling with remaining eligible jobs. These can be at any
  // position (not just the front), so pop_front() doesn't apply here -
  // keep the lazy mark-and-defer-to-GC removal for this part. A queue depth
  // fixes the scan end up front, leaving the loop body unchanged.
  const size_t scan_end =
      queue_depth_scan_end(m_wait_queue, 1, m_eligible_end_idx,
                           m_backfill_queue_depth, head_within_horizon);
  for (size_t i = 1; i < scan_end; ++i) {
    const auto &job = m_wait_queue[i];
    if (job.removed)
      continue;

    if (job.nodes_requested > available_nodes)
      continue;

    // IMPORTANT: strict < (not <=) - see FCFSEasyDequeScheduler::schedule()
    // for why. Backfill jobs must complete BEFORE the reservation
    // time, not at it.
    if (!head_within_horizon ||
        current_time + release_estimate(current_time, job.run_time_estimate) <
            m_fcfs_reservation_time) {
      jobs_to_run.push_back(job.job_id);
      available_nodes -= job.nodes_requested;
      m_wait_queue[i].removed = true;
      ++m_removed_count;

      if (available_nodes == 0)
        break;
    }
  }

  return jobs_to_run;
}

} // namespace dr_evt
