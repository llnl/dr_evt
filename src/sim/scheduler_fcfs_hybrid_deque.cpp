/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_hybrid_deque.cpp
 * @brief FCFS starts and hybrid/conservative backfill using a deque queue.
 * @details Hybrid protects up to R waiting jobs; conservative protects every
 * examined waiting job. Later jobs may start only when they preserve those
 * capacity reservations. Default examination accounting is shared across
 * calls at the same timestamp.
 * @note CLI: --priority_policy fcfs --backfill_policy hybrid
 * --reservation_depth R --queue_impl deque, with R > 0. For conservative use
 * --priority_policy fcfs_conservative --backfill_policy conservative. Use
 * --dispatch_interval 0 and --backfill_interval 0; omit
 * --per_pass_accounting.
 * Protobuf: Simulation_Params and InitRequest use the same field names
 * without the leading --; see dr_evt_params.proto and dr_evt_service.proto.
 */

#include "sim/scheduler_fcfs_hybrid_deque.hpp"
#include <algorithm>

namespace dr_evt {

void FCFSHybridDequeScheduler::sync_to(sim_time_t current_time) {
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

void FCFSHybridDequeScheduler::mark_removed(job_no_t job_id) {
  for (auto &entry : m_wait_queue) {
    if (entry.job_id == job_id && !entry.removed) {
      entry.removed = true;
      ++m_removed_count;
      return;
    }
  }
}

sim_time_t FCFSHybridDequeScheduler::calculate_hybrid_window(
    size_t backfill_index, num_nodes_t available_nodes,
    const running_jobs_t &running_jobs, sim_time_t current_time,
    const projected_starts_t &projected_starts) {
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
  auto plan = reservation_timeline(available_nodes, running_jobs, current_time,
                                   projected_starts);
  for (size_t i = 0; i < backfill_index; ++i) {
    const auto &job = m_wait_queue[i];
    if (job.removed)
      continue;
    if (depth != 0 && reserved >= depth)
      break;

    // Place each protected waiting job into the same temporary resource-time
    // plan before calculating the next reservation.
    const tdiff_t duration =
        release_estimate(current_time, job.run_time_estimate);
    const auto reservation =
        plan.earliest_fit(job.nodes_requested, duration, current_time);
    if (!reservation) {
      continue;
    }
    const sim_time_t job_reservation = *reservation;
    // Beyond the planning horizon the job is not planned: no reservation
    // and no reservation-depth slot (Slurm bf_window).
    if (!reservation_within_horizon(job_reservation, current_time))
      continue;
    ++reserved;
    plan.reserve(job_reservation, job.nodes_requested,
                 release_estimate(job_reservation, job.run_time_estimate));

    // Track the earliest reservation time
    earliest_conflict = std::min(earliest_conflict, job_reservation);
  }

  return earliest_conflict;
}

std::vector<job_no_t>
FCFSHybridDequeScheduler::schedule(num_nodes_t free_nodes,
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
  projected_starts_t projected_starts;

  // Step 1: Start jobs from the front of the queue (FCFS order)
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
      m_wait_queue.front().nodes_requested, available_nodes, running_jobs,
      current_time, projected_starts);
  record_estimated_start(m_wait_queue.front().job_id,
                       m_fcfs_reservation_time, false);
  if (available_nodes == 0) {
    return jobs_to_run;
  }

  // CONSERVATIVE backfilling: Must not delay ANY waiting job.
  // HYBRID: must not delay the first m_reservation_depth waiting jobs.

  // Share one depth budget across every backfill pass at this timestamp.
  // Jobs with no reservation in the planning horizon stay in queue but do
  // not consume a scan slot. Their projection is recomputed on every pass,
  // which captures ordinary resource releases and capacity changes without
  // modeling an external blocked-list notification.
  if (m_backfill_depth_time != current_time) {
    m_backfill_depth_time = current_time;
    m_backfill_depth_examined = 0;
    m_backfill_depth_head_accounted = false;
  }
  if (reservation_within_horizon(m_fcfs_reservation_time, current_time) &&
      !m_backfill_depth_head_accounted) {
    ++m_backfill_depth_examined;
    m_backfill_depth_head_accounted = true;
  }
#ifndef DR_EVT_LEGACY_RESERVATION_BARRIER
  // Build the common initial state once, then keep two independent value
  // copies as accepted starts and protected reservations diverge.
  auto baseline_plan = reservation_timeline(available_nodes, running_jobs,
                                            current_time, projected_starts);
  auto availability_plan = baseline_plan;
  auto protected_plan = std::move(baseline_plan);
  // Keep one incrementally extended plan for the full scan. Replaying every
  // protected prefix for each candidate made flexible reservations quadratic
  // in the scanned prefix and repeatedly allocated timeline-map nodes.
#else
  // Candidate horizon checks need the same availability projection as the
  // previous full map-based calculation. Extend it as jobs start instead of
  // rebuilding release maps for every candidate.
  auto availability_plan = reservation_timeline(available_nodes, running_jobs,
                                                current_time, projected_starts);
#endif
#ifndef DR_EVT_LEGACY_RESERVATION_BARRIER
  const size_t protected_depth =
      m_backfill_policy == BackfillPolicy::HYBRID ? m_reservation_depth : 0;
  size_t protected_count = 0;
  const auto protect_job = [&](const JobEntry &protected_job,
                               std::optional<sim_time_t> reservation =
                                   std::nullopt) {
    if (protected_depth != 0 && protected_count >= protected_depth) {
      return;
    }
    const tdiff_t duration =
        release_estimate(current_time, protected_job.run_time_estimate);
    if (!reservation) {
      reservation = protected_plan.earliest_fit(protected_job.nodes_requested,
                                                duration, current_time);
    }
    if (!reservation ||
        !reservation_within_horizon(*reservation, current_time)) {
      return;
    }
    ++protected_count;
    record_estimated_start(protected_job.job_id, *reservation, true);
    protected_plan.reserve(
        *reservation, protected_job.nodes_requested,
        release_estimate(*reservation, protected_job.run_time_estimate));
  };
  protect_job(m_wait_queue.front());
#endif
  for (size_t i = 1; i < m_eligible_end_idx; ++i) {
    const auto &job = m_wait_queue[i];
    if (job.removed)
      continue;
    const sim_time_t job_reservation =
        availability_plan.earliest_fit(job.nodes_requested, 0.0, current_time)
            .value_or(std::numeric_limits<sim_time_t>::max());
    if (!reservation_within_horizon(job_reservation, current_time)) {
      if (m_backfill_queue_depth == 0 ||
          m_backfill_depth_examined < m_backfill_queue_depth) {
        record_estimated_start(job.job_id, job_reservation, false);
      }
      continue;
    }
    if (m_backfill_queue_depth != 0 &&
        m_backfill_depth_examined >= m_backfill_queue_depth)
      break;
    ++m_backfill_depth_examined;
    record_estimated_start(job.job_id, job_reservation, false);

    // Check resource fit
    if (job.nodes_requested > available_nodes) {
#ifndef DR_EVT_LEGACY_RESERVATION_BARRIER
      // Preserve this blocked job's reservation for later candidates even
      // though it cannot consume currently free nodes itself.
      protect_job(job);
#endif
      continue;
    }

    // Calculate the window: earliest in-horizon reservation ahead.
#ifdef DR_EVT_LEGACY_RESERVATION_BARRIER
    const sim_time_t hybrid_window = calculate_hybrid_window(
        i, available_nodes, running_jobs, current_time, projected_starts);
#else
    const auto protected_start = protected_plan.earliest_fit(
        job.nodes_requested,
        release_estimate(current_time, job.run_time_estimate), current_time);
    const bool preserves_reservations =
        protected_start && *protected_start == current_time;
#endif
#ifdef DR_EVT_LEGACY_RESERVATION_BARRIER
    const bool preserves_reservations =
        current_time + release_estimate(current_time, job.run_time_estimate) <
        hybrid_window;
#endif
    // Job can backfill if it completes before ANY job ahead needs to start
    if (preserves_reservations) {
      jobs_to_run.push_back(job.job_id);
      available_nodes -= job.nodes_requested;
      m_wait_queue[i].removed = true;
      ++m_removed_count;

      // Update effective running jobs for next iteration
      projected_starts.push_back(
          {current_time, release_estimate(current_time, job.run_time_estimate),
           job.nodes_requested});
      availability_plan.reserve(
          current_time, job.nodes_requested,
          release_estimate(current_time, job.run_time_estimate));
#ifndef DR_EVT_LEGACY_RESERVATION_BARRIER
      protected_plan.reserve(
          current_time, job.nodes_requested,
          release_estimate(current_time, job.run_time_estimate));
#endif

      if (available_nodes == 0)
        break;
    } else {
#ifndef DR_EVT_LEGACY_RESERVATION_BARRIER
      protect_job(job, protected_start);
#endif
    }
  }

  return jobs_to_run;
}

} // namespace dr_evt
