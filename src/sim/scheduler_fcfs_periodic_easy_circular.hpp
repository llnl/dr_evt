/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_periodic_easy_circular.hpp
 * @brief Opt-in circular EASY scheduler for periodic backfill evaluation.
 */

#ifndef DR_EVT_SIM_SCHEDULER_FCFS_PERIODIC_EASY_CIRCULAR_HPP
#define DR_EVT_SIM_SCHEDULER_FCFS_PERIODIC_EASY_CIRCULAR_HPP

#include "sim/scheduler_fcfs_easy_circular.hpp"

#include <limits>

namespace dr_evt {

/**
 * @brief Circular EASY scheduler whose backfill scans run only at periodic
 * ticks.
 * @details Normal simulation events retain immediate FCFS dispatch through
 * schedule_event(); schedule() performs the usual EASY backfill scan when the
 * simulation reaches a configured tick.
 */
class FCFSPeriodicEasyCircularScheduler final
    : public FCFSEasyCircularScheduler {
  sim_time_t m_backfill_evaluation_time =
      std::numeric_limits<sim_time_t>::lowest();

public:
  /**
   * @brief Construct a periodic circular EASY scheduler.
   * @param[in] total_nodes Cluster capacity available for allocations.
   * @param[in] initial_job_count Initially known jobs, used when capacity is
   * zero.
   * @param[in] initial_capacity Initial queue capacity; zero uses job count.
   * @param[in] overflow_policy Action when the queue is full.
   * @param[in] backfill_queue_depth Eligible jobs examined per periodic
   * backfill cycle; zero is unlimited.
   */
  FCFSPeriodicEasyCircularScheduler(
      num_nodes_t total_nodes, size_t initial_job_count,
      size_t initial_capacity, CircularOverflowPolicy overflow_policy,
      size_t backfill_queue_depth)
      : FCFSEasyCircularScheduler(total_nodes, initial_job_count,
                                  BackfillPolicy::EASY, initial_capacity,
                                  overflow_policy, backfill_queue_depth) {}

  /**
   * @brief Run at most one EASY backfill scan at a periodic tick.
   * @details Repeated calls at the same tick use schedule_event() so the
   * queue-depth budget remains one backfill cycle while FCFS dispatch can
   * still consume newly feasible head jobs.
   * @param[in] free_nodes Nodes currently available for allocation.
   * @param[in] running_jobs Jobs already running at this instant.
   * @param[in] current_time Current simulation time.
   * @return Job IDs selected by the periodic scheduling pass.
   */
  std::vector<job_no_t> schedule(num_nodes_t free_nodes,
                                 const running_jobs_t &running_jobs,
                                 sim_time_t current_time) override;

  /** @copydoc SchedulerBase::schedule_event */
  std::vector<job_no_t>
  schedule_event(num_nodes_t free_nodes, const running_jobs_t &running_jobs,
                 sim_time_t current_time) override;
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_FCFS_PERIODIC_EASY_CIRCULAR_HPP
