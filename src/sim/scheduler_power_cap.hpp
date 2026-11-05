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

  /**
   * @brief Exempt warm-start initialization jobs from the aggregate power caps.
   * @details Jobs submitted at or before @p submit_time represent work that
   * was already running when the simulation window opened. They must start
   * regardless of the FCFS and backfill caps so the initial machine state
   * matches history; the caps then govern only later arrivals. Per-job
   * admission limits applied at trace load are unaffected. The default
   * (negative infinity) exempts nothing.
   * @param[in] submit_time Latest submit time treated as an initialization job.
   */
  void set_uncapped_submit_time(sim_time_t submit_time) {
    m_uncapped_submit_time = submit_time;
  }
  sim_time_t uncapped_submit_time() const { return m_uncapped_submit_time; }

protected:
  PowerCappedFCFSScheduler(
      num_nodes_t total_nodes, size_t initial_job_count,
      size_t num_max_candidates, double maximum_power,
      job_power_function_t power_function, bool cap_backfill_power,
      bool cap_fcfs_power, size_t initial_capacity = 0,
      CircularOverflowPolicy overflow_policy = CircularOverflowPolicy::GROW);

  virtual std::optional<double>
  power_from_metadata(const SchedulerJobMetadata &metadata) const;

  /**
   * @brief Sum the power metadata of the effective running set.
   * @param[in] running_jobs Running jobs with mandatory power metadata.
   * @return Aggregate predicted power of all running jobs.
   * @throws std::logic_error when a running job has no power metadata.
   */
  double current_power(const running_jobs_t &running_jobs) const;

  bool cap_backfill_power() const { return m_cap_backfill_power; }

  /** Whether a queued job is an initialization job exempt from the caps. */
  bool is_uncapped_job(const JobEntry &job) const {
    return job.submit_time <= m_uncapped_submit_time;
  }

  /**
   * @brief Return the power metadata for a queued job being started.
   * @param[in] job Queue entry selected for dispatch.
   * @return Predicted power retained in the effective running record.
   */
  std::optional<double> running_job_power(const JobEntry &job) const override;

  /**
   * @brief Check whether starting the FCFS head would exceed the power cap.
   * @param[in] job FCFS head queue entry.
   * @param[in] available_nodes Nodes currently available for dispatch.
   * @param[in] effective_running_jobs Running jobs and selected starts.
   * @param[in] current_time Scheduler timestamp.
   * @return true if the job is exempt, uncapped, or within the power cap.
   */
  bool can_start_fcfs_job(const JobEntry &job, num_nodes_t available_nodes,
                          const running_jobs_t &effective_running_jobs,
                          sim_time_t current_time) const override;

  /**
   * @brief Find the first resource- and power-feasible FCFS start time.
   * @param[in] job Blocked FCFS head queue entry.
   * @param[in] available_nodes Nodes currently available for dispatch.
   * @param[in] effective_running_jobs Running jobs and selected starts.
   * @param[in] current_time Scheduler timestamp.
   * @return Earliest feasible start, or infinity if releases never satisfy
   *         both the resource and power constraints.
   */
  sim_time_t
  fcfs_head_reservation_time(const JobEntry &job, num_nodes_t available_nodes,
                             const running_jobs_t &effective_running_jobs,
                             sim_time_t current_time) override;

private:
  double m_maximum_power;
  job_power_function_t m_power_function;
  bool m_cap_backfill_power;
  bool m_cap_fcfs_power;
  sim_time_t m_uncapped_submit_time;
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_POWER_CAP_HPP
