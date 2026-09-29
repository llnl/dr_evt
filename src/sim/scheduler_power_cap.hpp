/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULER_POWER_CAP_HPP
#define DR_EVT_SIM_SCHEDULER_POWER_CAP_HPP

#include "sim/scheduler_fcfs_easy_custom.hpp"
#include <functional>

namespace dr_evt {

using job_power_function_t =
    std::function<double(job_no_t, sim_time_t, tdiff_t, num_nodes_t)>;

/** Shared aggregate-power admission and reservation behavior for EASY. */
class PowerCappedFCFSScheduler : public FCFSEasyCustomScheduler {
public:
  double maximum_power() const { return m_maximum_power; }
  std::optional<double> maximum_job_power_for_admission() const override {
    return m_maximum_power;
  }

  void insert_job(job_no_t job_id, sim_time_t submit_time,
                  tdiff_t run_time_estimate,
                  num_nodes_t nodes_requested) override;

  void insert_job_with_metadata(job_no_t job_id, sim_time_t submit_time,
                                tdiff_t run_time_estimate,
                                num_nodes_t nodes_requested,
                                const SchedulerJobMetadata &metadata) override;

protected:
  PowerCappedFCFSScheduler(
      num_nodes_t total_nodes, size_t initial_job_count,
      size_t num_max_candidates, double maximum_power,
      job_power_function_t power_function, bool cap_backfill_power,
      bool cap_fcfs_power, size_t initial_capacity = 0,
      CircularOverflowPolicy overflow_policy = CircularOverflowPolicy::GROW);

  virtual std::optional<double>
  power_from_metadata(const SchedulerJobMetadata &metadata) const;

  double current_power(const running_jobs_t &running_jobs) const;

  bool cap_backfill_power() const { return m_cap_backfill_power; }

  std::optional<double> running_job_power(const JobEntry &job) const override;

  bool can_start_fcfs_job(const JobEntry &job, num_nodes_t available_nodes,
                          const running_jobs_t &effective_running_jobs,
                          sim_time_t current_time) const override;

  sim_time_t
  fcfs_head_reservation_time(const JobEntry &job, num_nodes_t available_nodes,
                             const running_jobs_t &effective_running_jobs,
                             sim_time_t current_time) override;

private:
  double m_maximum_power;
  job_power_function_t m_power_function;
  bool m_cap_backfill_power;
  bool m_cap_fcfs_power;
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_POWER_CAP_HPP
