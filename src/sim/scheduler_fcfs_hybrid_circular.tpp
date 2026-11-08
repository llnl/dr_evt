/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_hybrid_circular.tpp
 * @brief Shared FCFS queue scan with a supplied capacity-reservation
 * planner.
 * @details Used by event-driven hybrid/conservative backfill, separate
 * periodic backfill, and fixed periodic dispatch. It protects future
 * capacity counts, not particular nodes. The calling scheduler supplies its
 * forecast and examination rules.
 * @note CLI: No option selects this helper directly. Callers use
 * --queue_impl circular with --backfill_policy hybrid or conservative;
 * --backfill_interval I selects separate periodic backfill and
 * --dispatch_interval I selects periodic dispatch. Both intervals zero
 * selects event-driven scheduling.
 * Protobuf: Simulation_Params and InitRequest use the same field names
 * without the leading --; see dr_evt_params.proto and dr_evt_service.proto.
 */

template <typename ProtectionTimeline>
std::vector<job_no_t> FCFSHybridCircularScheduler::schedule_with_protection(
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
  if (active_job_count() == 0 || m_backfill_policy == BackfillPolicy::NONE) {
    return jobs_to_run;
  }

  // The blocked head's projection is kept for every policy so that
  // get_fcfs_head_shadow_time() reports it whether or not it is planned.
  m_fcfs_reservation_time = calculate_fcfs_reservation(
      m_wait_queue.front().nodes_requested, available_nodes, running_jobs,
      current_time, projected_starts);

  if (available_nodes == 0) {
    return jobs_to_run;
  }

  // Share one depth budget across every backfill pass at this timestamp.
  // A job beyond the planning horizon stays queued, holds no reservation, and
  // is projected again on the next pass instead of requiring blocked-list
  // state. Whether it also uses a depth slot is the subclass's choice.
  const bool horizon_job_uses_slot = horizon_skipped_job_uses_depth_slot();
  if (m_backfill_depth_time != current_time) {
    m_backfill_depth_time = current_time;
    m_backfill_depth_examined = 0;
    m_backfill_depth_head_accounted = false;
  }
  if ((horizon_job_uses_slot ||
       reservation_within_horizon(m_fcfs_reservation_time, current_time)) &&
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
  ProtectionTimeline protected_plan(std::move(baseline_plan));
  // The flexible rule protects resource-time commitments, not an earliest
  // finish-time barrier. Build that commitment plan once and extend it as the
  // scan advances; reconstructing it for every candidate repeated the same
  // map allocations and prefix reservations on this hot path.
#else
  // Reuse the availability projection for all candidate horizon checks.
  // Accepted starts extend it, avoiding a release-timeline rebuild per job.
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
    protected_plan.reserve(
        *reservation, protected_job.nodes_requested,
        release_estimate(*reservation, protected_job.run_time_estimate));
  };
  protect_job(m_wait_queue.front());
#endif
  for (size_t i = 1; i < m_eligible_end_idx; ++i) {
    const auto &job = m_wait_queue[i];
    if (job.removed) {
      continue;
    }
    const sim_time_t job_reservation =
        availability_plan.earliest_fit(job.nodes_requested, 0.0, current_time)
            .value_or(std::numeric_limits<sim_time_t>::max());
    const bool in_horizon =
        reservation_within_horizon(job_reservation, current_time);
    if (!in_horizon && !horizon_job_uses_slot) {
      continue;
    }
    if (m_backfill_queue_depth != 0 &&
        m_backfill_depth_examined >= m_backfill_queue_depth) {
      break;
    }
    ++m_backfill_depth_examined;
    if (!in_horizon) {
      continue; // Counted, but it holds no reservation and cannot backfill.
    }
    if (job.nodes_requested > available_nodes) {
#ifndef DR_EVT_LEGACY_RESERVATION_BARRIER
      // It cannot start now, but it may still be a protected reservation for
      // candidates farther back in the queue.
      protect_job(job);
#endif
      continue;
    }
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
    if (preserves_reservations) {
      jobs_to_run.push_back(job.job_id);
      available_nodes -= job.nodes_requested;
      m_wait_queue[i].removed = true;
      ++m_removed_count;
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
      if (available_nodes == 0) {
        break;
      }
    } else {
#ifndef DR_EVT_LEGACY_RESERVATION_BARRIER
      protect_job(job, protected_start);
#endif
    }
  }
  return jobs_to_run;
}
