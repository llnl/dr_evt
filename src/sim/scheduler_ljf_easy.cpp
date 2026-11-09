/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_ljf_easy.cpp
 * @brief Longest-job-first starts with EASY backfill.
 * @details Order runnable jobs by requested time limit, longest first. When
 * the FCFS head must wait, protect its predicted start before allowing
 * backfill. The queue container is fixed by this implementation.
 * @note CLI: --priority_policy ljf --backfill_policy easy, with both
 * dispatch/backfill intervals zero and no --per_pass_accounting.
 * --queue_impl does not select another container. --backfill_policy none
 * disables backfill; hybrid and conservative are unsupported.
 * Protobuf: Simulation_Params and InitRequest use the same field names
 * without the leading --; see dr_evt_params.proto and dr_evt_service.proto.
 */

#include "sim/scheduler_ljf_easy.hpp"
#include <algorithm>

namespace dr_evt {

LJFEasyScheduler::LJFEasyScheduler(num_nodes_t total_nodes,
                                   BackfillPolicy backfill_policy,
                                   size_t backfill_queue_depth)
    : SchedulerBase(total_nodes, backfill_policy), m_current_tracked_time(0.0),
      m_backfill_queue_depth(backfill_queue_depth) {}

void LJFEasyScheduler::insert_job(job_no_t job_id, sim_time_t submit_time,
                                  tdiff_t run_time, num_nodes_t nodes) {
  JobEntry entry{job_id, submit_time, run_time, nodes};
  m_wait_queue.insert({run_time, entry});

  // If already eligible, add to eligible set
  if (submit_time <= m_current_tracked_time) {
    m_eligible_jobs.insert(job_id);
  }
}

void LJFEasyScheduler::update_eligible_jobs(sim_time_t current_time) {
  if (current_time <= m_current_tracked_time) {
    return; // Time hasn't advanced
  }

  // Scan wait_queue for newly eligible jobs
  for (auto &pair : m_wait_queue) {
    const JobEntry &entry = pair.second;
    if (entry.submit_time <= current_time &&
        entry.submit_time > m_current_tracked_time) {
      m_eligible_jobs.insert(entry.job_id);
    }
  }

  m_current_tracked_time = current_time;
}

std::multimap<tdiff_t, LJFEasyScheduler::JobEntry,
              LJFEasyScheduler::DescendingRunTime>::iterator
LJFEasyScheduler::find_fcfs_head() {
  // FCFS head = eligible job with earliest submit_time
  auto fcfs_it = m_wait_queue.end();
  sim_time_t earliest_submit = std::numeric_limits<sim_time_t>::max();

  for (auto it = m_wait_queue.begin(); it != m_wait_queue.end(); ++it) {
    const JobEntry &entry = it->second;
    if (m_eligible_jobs.count(entry.job_id) == 0)
      continue;

    if (entry.submit_time < earliest_submit) {
      earliest_submit = entry.submit_time;
      fcfs_it = it;
    }
  }

  return fcfs_it;
}

std::vector<job_no_t>
LJFEasyScheduler::schedule(num_nodes_t free_nodes,
                           const running_jobs_t &running_jobs,
                           sim_time_t current_time) {
  // Update eligibility tracking
  update_eligible_jobs(current_time);

  if (m_wait_queue.empty() || m_eligible_jobs.empty()) {
    return {};
  }

  // Find FCFS head (for reservation time calculation)
  auto fcfs_head_it = find_fcfs_head();
  if (fcfs_head_it == m_wait_queue.end()) {
    return {}; // No eligible jobs
  }

  // Copy out what's needed before any erase invalidates this iterator
  // and the reference into it.
  job_no_t head_job_id = fcfs_head_it->second.job_id;
  num_nodes_t head_nodes = fcfs_head_it->second.nodes;

  // Check if FCFS head can run
  if (head_nodes <= free_nodes) {
    // Start FCFS head - erase immediately rather than lazily marking
    // (see SJFEasyScheduler for full reasoning: multimap erase-by-iterator
    // is O(1) amortized regardless of tree position).
    m_eligible_jobs.erase(head_job_id);
    m_wait_queue.erase(fcfs_head_it);
    return {head_job_id};
  }

  // FCFS head blocked - try backfilling
  // Calculate FCFS reservation time
  auto reservation_plan =
      reservation_timeline(free_nodes, running_jobs, current_time);
  m_fcfs_reservation_time =
      reservation_plan.earliest_fit(head_nodes, 0.0, current_time)
          .value_or(std::numeric_limits<sim_time_t>::max());
  const sim_time_t reservation_time = m_fcfs_reservation_time;

  if (reservation_time <= current_time) {
    return {}; // No valid reservation window
  }

#ifdef DR_EVT_LEGACY_RESERVATION_BARRIER
  const tdiff_t backfill_window = reservation_time - current_time;
#endif
  // A head reserved beyond the planning horizon is not planned, so the
  // window does not constrain candidates (Slurm bf_window).
  const bool head_within_horizon =
      reservation_within_horizon(reservation_time, current_time);
  const bool head_has_projection =
      reservation_time != std::numeric_limits<sim_time_t>::max();
  record_planned_start(head_job_id, m_fcfs_reservation_time,
                       head_within_horizon);
  if (head_within_horizon) {
    reservation_plan.reserve(
        reservation_time, head_nodes,
        release_estimate(reservation_time, fcfs_head_it->second.run_time));
  }

  // Scan in LJF order (longest first) for backfill candidates. Every candidate
  // that qualifies is started in this one call, as in the index-based FCFS
  // schedulers, so --backfill_queue_depth counts the same jobs whichever
  // scheduler runs. Starting a candidate leaves the window unchanged: it ends
  // no later than the head's reservation, so its nodes are back in time.
  std::vector<job_no_t> started;
  num_nodes_t available_nodes = free_nodes;
  size_t examined = head_within_horizon ? 1 : 0;
  for (auto it = m_wait_queue.begin();
       it != m_wait_queue.end() && available_nodes > 0;) {
    if (it == fcfs_head_it) {
      ++it;
      continue; // Skip FCFS head
    }

    const JobEntry &entry = it->second;
    if (m_eligible_jobs.count(entry.job_id) == 0) {
      ++it;
      continue;
    }
    if (m_backfill_queue_depth != 0) {
      if (examined >= m_backfill_queue_depth)
        break; // Queue depth reached for this pass.
      ++examined;
    }

    // Check backfill constraints
    bool fits_nodes = (entry.nodes <= available_nodes);
#ifdef DR_EVT_LEGACY_RESERVATION_BARRIER
    const bool fits_window =
        head_has_projection &&
        (!head_within_horizon ||
         release_estimate(current_time, entry.run_time) <= backfill_window);
#else
    const bool fits_window =
        head_has_projection &&
        (!head_within_horizon ||
         reservation_plan.can_fit(
             current_time, entry.nodes,
             release_estimate(current_time, entry.run_time)));
#endif

    // EASY must fit both nodes and window. CONSERVATIVE uses the same test
    // here (conservative approximation).
    if ((m_backfill_policy == BackfillPolicy::EASY ||
         m_backfill_policy == BackfillPolicy::CONSERVATIVE) &&
        fits_nodes && fits_window) {
      // Copy out what's needed before erasing invalidates entry.
      const job_no_t id = entry.job_id;
      available_nodes -= entry.nodes;
      if (head_within_horizon) {
        reservation_plan.reserve(
            current_time, entry.nodes,
            release_estimate(current_time, entry.run_time));
      }
      m_eligible_jobs.erase(id);
      it = m_wait_queue.erase(it);
      started.push_back(id);
      continue;
    }
    ++it;
  }

  return started;
}

} // namespace dr_evt
