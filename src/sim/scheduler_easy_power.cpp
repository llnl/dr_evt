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
#include <map>
#include <stdexcept>
#include <utility>

namespace dr_evt {

EASYPowerScheduler::EASYPowerScheduler(
    num_nodes_t total_nodes, size_t initial_job_count,
    size_t num_max_candidates, double maximum_power, double initial_target,
    double target_weight, double maximum_weight, double dominant_cost,
    job_power_function_t power_function,
    easypower_forward_replay_t forward_replay, size_t initial_capacity,
    CircularOverflowPolicy overflow_policy, bool cap_backfill_power,
    bool cap_fcfs_power)
    : FCFSEasyCustomScheduler(total_nodes, initial_job_count,
                              BackfillPolicy::EASY, num_max_candidates,
                              initial_capacity, overflow_policy),
      m_maximum_power(maximum_power), m_power_target(initial_target),
      m_target_weight(target_weight), m_maximum_weight(maximum_weight),
      m_dominant_cost(dominant_cost),
      m_power_function(std::move(power_function)),
      m_forward_replay(std::move(forward_replay)),
      m_target_refreshed_for_arrivals(false),
      m_cap_backfill_power(cap_backfill_power),
      m_cap_fcfs_power(cap_fcfs_power) {
  if (!std::isfinite(m_maximum_power) || m_maximum_power < 0.0) {
    throw std::invalid_argument(
        "EASYPowerScheduler maximum power must be finite and nonnegative");
  }
  if (!std::isfinite(m_power_target) || m_power_target < 0.0 ||
      m_power_target > m_maximum_power) {
    throw std::invalid_argument(
        "EASYPowerScheduler initial target must be finite and in [0, Pmax]");
  }
  const double largest_in_limit_cost =
      m_target_weight * m_maximum_power * m_maximum_power;
  if (!std::isfinite(m_target_weight) || m_target_weight <= 0.0 ||
      !std::isfinite(m_maximum_weight) || m_maximum_weight <= 0.0 ||
      !std::isfinite(m_dominant_cost) ||
      m_dominant_cost < largest_in_limit_cost) {
    throw std::invalid_argument(
        "EASYPowerScheduler requires positive finite weights and a finite "
        "dominant cost >= target_weight * Pmax^2");
  }
  if (!m_power_function || !m_forward_replay) {
    throw std::invalid_argument(
        "EASYPowerScheduler requires power and forward-replay functions");
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
  bool best_exceeds_maximum = false;
  for (const auto &[job_id, ignored_cost] : candidates) {
    (void)ignored_cost;
    const auto power = m_predicted_power.find(job_id);
    if (power == m_predicted_power.end()) {
      throw std::logic_error(
          "EASYPowerScheduler has no power estimate for a candidate");
    }
    const double projected_power = current_power + power->second;
    const bool exceeds_maximum = projected_power > m_maximum_power;
    if (m_cap_backfill_power && exceeds_maximum) {
      continue;
    }
    const double cost = candidate_power_cost(projected_power);
    // Preserve the mathematical dominance of the second branch even when
    // floating-point rounding makes C_dom + a tiny overshoot equal C_dom.
    if (!selected || (best_exceeds_maximum && !exceeds_maximum) ||
        (best_exceeds_maximum == exceeds_maximum && cost < best_cost)) {
      selected = job_id;
      best_cost = cost;
      best_exceeds_maximum = exceeds_maximum;
    }
  }
  return selected;
}

bool EASYPowerScheduler::can_start_fcfs_job(
    job_no_t job_id, num_nodes_t available_nodes,
    const running_jobs_t &effective_running_jobs,
    sim_time_t current_time) const {
  (void)available_nodes;
  if (!m_cap_fcfs_power) {
    return true;
  }
  const auto head_power = m_predicted_power.find(job_id);
  if (head_power == m_predicted_power.end()) {
    throw std::logic_error(
        "EASYPowerScheduler has no power estimate for the FCFS head");
  }
  double projected_power = head_power->second;
  for (const auto &job :
       running_power_jobs(effective_running_jobs, current_time)) {
    projected_power += job.predicted_power;
  }
  return projected_power <= m_maximum_power;
}

sim_time_t EASYPowerScheduler::fcfs_head_reservation_time(
    job_no_t job_id, num_nodes_t nodes_requested, num_nodes_t available_nodes,
    const running_jobs_t &effective_running_jobs, sim_time_t current_time) {
  if (!m_cap_fcfs_power) {
    return FCFSEasyCustomScheduler::fcfs_head_reservation_time(
        job_id, nodes_requested, available_nodes, effective_running_jobs,
        current_time);
  }

  const auto head_power = m_predicted_power.find(job_id);
  if (head_power == m_predicted_power.end()) {
    throw std::logic_error(
        "EASYPowerScheduler has no power estimate for the FCFS head");
  }

  num_nodes_t projected_available = available_nodes;
  double projected_power = 0.0;
  struct Release {
    num_nodes_t nodes = 0;
    double power = 0.0;
  };
  std::map<sim_time_t, Release> releases;
  for (const auto &[running_id, running] : effective_running_jobs) {
    const auto power = m_predicted_power.find(running_id);
    if (power == m_predicted_power.end()) {
      throw std::logic_error(
          "EASYPowerScheduler has no power estimate for a running job");
    }
    projected_power += power->second;
    const sim_time_t end_time = running.start_time + running.run_time;
    if (end_time > current_time) {
      releases[end_time].nodes += running.nodes;
      releases[end_time].power += power->second;
    }
  }
  if (nodes_requested <= projected_available &&
      projected_power + head_power->second <= m_maximum_power) {
    return current_time;
  }
  for (const auto &[end_time, release] : releases) {
    projected_available += release.nodes;
    projected_power = std::max(0.0, projected_power - release.power);
    if (nodes_requested <= projected_available &&
        projected_power + head_power->second <= m_maximum_power) {
      return end_time;
    }
  }
  return std::numeric_limits<sim_time_t>::infinity();
}

double EASYPowerScheduler::candidate_power_cost(double projected_power) const {
  if (!std::isfinite(projected_power) || projected_power < 0.0) {
    throw std::invalid_argument(
        "EASYPowerScheduler projected power must be finite and nonnegative");
  }
  const double target_error = projected_power - m_power_target;
  const double maximum_error = projected_power - m_maximum_power;
  const double cost =
      projected_power <= m_maximum_power
          ? m_target_weight * target_error * target_error
          : m_dominant_cost + m_maximum_weight * maximum_error * maximum_error;
  if (!std::isfinite(cost)) {
    throw std::invalid_argument(
        "EASYPowerScheduler candidate cost must be finite");
  }
  return cost;
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
