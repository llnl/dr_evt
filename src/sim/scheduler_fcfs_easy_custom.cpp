/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include "sim/scheduler_fcfs_easy_custom.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace dr_evt {

FCFSEasyCustomScheduler::FCFSEasyCustomScheduler(
    num_nodes_t total_nodes, size_t initial_job_count, BackfillPolicy bf_policy,
    size_t num_max_candidates, job_cost_function_t cost_function,
    backfill_selector_t selector, size_t initial_capacity,
    CircularOverflowPolicy overflow_policy)
    : FCFSEasyCustomScheduler(total_nodes, initial_job_count, bf_policy,
                              num_max_candidates, initial_capacity,
                              overflow_policy) {
  m_job_cost_function = std::move(cost_function);
  m_backfill_selector = std::move(selector);
  if (!m_job_cost_function) {
    throw std::invalid_argument(
        "FCFSEasyCustomScheduler requires a job cost function");
  }
  if (!m_backfill_selector) {
    throw std::invalid_argument(
        "FCFSEasyCustomScheduler requires a backfill selector");
  }
}

FCFSEasyCustomScheduler::FCFSEasyCustomScheduler(
    num_nodes_t total_nodes, size_t initial_job_count, BackfillPolicy bf_policy,
    size_t num_max_candidates, size_t initial_capacity,
    CircularOverflowPolicy overflow_policy)
    : SchedulerBase(total_nodes, bf_policy),
      m_wait_queue(initial_capacity != 0 ? initial_capacity
                                         : initial_job_count),
      m_overflow_policy(overflow_policy), m_eligible_end_idx(0),
      m_current_tracked_time(0.0), m_removed_count(0),
      m_num_max_candidates(num_max_candidates), m_newly_eligible_begin_idx(0),
      m_arrival_update_pending(false), m_reevaluate_all_candidates(false),
      m_candidate_preparation_pending(true), m_resource_area(0.0),
      m_resource_area_time(0.0), m_resource_area_start(0.0),
      m_accounted_available_nodes(total_nodes) {
  if (m_num_max_candidates == 0) {
    throw std::invalid_argument(
        "FCFSEasyCustomScheduler requires num_max_candidates > 0");
  }
  if (m_backfill_policy == BackfillPolicy::CONSERVATIVE) {
    throw std::invalid_argument(
        "FCFSEasyCustomScheduler supports EASY or NONE backfilling");
  }
}

void FCFSEasyCustomScheduler::advance_resource_accounting_to(
    sim_time_t current_time) {
  if (current_time < m_resource_area_time) {
    throw std::logic_error("resource accounting cannot move backward in time");
  }
  if (!std::isfinite(current_time)) {
    return;
  }
  const num_nodes_t allocated_nodes =
      m_total_nodes - m_accounted_available_nodes;
  m_resource_area += static_cast<tdiff_t>(allocated_nodes) *
                     (current_time - m_resource_area_time);
  m_resource_area_time = current_time;
}

void FCFSEasyCustomScheduler::commit_available_nodes(
    num_nodes_t available_nodes) {
  if (available_nodes > m_total_nodes) {
    throw std::logic_error("available nodes exceed total system capacity");
  }
  m_accounted_available_nodes = available_nodes;
}

void FCFSEasyCustomScheduler::reset_resource_accounting() {
  reset_resource_accounting(0.0, m_total_nodes);
}

void FCFSEasyCustomScheduler::reset_resource_accounting(
    sim_time_t start_time, num_nodes_t available_nodes) {
  if (!std::isfinite(start_time) || start_time < 0.0) {
    throw std::invalid_argument(
        "resource accounting start time must be finite and nonnegative");
  }
  if (available_nodes > m_total_nodes) {
    throw std::invalid_argument(
        "resource accounting available nodes exceed total capacity");
  }
  m_resource_area = 0.0;
  m_resource_area_time = start_time;
  m_resource_area_start = start_time;
  m_accounted_available_nodes = available_nodes;
}

