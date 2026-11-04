/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_circular_fcfs_periodic_hybrid.hpp
 * @brief Opt-in circular hybrid scheduler for periodic backfill evaluation.
 */

#ifndef DR_EVT_SIM_SCHEDULER_CIRCULAR_FCFS_PERIODIC_HYBRID_HPP
#define DR_EVT_SIM_SCHEDULER_CIRCULAR_FCFS_PERIODIC_HYBRID_HPP

#include "sim/scheduler_circular_fcfs_conservative.hpp"

#include <limits>

namespace dr_evt {

/**
 * @brief Circular scheduler selected for periodic backfill runs.
 * @details The simulation driver supplies periodic evaluation times. This
 * class inherits the existing hybrid queue and reservation logic, leaving
 * ordinary scheduler hot paths unchanged.
 */
class CircularBufferFCFSPeriodicHybridScheduler final
    : public CircularBufferFCFSConservativeScheduler {
public:
  /**
   * @brief Construct the opt-in periodic backfill scheduler.
   * @param[in] total_nodes Cluster capacity available for allocations.
   * @param[in] initial_job_count Initially known jobs, used when capacity is
   * zero.
   * @param[in] initial_capacity Initial queue capacity; zero uses job count.
   * @param[in] overflow_policy Action when the queue is full.
   * @param[in] backfill_policy EASY, conservative, or hybrid policy.
   * @param[in] reservation_depth Waiting jobs protected by reservations.
   * @param[in] backfill_queue_depth Eligible jobs examined per backfill cycle,
   * that is per tick under every policy; zero is unlimited.
   */
  CircularBufferFCFSPeriodicHybridScheduler(
      num_nodes_t total_nodes, size_t initial_job_count,
      size_t initial_capacity, CircularOverflowPolicy overflow_policy,
      BackfillPolicy backfill_policy, size_t reservation_depth,
      size_t backfill_queue_depth)
      : CircularBufferFCFSConservativeScheduler(
            total_nodes, initial_job_count, backfill_policy, initial_capacity,
            overflow_policy, reservation_depth, backfill_queue_depth) {
    // One tick is one backfill cycle for every policy, EASY included.
    m_share_easy_depth_across_passes = true;
  }

  /**
   * @brief Perform an event-driven FCFS pass without backfill scanning.
   * @param[in] free_nodes Nodes currently available for allocation.
   * @param[in] running_jobs Jobs already running at this instant.
   * @param[in] current_time Current simulation time.
   * @return Jobs started from the FCFS front.
   */
  std::vector<job_no_t> schedule_event(num_nodes_t free_nodes,
                                       const running_jobs_t &running_jobs,
                                       sim_time_t current_time);
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_CIRCULAR_FCFS_PERIODIC_HYBRID_HPP
