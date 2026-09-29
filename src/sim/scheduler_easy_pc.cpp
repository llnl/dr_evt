/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include "sim/scheduler_easy_pc.hpp"
#include <utility>

namespace dr_evt {

EASYPCScheduler::EASYPCScheduler(num_nodes_t total_nodes,
                                 size_t initial_job_count,
                                 size_t num_max_candidates,
                                 double maximum_power, EASYPCPowerMode mode,
                                 job_power_function_t power_function,
                                 size_t initial_capacity,
                                 CircularOverflowPolicy overflow_policy)
    : PowerCappedFCFSScheduler(total_nodes, initial_job_count,
                               num_max_candidates, maximum_power,
                               std::move(power_function), true, true,
                               initial_capacity, overflow_policy),
      m_power_mode(mode) {}

std::optional<double> EASYPCScheduler::maximum_job_power_for_admission() const {
  return m_power_mode == EASYPCPowerMode::MAXIMUM
             ? std::optional<double>{maximum_power()}
             : std::nullopt;
}

std::optional<double>
EASYPCScheduler::maximum_average_job_power_for_admission() const {
  return m_power_mode == EASYPCPowerMode::MEAN
             ? std::optional<double>{maximum_power()}
             : std::nullopt;
}

std::optional<double> EASYPCScheduler::scheduling_power(
    std::optional<double> average_power,
    std::optional<double> maximum_power_value) const {
  return m_power_mode == EASYPCPowerMode::MEAN ? average_power
                                               : maximum_power_value;
}

std::optional<double> EASYPCScheduler::power_from_metadata(
    const SchedulerJobMetadata &metadata) const {
  return scheduling_power(metadata.predicted_power, metadata.maximum_power);
}

std::optional<size_t> EASYPCScheduler::select_backfill_candidate(
    const backfill_candidates_t &candidates, num_nodes_t available_nodes,
    const running_jobs_t &effective_running_jobs, sim_time_t current_time) {
  (void)available_nodes;
  (void)current_time;
  const double running_power = current_power(effective_running_jobs);
  for (size_t index = 0; index < candidates.size(); ++index) {
    if (running_power + candidates[index].cost <= maximum_power()) {
      return index;
    }
  }
  return std::nullopt;
}

} // namespace dr_evt
