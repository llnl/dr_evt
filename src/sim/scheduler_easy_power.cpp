/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include "sim/scheduler_easy_power.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dr_evt {

EASYPowerScheduler::EASYPowerScheduler(
    num_nodes_t total_nodes, size_t initial_job_count,
    size_t num_max_candidates, double maximum_power, double initial_target,
    job_power_function_t power_function,
    easypower_cost_function_t cost_function,
    easypower_forward_replay_t forward_replay, size_t initial_capacity,
    CircularOverflowPolicy overflow_policy)
    : CustomFCFSScheduler(total_nodes, initial_job_count, BackfillPolicy::EASY,
                          num_max_candidates, initial_capacity,
                          overflow_policy),
      m_maximum_power(maximum_power), m_power_target(initial_target),
      m_power_function(std::move(power_function)),
      m_cost_function(std::move(cost_function)),
      m_forward_replay(std::move(forward_replay)),
      m_target_refreshed_for_arrivals(false) {
  if (!std::isfinite(m_maximum_power) || m_maximum_power < 0.0) {
    throw std::invalid_argument(
        "EASYPowerScheduler maximum power must be finite and nonnegative");
  }
  if (!std::isfinite(m_power_target) || m_power_target < 0.0 ||
      m_power_target > m_maximum_power) {
    throw std::invalid_argument(
        "EASYPowerScheduler initial target must be finite and in [0, Pmax]");
  }
  if (!m_power_function || !m_cost_function || !m_forward_replay) {
    throw std::invalid_argument(
        "EASYPowerScheduler requires power, cost, and forward-replay "
        "functions");
  }
}

void EASYPowerScheduler::insert_job(job_no_t job_id, sim_time_t submit_time,
                                    tdiff_t run_time_estimate,
                                    num_nodes_t nodes_requested) {
  const double predicted_power =
      m_power_function(job_id, submit_time, run_time_estimate, nodes_requested);
  if (!std::isfinite(predicted_power) || predicted_power < 0.0) {
    throw std::invalid_argument(
        "EASYPowerScheduler predicted job power must be finite and "
        "nonnegative");
  }
  insert_job_with_cost(job_id, submit_time, run_time_estimate, nodes_requested,
                       0);
  m_predicted_power[job_id] = predicted_power;
}

std::vector<EASYPowerJob>
EASYPowerScheduler::running_power_jobs(const running_jobs_t &running_jobs,
                                       sim_time_t current_time) const {
  std::vector<EASYPowerJob> result;
  result.reserve(running_jobs.size());
  for (const auto &[job_id, running] : running_jobs) {
    const auto power = m_predicted_power.find(job_id);
    if (power == m_predicted_power.end()) {
      throw std::logic_error(
          "EASYPowerScheduler has no power estimate for a running job");
    }
    const tdiff_t remaining = std::max<tdiff_t>(
        0.0, running.start_time + running.run_time - current_time);
    result.push_back({job_id, remaining, running.nodes, power->second});
  }
  return result;
}

std::optional<job_no_t> EASYPowerScheduler::select_backfill_candidate(
    const backfill_candidates_t &candidates, num_nodes_t available_nodes,
    const running_jobs_t &effective_running_jobs, sim_time_t current_time) {
  (void)available_nodes;
  double current_power = 0.0;
  for (const auto &job :
       running_power_jobs(effective_running_jobs, current_time)) {
    current_power += job.predicted_power;
  }

  std::optional<job_no_t> selected;
  double best_cost = std::numeric_limits<double>::infinity();
  for (const auto &[job_id, ignored_cost] : candidates) {
    (void)ignored_cost;
    const auto power = m_predicted_power.find(job_id);
    if (power == m_predicted_power.end()) {
      throw std::logic_error(
          "EASYPowerScheduler has no power estimate for a candidate");
    }
    const double cost = m_cost_function(current_power + power->second,
                                        current_time, m_power_target);
    if (!std::isfinite(cost)) {
      throw std::invalid_argument(
          "EASYPowerScheduler candidate cost must be finite");
    }
    if (!selected || cost < best_cost) {
      selected = job_id;
      best_cost = cost;
    }
  }
  return selected;
}

void EASYPowerScheduler::on_jobs_became_eligible(
    size_t newly_eligible_begin, size_t eligible_end,
    num_nodes_t available_nodes, const running_jobs_t &running_jobs,
    sim_time_t current_time) {
  (void)newly_eligible_begin;
  (void)eligible_end;
  (void)available_nodes;
  refresh_power_target(running_jobs, current_time);
  m_target_refreshed_for_arrivals = true;
}

void EASYPowerScheduler::on_backfill_candidates_ready(
    const backfill_candidates_t &candidates, num_nodes_t available_nodes,
    const running_jobs_t &effective_running_jobs, sim_time_t current_time,
    bool fcfs_jobs_started) {
  (void)candidates;
  (void)available_nodes;
  if (!m_target_refreshed_for_arrivals || fcfs_jobs_started) {
    refresh_power_target(effective_running_jobs, current_time);
  }
  m_target_refreshed_for_arrivals = false;
}

void EASYPowerScheduler::on_scheduling_cycle_complete(
    num_nodes_t available_nodes, const running_jobs_t &running_jobs,
    sim_time_t current_time) {
  (void)available_nodes;
  (void)running_jobs;
  (void)current_time;
  m_target_refreshed_for_arrivals = false;
}

void EASYPowerScheduler::refresh_power_target(
    const running_jobs_t &running_jobs, sim_time_t current_time) {
  const auto &queue = queued_jobs();
  std::vector<EASYPowerJob> waiting;
  waiting.reserve(active_job_count());
  double waiting_energy = 0.0;
  for (size_t index = 0; index < eligible_job_end(); ++index) {
    const auto &job = queue[index];
    if (job.removed) {
      continue;
    }
    const auto power = m_predicted_power.find(job.job_id);
    if (power == m_predicted_power.end()) {
      throw std::logic_error(
          "EASYPowerScheduler has no power estimate for a waiting job");
    }
    waiting.push_back({job.job_id, job.run_time_estimate, job.nodes_requested,
                       power->second});
    waiting_energy += power->second * job.run_time_estimate;
  }

  const auto running = running_power_jobs(running_jobs, current_time);
  const double utilization = utilization_through(current_time);
  const auto replay =
      m_forward_replay(waiting, running, utilization, m_total_nodes);
  if (!std::isfinite(replay.horizon) || replay.horizon < 0.0 ||
      !std::isfinite(replay.running_energy) || replay.running_energy < 0.0) {
    throw std::invalid_argument(
        "EASYPowerScheduler forward replay returned an invalid result");
  }
  if (replay.horizon == 0.0) {
    if (waiting_energy != 0.0 || replay.running_energy != 0.0) {
      throw std::invalid_argument(
          "EASYPowerScheduler forward replay returned a zero horizon for "
          "nonzero energy");
    }
    m_power_target = 0.0;
    return;
  }

  const double queue_pressure = waiting_energy / replay.horizon;
  const double average_running_power = replay.running_energy / replay.horizon;
  m_power_target =
      std::min(m_maximum_power, average_running_power + queue_pressure);
}

} // namespace dr_evt
