/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include "sim/scheduler_power_cap.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace dr_evt {

PowerCappedFCFSScheduler::PowerCappedFCFSScheduler(
    num_nodes_t total_nodes, size_t initial_job_count,
    size_t num_max_candidates, double maximum_power,
    job_power_function_t power_function, bool cap_backfill_power,
    bool cap_fcfs_power, size_t initial_capacity,
    CircularOverflowPolicy overflow_policy)
    : FCFSEasyCustomScheduler(total_nodes, initial_job_count, BackfillPolicy::EASY,
                          num_max_candidates, initial_capacity,
                          overflow_policy),
      m_maximum_power(maximum_power),
      m_power_function(std::move(power_function)),
      m_cap_backfill_power(cap_backfill_power),
      m_cap_fcfs_power(cap_fcfs_power),
      m_uncapped_submit_time(-std::numeric_limits<sim_time_t>::infinity()) {
  if (!std::isfinite(m_maximum_power) || m_maximum_power < 0.0) {
    throw std::invalid_argument(
        "power-capped scheduler maximum power must be finite and "
        "nonnegative");
  }
}

void PowerCappedFCFSScheduler::insert_job(job_no_t job_id,
                                          sim_time_t submit_time,
                                          tdiff_t run_time_estimate,
                                          num_nodes_t nodes_requested) {
  if (!m_power_function) {
    throw std::invalid_argument(
        "power-capped scheduler requires inline power metadata or a power "
        "function");
  }
  const double predicted_power =
      m_power_function(job_id, submit_time, run_time_estimate, nodes_requested);
  if (!std::isfinite(predicted_power) || predicted_power < 0.0) {
    throw std::invalid_argument(
        "power-capped scheduler job power must be finite and nonnegative");
  }
  insert_job_with_cost(job_id, submit_time, run_time_estimate, nodes_requested,
                       predicted_power);
}

std::optional<double> PowerCappedFCFSScheduler::power_from_metadata(
    const SchedulerJobMetadata &metadata) const {
  return metadata.predicted_power;
}

void PowerCappedFCFSScheduler::insert_job_with_metadata(
    job_no_t job_id, sim_time_t submit_time, tdiff_t run_time_estimate,
    num_nodes_t nodes_requested, const SchedulerJobMetadata &metadata) {
  const auto power = power_from_metadata(metadata);
  if (!power) {
    insert_job(job_id, submit_time, run_time_estimate, nodes_requested);
    return;
  }
  if (!std::isfinite(*power) || *power < 0.0) {
    throw std::invalid_argument(
        "power-capped scheduler job power must be finite and nonnegative");
  }
  insert_job_with_cost(job_id, submit_time, run_time_estimate, nodes_requested,
                       *power);
}

double PowerCappedFCFSScheduler::current_power(
    const running_jobs_t &running_jobs) const {
  double power = 0.0;
  for (const auto &[job_id, running] : running_jobs) {
    (void)job_id;
    if (!running.predicted_power) {
      throw std::logic_error(
          "power-capped scheduler has no power estimate for a running job");
    }
    power += *running.predicted_power;
  }
  return power;
}

std::optional<double>
PowerCappedFCFSScheduler::running_job_power(const JobEntry &job) const {
  return job.m_cost;
}

bool PowerCappedFCFSScheduler::can_start_fcfs_job(
    const JobEntry &job, num_nodes_t available_nodes,
    const running_jobs_t &effective_running_jobs,
    sim_time_t current_time) const {
  (void)available_nodes;
  (void)current_time;
  return !m_cap_fcfs_power || is_uncapped_job(job) ||
         current_power(effective_running_jobs) + job.m_cost <= m_maximum_power;
}

sim_time_t PowerCappedFCFSScheduler::fcfs_head_reservation_time(
    const JobEntry &job, num_nodes_t available_nodes,
    const running_jobs_t &effective_running_jobs, sim_time_t current_time) {
  if (!m_cap_fcfs_power || is_uncapped_job(job)) {
    return FCFSEasyCustomScheduler::fcfs_head_reservation_time(
        job, available_nodes, effective_running_jobs, current_time);
  }

  num_nodes_t projected_available = available_nodes;
  double projected_power = 0.0;
  struct Release {
    num_nodes_t nodes = 0;
    double power = 0.0;
  };
  std::map<sim_time_t, Release> releases;
  for (const auto &[running_id, running] : effective_running_jobs) {
    (void)running_id;
    if (!running.predicted_power) {
      throw std::logic_error(
          "power-capped scheduler has no power estimate for a running job");
    }
    projected_power += *running.predicted_power;
    const sim_time_t end_time = running.start_time + running.run_time;
    if (end_time > current_time) {
      releases[end_time].nodes += running.nodes;
      releases[end_time].power += *running.predicted_power;
    }
  }
  if (job.nodes_requested <= projected_available &&
      projected_power + job.m_cost <= m_maximum_power) {
    return current_time;
  }
  for (const auto &[end_time, release] : releases) {
    projected_available += release.nodes;
    projected_power = std::max(0.0, projected_power - release.power);
    if (job.nodes_requested <= projected_available &&
        projected_power + job.m_cost <= m_maximum_power) {
      return end_time;
    }
  }
  return std::numeric_limits<sim_time_t>::infinity();
}

} // namespace dr_evt
