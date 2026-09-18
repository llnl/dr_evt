/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULER_EASY_POWER_HPP
#define DR_EVT_SIM_SCHEDULER_EASY_POWER_HPP

#include "sim/scheduler_fcfs_custom.hpp"
#include <functional>
#include <unordered_map>

namespace dr_evt {

/** Job projection supplied to the EASYPower forward replay. */
struct EASYPowerJob {
  job_no_t job_id;
  tdiff_t remaining_time;
  num_nodes_t nodes;
  double predicted_power;
};

/** Horizon and running-set energy returned by an EASYPower forward replay. */
struct EASYPowerReplayResult {
  tdiff_t horizon;
  double running_energy;
};

using job_power_function_t =
    std::function<double(job_no_t, sim_time_t, tdiff_t, num_nodes_t)>;
using easypower_cost_function_t =
    std::function<double(double, sim_time_t, double)>;
using easypower_forward_replay_t = std::function<EASYPowerReplayResult(
    const std::vector<EASYPowerJob> &, const std::vector<EASYPowerJob> &,
    double, num_nodes_t)>;

/**
 * @brief Power-aware EASY scheduler built on CustomFCFSScheduler.
 *
 * The inherited scheduler maintains the standard EASY head reservation and
 * exposes at most N feasible jobs per decision. This class scores those jobs
 * using C(P_current + predicted_job_power, time, target) and dispatches the
 * minimum-cost candidate. Arrival batches refresh the target before FCFS
 * dispatch. If that dispatch changes the state, the target is refreshed again
 * when the first backfill candidate set is ready; later backfills at the same
 * event reuse it.
 */
class EASYPowerScheduler : public CustomFCFSScheduler {
public:
  EASYPowerScheduler(
      num_nodes_t total_nodes, size_t initial_job_count,
      size_t num_max_candidates, double maximum_power, double initial_target,
      job_power_function_t power_function,
      easypower_cost_function_t cost_function,
      easypower_forward_replay_t forward_replay, size_t initial_capacity = 0,
      CircularOverflowPolicy overflow_policy = CircularOverflowPolicy::GROW);

  void insert_job(job_no_t job_id, sim_time_t submit_time,
                  tdiff_t run_time_estimate,
                  num_nodes_t nodes_requested) override;

  double power_target() const { return m_power_target; }
  double maximum_power() const { return m_maximum_power; }

protected:
  std::optional<job_no_t>
  select_backfill_candidate(const backfill_candidates_t &candidates,
                            num_nodes_t available_nodes,
                            const running_jobs_t &effective_running_jobs,
                            sim_time_t current_time) override;

  void on_jobs_became_eligible(size_t newly_eligible_begin, size_t eligible_end,
                               num_nodes_t available_nodes,
                               const running_jobs_t &running_jobs,
                               sim_time_t current_time) override;

  void on_backfill_candidates_ready(
      const backfill_candidates_t &candidates, num_nodes_t available_nodes,
      const running_jobs_t &effective_running_jobs, sim_time_t current_time,
      bool fcfs_jobs_started) override;

  void on_scheduling_cycle_complete(num_nodes_t available_nodes,
                                    const running_jobs_t &running_jobs,
                                    sim_time_t current_time) override;

private:
  void refresh_power_target(const running_jobs_t &running_jobs,
                            sim_time_t current_time);

  std::vector<EASYPowerJob>
  running_power_jobs(const running_jobs_t &running_jobs,
                     sim_time_t current_time) const;

  double m_maximum_power;
  double m_power_target;
  job_power_function_t m_power_function;
  easypower_cost_function_t m_cost_function;
  easypower_forward_replay_t m_forward_replay;
  std::unordered_map<job_no_t, double> m_predicted_power;
  bool m_target_refreshed_for_arrivals;
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_EASY_POWER_HPP
