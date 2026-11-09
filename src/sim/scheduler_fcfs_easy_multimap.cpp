/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_easy_multimap.cpp
 * @brief FCFS starts and EASY backfill using a multimap waiting queue.
 * @details Jobs are considered in arrival order. When the head must wait,
 * protect its predicted start and allow later jobs only when they do not
 * delay it. All resources are interchangeable capacity counts.
 * @note CLI: --priority_policy fcfs --backfill_policy easy --queue_impl
 * multimap. Use --dispatch_interval 0 and --backfill_interval 0; omit
 * --per_pass_accounting. --backfill_policy none disables backfill.
 * --priority_policy fcfs_alt also selects this queue.
 * Protobuf: Simulation_Params and InitRequest use the same field names
 * without the leading --; see dr_evt_params.proto and dr_evt_service.proto.
 */

#include "sim/scheduler_fcfs_easy_multimap.hpp"
#include <algorithm>

namespace dr_evt {

std::optional<tdiff_t>
FCFSEasyMultimapScheduler::waiting_resource_area() const {
  tdiff_t area = 0.0;
  for (const auto &[submit_time, job] : m_wait_queue) {
    (void)submit_time;
    if (m_eligible_jobs.contains(job.job_id)) {
      area += static_cast<tdiff_t>(job.nodes) * job.run_time;
    }
  }
  return area;
}

FCFSEasyMultimapScheduler::FCFSEasyMultimapScheduler(
    num_nodes_t total_nodes, BackfillPolicy backfill_policy,
    size_t backfill_queue_depth)
    : SchedulerBase(total_nodes, backfill_policy), m_current_tracked_time(0.0),
      m_backfill_queue_depth(backfill_queue_depth) {}

void FCFSEasyMultimapScheduler::insert_job(job_no_t job_id,
                                           sim_time_t submit_time,
                                           tdiff_t run_time,
                                           num_nodes_t nodes) {
  JobEntry entry{job_id, submit_time, run_time, nodes};

  // KEY DIFFERENCE: Order by submit_time (FCFS) instead of run_time (SJF)
  m_wait_queue.insert({submit_time, entry});

  // If already eligible, add to eligible set
  if (submit_time <= m_current_tracked_time) {
    m_eligible_jobs.insert(job_id);
  }
}

void FCFSEasyMultimapScheduler::update_eligible_jobs(sim_time_t current_time) {
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

std::multimap<sim_time_t, FCFSEasyMultimapScheduler::JobEntry>::iterator
FCFSEasyMultimapScheduler::find_fcfs_head() {
  // FCFS head = first eligible job in submit_time order
  // Since multimap is ordered by submit_time, just find first eligible
  for (auto it = m_wait_queue.begin(); it != m_wait_queue.end(); ++it) {
    const JobEntry &entry = it->second;
    if (m_eligible_jobs.count(entry.job_id) == 0)
      continue;

    // Found first eligible job (earliest submit_time)
    return it;
  }

  return m_wait_queue.end();
}

std::vector<job_no_t>
FCFSEasyMultimapScheduler::schedule(num_nodes_t free_nodes,
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
    // (see SJFEasyScheduler for full reasoning).
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

  if (m_fcfs_reservation_time <= current_time) {
    return {}; // No valid reservation window
  }

#ifdef DR_EVT_LEGACY_RESERVATION_BARRIER
  const tdiff_t backfill_window = m_fcfs_reservation_time - current_time;
#endif
  // A head reserved beyond the planning horizon is not planned, so the
  // window does not constrain candidates (Slurm bf_window).
  const bool head_within_horizon =
      reservation_within_horizon(m_fcfs_reservation_time, current_time);
  const bool head_has_projection =
      m_fcfs_reservation_time != std::numeric_limits<sim_time_t>::max();
  record_planned_start(head_job_id, m_fcfs_reservation_time,
                       head_within_horizon);
  if (head_within_horizon) {
    reservation_plan.reserve(m_fcfs_reservation_time, head_nodes,
                             release_estimate(m_fcfs_reservation_time,
                                              fcfs_head_it->second.run_time));
  }

  // Scan in FCFS order (earliest submit_time first) for backfill candidates.
  // Every candidate that qualifies is started in this one call, as in the
  // index-based FCFS schedulers, so --backfill_queue_depth counts the same
  // jobs whichever implementation runs. Starting a candidate leaves the
  // window unchanged: it ends before the head's reservation, so its nodes
  // are back before the head needs them.
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

    // IMPORTANT: Use strict < (not <=) because resources don't become available
    // instantly when a job completes. If a backfill job would complete exactly
    // at the reservation time, the FCFS head must wait for that completion
    // event to be processed first, causing a delay. Therefore, backfill jobs
    // must complete BEFORE (not at) the reservation time.
#ifdef DR_EVT_LEGACY_RESERVATION_BARRIER
    const bool fits_window =
        head_has_projection &&
        (!head_within_horizon ||
         release_estimate(current_time, entry.run_time) < backfill_window);
#else
    const bool fits_window =
        head_has_projection &&
        (!head_within_horizon ||
         reservation_plan.can_fit(
             current_time, entry.nodes,
             release_estimate(current_time, entry.run_time)));
#endif

    // EASY must fit both nodes and window. CONSERVATIVE uses the same test
    // here (conservative approximation; the hybrid schedulers implement the
    // full rule).
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