tdiff_t
FCFSEasyCustomScheduler::resource_area_through(sim_time_t through_time) const {
  tdiff_t area = m_resource_area;
  if (std::isfinite(through_time) && through_time > m_resource_area_time) {
    const num_nodes_t allocated_nodes =
        m_total_nodes - m_accounted_available_nodes;
    area += static_cast<tdiff_t>(allocated_nodes) *
            (through_time - m_resource_area_time);
  }
  return area;
}

double
FCFSEasyCustomScheduler::utilization_through(sim_time_t through_time) const {
  const sim_time_t duration = through_time - m_resource_area_start;
  if (m_total_nodes == 0 || duration <= 0.0) {
    return 0.0;
  }
  return resource_area_through(through_time) /
         (static_cast<double>(m_total_nodes) * duration);
}

std::optional<tdiff_t> FCFSEasyCustomScheduler::waiting_resource_area() const {
  tdiff_t area = 0.0;
  for (size_t i = 0; i < m_eligible_end_idx; ++i) {
    const auto &job = m_wait_queue[i];
    if (!job.removed) {
      area += static_cast<tdiff_t>(job.nodes_requested) * job.run_time_estimate;
    }
  }
  return area;
}

std::optional<size_t> FCFSEasyCustomScheduler::select_backfill_candidate(
    const backfill_candidates_t &candidates, num_nodes_t available_nodes,
    const running_jobs_t &effective_running_jobs, sim_time_t current_time) {
  (void)available_nodes;
  (void)effective_running_jobs;
  (void)current_time;
  if (!m_backfill_selector) {
    throw std::logic_error("FCFSEasyCustomScheduler subclass did not implement "
                           "candidate selection");
  }
  return m_backfill_selector(candidates);
}

std::optional<double>
FCFSEasyCustomScheduler::running_job_power(const JobEntry &job) const {
  (void)job;
  return std::nullopt;
}

bool FCFSEasyCustomScheduler::can_start_fcfs_job(
    const JobEntry &job, num_nodes_t available_nodes,
    const running_jobs_t &effective_running_jobs,
    sim_time_t current_time) const {
  (void)job;
  (void)available_nodes;
  (void)effective_running_jobs;
  (void)current_time;
  return true;
}

sim_time_t FCFSEasyCustomScheduler::fcfs_head_reservation_time(
    const JobEntry &job, num_nodes_t available_nodes,
    const running_jobs_t &effective_running_jobs, sim_time_t current_time) {
  return calculate_fcfs_reservation(job.nodes_requested, available_nodes,
                                    effective_running_jobs, current_time);
}

void FCFSEasyCustomScheduler::on_jobs_became_eligible(
    size_t newly_eligible_begin, size_t eligible_end,
    num_nodes_t available_nodes, const running_jobs_t &running_jobs,
    sim_time_t current_time) {
  (void)newly_eligible_begin;
  (void)eligible_end;
  (void)available_nodes;
  (void)running_jobs;
  (void)current_time;
}

void FCFSEasyCustomScheduler::on_backfill_candidates_ready(
    const backfill_candidates_t &candidates, num_nodes_t available_nodes,
    const running_jobs_t &effective_running_jobs, sim_time_t current_time,
    bool fcfs_jobs_started) {
  (void)candidates;
  (void)available_nodes;
  (void)effective_running_jobs;
  (void)current_time;
  (void)fcfs_jobs_started;
}

void FCFSEasyCustomScheduler::on_scheduling_cycle_complete(
    num_nodes_t available_nodes, const running_jobs_t &running_jobs,
    sim_time_t current_time) {
  (void)available_nodes;
  (void)running_jobs;
  (void)current_time;
}

void FCFSEasyCustomScheduler::finish_scheduling_cycle(
    num_nodes_t available_nodes, const running_jobs_t &running_jobs,
    sim_time_t current_time) {
  on_scheduling_cycle_complete(available_nodes, running_jobs, current_time);
  complete_candidate_scan();
}

