/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_pass_hybrid_circular.cpp
 * @brief Event-driven FCFS queue scans with limits reset for each scan.
 * @details A pass is one attempt to scan the waiting queue at the current
 * simulation time. Starts and successful reservations count toward Q; failed
 * tests after reaching R also count. A new pass restarts at the head with
 * fresh Q/R, without advancing time. Identical reservation-only retries are
 * stopped by a simulation guard. The OpenPBS subclass reuses this scan with
 * its own forecast.
 * @note CLI: --per_pass_accounting --priority_policy fcfs --queue_impl
 * circular. --backfill_queue_depth sets Q (0 is unlimited).
 * --backfill_policy hybrid --reservation_depth R protects R jobs; easy
 * protects one and conservative protects all examined jobs. Keep both
 * intervals zero and omit --planning_horizon for unlimited look-ahead.
 * --priority_policy fcfs_1s_evt_group selects the OpenPBS forecast without
 * requiring --per_pass_accounting.
 * Protobuf: Simulation_Params and InitRequest use the same field names
 * without the leading --; see dr_evt_params.proto and dr_evt_service.proto.
 */

#include "sim/scheduler_fcfs_pass_hybrid_circular.hpp"
#include "sim/forecast_1s_evt_group_timeline.hpp"
#include "sim/scheduler_fcfs_1s_evt_group_circular.hpp"
#include <tuple>

namespace dr_evt {

template <typename Timeline>
std::vector<job_no_t> FCFSPassHybridCircularScheduler::schedule_with_forecast(
    num_nodes_t free_nodes, const running_jobs_t &running_jobs,
    sim_time_t current_time) {
  if (m_planning_horizon != std::numeric_limits<tdiff_t>::max()) {
    throw std::invalid_argument(
        "per_pass_accounting requires an unlimited planning_horizon");
  }
  sync_to(current_time);
  if (current_time != m_state_time || free_nodes != m_state_free ||
      running_jobs.size() != m_state_running ||
      m_outlook_capacity != m_state_capacity ||
      m_outlook_changes_begin != m_state_changes) {
    m_blocked.clear();
  }
  m_state_time = current_time;
  m_state_free = free_nodes;
  m_state_running = running_jobs.size();
  m_state_capacity = m_outlook_capacity;
  m_state_changes = m_outlook_changes_begin;

  // Compact only this subclass's queue; inherited traversals are untouched.
  if (m_removed_count != 0) {
    m_eligible_end_idx -= m_removed_count;
    auto end = std::remove_if(m_wait_queue.begin(), m_wait_queue.end(),
                              [](const JobEntry &job) { return job.removed; });
    m_wait_queue.erase(end, m_wait_queue.end());
    m_removed_count = 0;
  }
  std::vector<job_no_t> starts;
  projected_starts_t projected;
  using reservation_t = std::tuple<job_no_t, sim_time_t, tdiff_t>;
  std::vector<reservation_t> previous_plan;
  bool have_previous_plan = false;
  m_last_pass_count = 0;
  m_last_retry_stopped = false;
  const size_t reservation_limit =
      m_backfill_policy == BackfillPolicy::CONSERVATIVE
          ? std::numeric_limits<size_t>::max()
      : m_backfill_policy == BackfillPolicy::EASY   ? 1
      : m_backfill_policy == BackfillPolicy::HYBRID ? m_reservation_depth
                                                    : 0;
  // Each changing pass starts or blocks at least one job; two unchanged
  // reservation-only passes suffice to detect a deterministic repeated plan.
  const size_t pass_limit = active_job_count() + 2;
  for (size_t pass = 0; pass < pass_limit; ++pass) {
    ++m_last_pass_count;
    Timeline plan(reservation_timeline(free_nodes, running_jobs, current_time,
                                       projected));
    std::vector<reservation_t> reservations;
    size_t tested = 0;
    size_t cursor = 0;
    size_t blocked_before = m_blocked.size();
    size_t starts_before = starts.size();
    bool dirty = false;
    m_fcfs_reservation_time = 0;
    for (; cursor < m_eligible_end_idx; ++cursor) {
      auto &job = m_wait_queue[cursor];
      if (job.removed || m_blocked.count(job.job_id) != 0) {
        continue;
      }
      if (m_backfill_queue_depth != 0 && tested >= m_backfill_queue_depth) {
        break;
      }
      const tdiff_t duration =
          release_estimate(current_time, job.run_time_estimate);
      const bool try_reserve = reservations.size() < reservation_limit;
      const auto at =
          plan.earliest_fit(job.nodes_requested, duration, current_time);
      if (at && *at == current_time) {
        starts.push_back(job.job_id);
        free_nodes -= job.nodes_requested;
        projected.push_back({current_time, duration, job.nodes_requested});
        plan.reserve(current_time, job.nodes_requested, duration);
        job.removed = true;
        ++m_removed_count;
        ++tested;
        dirty = true;
      } else if (try_reserve && at) {
        const tdiff_t reserved_duration =
            release_estimate(*at, job.run_time_estimate);
        plan.reserve(*at, job.nodes_requested, reserved_duration);
        reservations.emplace_back(job.job_id, *at, reserved_duration);
        if (reservations.size() == 1) {
          m_fcfs_reservation_time = *at;
        }
        ++tested;
        dirty = true;
      } else if (try_reserve) {
        // Allocation-or-reservation failure: no Q or R charge. A state
        // change re-enables this entry; it retains its original FCFS position.
        m_blocked.insert(job.job_id);
      } else {
        // Allocation-only failure consumes Q, including after reaching R.
        ++tested;
        if (m_backfill_policy == BackfillPolicy::NONE) {
          return starts;
        }
      }
    }
    // Native pending excludes allocated/blocked entries. Ignore them when
    // deciding whether candidates remain after this pass's Q boundary.
    while (cursor < m_eligible_end_idx &&
           (m_wait_queue[cursor].removed ||
            m_blocked.count(m_wait_queue[cursor].job_id) != 0)) {
      ++cursor;
    }
    const bool retry = m_backfill_queue_depth != 0 &&
                       tested >= m_backfill_queue_depth &&
                       cursor < m_eligible_end_idx && dirty;
    if (!retry) {
      return starts;
    }
    // Tentative simulation guard: we have not established which native
    // callback sequence prevents reservation-only restarts. Do not treat
    // this as a verified reproduction of that mechanism; revisit it when
    // Fluxion's complete restart handling has been traced.
    if (have_previous_plan && starts.size() == starts_before &&
        m_blocked.size() == blocked_before && reservations == previous_plan) {
      m_last_retry_stopped = true;
      return starts;
    }
    previous_plan = std::move(reservations);
    have_previous_plan = true;
  }
  // Defensive ceiling in addition to exact repeated-plan detection.
  m_last_retry_stopped = true;
  return starts;
}

std::vector<job_no_t>
FCFSPassHybridCircularScheduler::schedule(num_nodes_t free_nodes,
                                          const running_jobs_t &running_jobs,
                                          sim_time_t current_time) {
  return schedule_with_forecast<Reservation_Timeline>(free_nodes, running_jobs,
                                                      current_time);
}

std::vector<job_no_t> FCFSOneSecondEventGroupCircularScheduler::schedule(
    num_nodes_t free_nodes, const running_jobs_t &running_jobs,
    sim_time_t current_time) {
  return schedule_with_forecast<OneSecondEventGroupTimeline>(
      free_nodes, running_jobs, current_time);
}

} // namespace dr_evt
