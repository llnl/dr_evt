/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_conservative.cpp
 * @brief Conservative-reservation FCFS scheduling implementation.
 */

#include "sim/scheduler_fcfs_conservative.hpp"
#include <algorithm>

namespace dr_evt {

void FCFSConservativeScheduler::sync_to(sim_time_t current_time) {
  if (current_time <= m_current_tracked_time) {
    return;
  }

  // Advance index to mark newly eligible jobs
  while (m_eligible_end_idx < m_wait_queue.size() &&
         m_wait_queue[m_eligible_end_idx].submit_time <= current_time) {
    ++m_eligible_end_idx;
  }
  m_current_tracked_time = current_time;
}

void FCFSConservativeScheduler::mark_removed(job_no_t job_id) {
  for (auto &entry : m_wait_queue) {
    if (entry.job_id == job_id && !entry.removed) {
      entry.removed = true;
      ++m_removed_count;
      return;
    }
  }
}

sim_time_t FCFSConservativeScheduler::calculate_conservative_window(
    size_t backfill_index, num_nodes_t available_nodes,
    const running_jobs_t &running_jobs, sim_time_t current_time) {
  // For conservative backfilling, we need to ensure the backfill job
  // doesn't delay ANY waiting job that came before it.
  // Calculate reservation time for each job ahead of this one.

  sim_time_t earliest_conflict = std::numeric_limits<sim_time_t>::max();

  // Check the reserved jobs ahead of this backfill candidate: every waiting
  // job ahead of it under CONSERVATIVE, or only the first
  // m_reservation_depth of them under HYBRID.
  const size_t depth =
      m_backfill_policy == BackfillPolicy::HYBRID ? m_reservation_depth : 0;
  size_t reserved = 0;
  for (size_t i = 0; i < backfill_index; ++i) {
    const auto &job = m_wait_queue[i];
    if (job.removed)
      continue;
    if (depth != 0 && reserved >= depth)
      break;

    // Calculate when this job could start
    sim_time_t job_reservation = calculate_fcfs_reservation(
        job.nodes_requested, available_nodes, running_jobs, current_time);
    // Beyond the planning horizon the job is not planned: no reservation
    // and no reservation-depth slot (Slurm bf_window).
    if (!reservation_within_horizon(job_reservation, current_time))
      continue;
    ++reserved;

    // Track the earliest reservation time
    earliest_conflict = std::min(earliest_conflict, job_reservation);
  }

  return earliest_conflict;
}

std::vector<job_no_t>
FCFSConservativeScheduler::schedule(num_nodes_t free_nodes,
                                    const running_jobs_t &running_jobs,
                                    sim_time_t current_time) {
  // Sync eligibility tracking
  sync_to(current_time);

  // Garbage collection: compact queue if >50% is garbage
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

  // Step 1: Start jobs from the front of the queue (FCFS order)
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

  // Step 2: Check if backfilling is disabled
  if (m_backfill_policy == BackfillPolicy::NONE) {
    // No backfilling - strict FCFS order only
    return jobs_to_run;
  }

  // Step 3: Try backfilling based on policy
  // Update effective running jobs to include jobs just started
  // The blocked head's projection is kept for every policy so that
  // get_fcfs_head_shadow_time() reports it whether or not it is planned.
  m_fcfs_reservation_time = calculate_fcfs_reservation(
      m_wait_queue.front().nodes_requested, available_nodes,
      effective_running_jobs, current_time);
  if (m_backfill_policy == BackfillPolicy::EASY) {
    // EASY backfilling: Only consider the head job's reservation
    const bool head_within_horizon =
        reservation_within_horizon(m_fcfs_reservation_time, current_time);

    // The reservation remains part of the public FCFS snapshot. No candidate
    // can start when FCFS has consumed all free nodes.
    if (available_nodes == 0) {
      return jobs_to_run;
    }

    // Try backfilling with remaining eligible jobs, up to the queue depth.
    const size_t scan_end =
        queue_depth_scan_end(m_wait_queue, 1, m_eligible_end_idx,
                             m_backfill_queue_depth, head_within_horizon);
    for (size_t i = 1; i < scan_end; ++i) {
      const auto &job = m_wait_queue[i];
      if (job.removed)
        continue;

      // Check resource fit
      if (job.nodes_requested > available_nodes)
        continue;

      // Check if job completes before head's reservation; an unplanned head
      // (beyond the horizon) does not constrain candidates.
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
  } else if (m_backfill_policy == BackfillPolicy::CONSERVATIVE ||
             m_backfill_policy == BackfillPolicy::HYBRID) {
    if (available_nodes == 0) {
      return jobs_to_run;
    }

    // CONSERVATIVE backfilling: Must not delay ANY waiting job.
    // HYBRID: must not delay the first m_reservation_depth waiting jobs.

    // Jobs with no reservation in the planning horizon stay in queue but do
    // not consume a scan slot. Their projection is recomputed on every pass,
    // which captures ordinary resource releases and capacity changes without
    // modeling an external blocked-list notification.
    size_t examined = reservation_within_horizon(m_fcfs_reservation_time,
                                                  current_time)
                          ? 1
                          : 0;
    for (size_t i = 1; i < m_eligible_end_idx; ++i) {
      const auto &job = m_wait_queue[i];
      if (job.removed)
        continue;
      const sim_time_t job_reservation = calculate_fcfs_reservation(
          job.nodes_requested, available_nodes, effective_running_jobs,
          current_time);
      if (!reservation_within_horizon(job_reservation, current_time))
        continue;
      if (m_backfill_queue_depth != 0 && examined >= m_backfill_queue_depth)
        break;
      ++examined;

      // Check resource fit
      if (job.nodes_requested > available_nodes)
        continue;

      // Calculate conservative window: earliest in-horizon reservation ahead.
      sim_time_t conservative_window = calculate_conservative_window(
          i, available_nodes, effective_running_jobs, current_time);

      // Job can backfill if it completes before ANY job ahead needs to start
      if (current_time + release_estimate(current_time, job.run_time_estimate) <
          conservative_window) {
        jobs_to_run.push_back(job.job_id);
        available_nodes -= job.nodes_requested;
        m_wait_queue[i].removed = true;
        ++m_removed_count;

        // Update effective running jobs for next iteration
        effective_running_jobs[job.job_id] = {
            current_time, release_estimate(current_time, job.run_time_estimate),
            job.nodes_requested};

        if (available_nodes == 0)
          break;
      }
    }
  }

  return jobs_to_run;
}

} // namespace dr_evt