void FCFSEasyCustomScheduler::insert_job(job_no_t job_id,
                                         sim_time_t submit_time,
                                         tdiff_t run_time_estimate,
                                         num_nodes_t nodes_requested) {
  if (!m_job_cost_function) {
    throw std::logic_error(
        "FCFSEasyCustomScheduler subclass must use insert_job_with_cost");
  }
  insert_job_with_cost(job_id, submit_time, run_time_estimate, nodes_requested,
                       m_job_cost_function(job_id, submit_time,
                                           run_time_estimate, nodes_requested));
}

void FCFSEasyCustomScheduler::insert_job_with_cost(job_no_t job_id,
                                                   sim_time_t submit_time,
                                                   tdiff_t run_time_estimate,
                                                   num_nodes_t nodes_requested,
                                                   job_cost_t cost) {
  if (m_wait_queue.full()) {
    if (m_overflow_policy == CircularOverflowPolicy::ABORT) {
      throw std::runtime_error(
          "FCFSEasyCustomScheduler: wait queue capacity (" +
          std::to_string(m_wait_queue.capacity()) + ") exceeded");
    }
    m_wait_queue.set_capacity(std::max<size_t>(m_wait_queue.capacity() * 2, 1));
  }

  m_wait_queue.push_back(
      JobEntry(job_id, submit_time, run_time_estimate, nodes_requested, cost));
  if (submit_time <= m_current_tracked_time) {
    m_newly_eligible_begin_idx =
        std::min(m_newly_eligible_begin_idx, m_eligible_end_idx);
    m_eligible_end_idx = m_wait_queue.size();
    m_arrival_update_pending = true;
    m_candidate_preparation_pending = true;
  }
}

void FCFSEasyCustomScheduler::sync_to(sim_time_t current_time) {
  if (current_time <= m_current_tracked_time) {
    return;
  }
  m_newly_eligible_begin_idx = m_eligible_end_idx;
  const size_t previous_eligible_end = m_eligible_end_idx;
  while (m_eligible_end_idx < m_wait_queue.size() &&
         m_wait_queue[m_eligible_end_idx].submit_time <= current_time) {
    ++m_eligible_end_idx;
  }
  if (m_eligible_end_idx != previous_eligible_end) {
    m_arrival_update_pending = true;
    m_candidate_preparation_pending = true;
  }
  m_current_tracked_time = current_time;
}

sim_time_t FCFSEasyCustomScheduler::get_next_arrival_time() {
  for (size_t i = m_eligible_end_idx; i < m_wait_queue.size(); ++i) {
    if (!m_wait_queue[i].removed) {
      return m_wait_queue[i].submit_time;
    }
  }
  return std::numeric_limits<sim_time_t>::max();
}

void FCFSEasyCustomScheduler::compact_if_needed() {
  if (m_removed_count == 0 || m_removed_count * 2 <= m_wait_queue.size()) {
    return;
  }

  size_t eligible_removed = 0;
  size_t newly_eligible_removed = 0;
  for (size_t i = 0; i < m_eligible_end_idx; ++i) {
    if (m_wait_queue[i].removed) {
      ++eligible_removed;
      if (i < m_newly_eligible_begin_idx) {
        ++newly_eligible_removed;
      }
    }
  }
  const auto new_end =
      std::remove_if(m_wait_queue.begin(), m_wait_queue.end(),
                     [](const JobEntry &entry) { return entry.removed; });
  m_wait_queue.erase(new_end, m_wait_queue.end());
  m_eligible_end_idx -= eligible_removed;
  m_newly_eligible_begin_idx -= newly_eligible_removed;
  m_removed_count = 0;
}

backfill_candidates_t FCFSEasyCustomScheduler::find_backfill_candidates(
    num_nodes_t available_nodes, sim_time_t current_time,
    sim_time_t reservation_time) const {
  return find_backfill_candidates_from(1, available_nodes, current_time,
                                       reservation_time, {});
}

