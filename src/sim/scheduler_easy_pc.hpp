/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULER_EASY_PC_HPP
#define DR_EVT_SIM_SCHEDULER_EASY_PC_HPP

#include "sim/scheduler_power_cap.hpp"

namespace dr_evt {

enum class EASYPCPowerMode { MEAN, MAXIMUM };

/** Power-capped EASY using FCFS ordering and no power-target ranking. */
class EASYPCScheduler final : public PowerCappedFCFSScheduler {
public:
  EASYPCScheduler(
      num_nodes_t total_nodes, size_t initial_job_count,
      size_t num_max_candidates, double maximum_power, EASYPCPowerMode mode,
      job_power_function_t power_function = {}, size_t initial_capacity = 0,
      CircularOverflowPolicy overflow_policy = CircularOverflowPolicy::GROW);

  EASYPCPowerMode power_mode() const { return m_power_mode; }

  std::optional<double> maximum_job_power_for_admission() const override;
  std::optional<double>
  maximum_average_job_power_for_admission() const override;
  std::optional<double>
  scheduling_power(std::optional<double> average_power,
                   std::optional<double> maximum_power) const override;

protected:
  std::optional<double>
  power_from_metadata(const SchedulerJobMetadata &metadata) const override;

  std::optional<size_t>
  select_backfill_candidate(const backfill_candidates_t &candidates,
                            num_nodes_t available_nodes,
                            const running_jobs_t &effective_running_jobs,
                            sim_time_t current_time) override;

private:
  EASYPCPowerMode m_power_mode;
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_EASY_PC_HPP