backfill_candidates_t FCFSEasyCustomScheduler::find_backfill_candidates_from(
    size_t begin_idx, num_nodes_t available_nodes, sim_time_t current_time,
    sim_time_t reservation_time, const running_jobs_t &running_jobs,
    const projected_starts_t &projected_starts) const {
  backfill_candidates_t candidates;
  candidates.reserve(std::min(m_num_max_candidates, m_eligible_end_idx > 0
                                                        ? m_eligible_end_idx - 1
                                                        : size_t{0}));

  if (m_num_max_candidates == 0 || m_eligible_end_idx <= 1) {
    return candidates;
  }

  // A head reserved beyond the planning horizon is not planned, so every
  // fitting job is a candidate (Slurm bf_window).
  const bool head_within_horizon =
      reservation_within_horizon(reservation_time, current_time);
  auto reservation_plan = reservation_timeline(available_nodes, running_jobs,
                                               current_time, projected_starts);
#ifndef DR_EVT_LEGACY_RESERVATION_BARRIER
  if (head_within_horizon) {
    const auto &head = m_wait_queue.front();
    reservation_plan.reserve(
        reservation_time, head.nodes_requested,
        release_estimate(reservation_time, head.run_time_estimate));
  }
#endif
  // The queue depth is counted from the head, as in the built-in FCFS
  // schedulers, before any feasibility test or the candidate cap.
  const size_t scan_end =
      queue_depth_scan_end(m_wait_queue, 1, m_eligible_end_idx,
                           m_backfill_queue_depth, head_within_horizon);
  for (size_t i = std::max<size_t>(1, begin_idx); i < scan_end; ++i) {
    const auto &job = m_wait_queue[i];
    if (job.removed || job.nodes_requested > available_nodes) {
      continue;
    }
#ifdef DR_EVT_LEGACY_RESERVATION_BARRIER
    if (head_within_horizon &&
        current_time + release_estimate(current_time, job.run_time_estimate) >=
            reservation_time) {
      continue;
    }
#endif
#ifndef DR_EVT_LEGACY_RESERVATION_BARRIER
    if (head_within_horizon &&
        !reservation_plan.can_fit(
            current_time, job.nodes_requested,
            release_estimate(current_time, job.run_time_estimate))) {
      continue;
    }
#endif
    candidates.push_back({i, job.m_cost});
    if (candidates.size() == m_num_max_candidates) {
      break;
    }
  }
  return candidates;
}

std::vector<job_no_t>
FCFSEasyCustomScheduler::schedule(num_nodes_t free_nodes,
                                  const running_jobs_t &running_jobs,
                                  sim_time_t current_time) {
  sync_to(current_time);
  compact_if_needed();

  if (m_arrival_update_pending) {
    on_jobs_became_eligible(m_newly_eligible_begin_idx, m_eligible_end_idx,
                            free_nodes, running_jobs, current_time);
    m_arrival_update_pending = false;
  }

  if (m_eligible_end_idx == 0) {
    finish_scheduling_cycle(free_nodes, running_jobs, current_time);
    return {};
  }

  std::vector<job_no_t> jobs_to_run;
  num_nodes_t available_nodes = free_nodes;
  running_jobs_t effective_running_jobs = running_jobs;
  projected_starts_t projected_starts;

  while (m_eligible_end_idx > 0 && !m_wait_queue.empty()) {
    if (!m_wait_queue.front().removed &&
        (m_wait_queue.front().nodes_requested > available_nodes ||
         !can_start_fcfs_job(m_wait_queue.front(), available_nodes,
                             effective_running_jobs, current_time))) {
      break;
    }
    if (!m_wait_queue.front().removed) {
      const auto &job = m_wait_queue.front();
      jobs_to_run.push_back(job.job_id);
      available_nodes -= job.nodes_requested;
      const Running_Job projected_start{
          current_time, release_estimate(current_time, job.run_time_estimate),
          job.nodes_requested, running_job_power(job)};
      projected_starts.push_back(projected_start);
      effective_running_jobs[job.job_id] = projected_start;
    } else {
      --m_removed_count;
    }
    m_wait_queue.pop_front();
    --m_eligible_end_idx;
    if (m_newly_eligible_begin_idx > 0) {
      --m_newly_eligible_begin_idx;
    }
  }

  if (active_job_count() == 0 || m_backfill_policy == BackfillPolicy::NONE) {
    if (jobs_to_run.empty()) {
      finish_scheduling_cycle(available_nodes, running_jobs, current_time);
    } else {
      complete_candidate_scan();
    }
    return jobs_to_run;
  }

  m_fcfs_reservation_time =
      fcfs_head_reservation_time(m_wait_queue.front(), available_nodes,
                                 effective_running_jobs, current_time);

  // Scanning only new arrivals is unsafe with a queue depth: a start moves
  // older, never-examined jobs into the window. The depth bounds the full
  // scan, so it stays cheap.
  const size_t candidate_begin =
      (m_reevaluate_all_candidates || m_backfill_queue_depth != 0)
          ? 1
          : m_newly_eligible_begin_idx;
  const auto candidates = find_backfill_candidates_from(
      candidate_begin, available_nodes, current_time, m_fcfs_reservation_time,
      running_jobs, projected_starts);
  if (candidates.empty()) {
    if (jobs_to_run.empty()) {
      finish_scheduling_cycle(available_nodes, running_jobs, current_time);
    } else {
      complete_candidate_scan();
    }
    return jobs_to_run;
  }

  if (m_candidate_preparation_pending) {
    on_backfill_candidates_ready(candidates, available_nodes,
                                 effective_running_jobs, current_time,
                                 !jobs_to_run.empty());
    m_candidate_preparation_pending = false;
  }

  const std::optional<size_t> selected = select_backfill_candidate(
      candidates, available_nodes, effective_running_jobs, current_time);
  if (!selected) {
    if (jobs_to_run.empty()) {
      finish_scheduling_cycle(available_nodes, effective_running_jobs,
                              current_time);
    } else {
      complete_candidate_scan();
    }
    return jobs_to_run;
  }

  if (*selected >= candidates.size()) {
    throw std::invalid_argument(
        "backfill selector returned an invalid candidate index");
  }

  const size_t queue_index = candidates[*selected].queue_index;
  if (queue_index >= m_eligible_end_idx || m_wait_queue[queue_index].removed) {
    throw std::logic_error(
        "backfill candidate no longer refers to a waiting job");
  }
  auto &selected_job = m_wait_queue[queue_index];

#ifndef DR_EVT_LEGACY_RESERVATION_BARRIER
  const bool head_within_horizon =
      reservation_within_horizon(m_fcfs_reservation_time, current_time);
  auto reservation_plan = reservation_timeline(available_nodes, running_jobs,
                                               current_time, projected_starts);
  if (head_within_horizon) {
    const auto &head = m_wait_queue.front();
    reservation_plan.reserve(
        m_fcfs_reservation_time, head.nodes_requested,
        release_estimate(m_fcfs_reservation_time, head.run_time_estimate));
  }
  if (head_within_horizon &&
      !reservation_plan.can_fit(
          current_time, selected_job.nodes_requested,
          release_estimate(current_time, selected_job.run_time_estimate))) {
    if (jobs_to_run.empty()) {
      finish_scheduling_cycle(available_nodes, effective_running_jobs,
                              current_time);
    } else {
      complete_candidate_scan();
    }
    return jobs_to_run;
  }
#endif

  selected_job.removed = true;
  ++m_removed_count;
  jobs_to_run.push_back(selected_job.job_id);
  return jobs_to_run;
}

} // namespace dr_evt
