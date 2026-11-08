/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_periodic_hybrid_circular.hpp
 * @brief Event-driven FCFS dispatch and periodic hybrid/conservative backfill
 * using a circular queue.
 */

#ifndef DR_EVT_SIM_SCHEDULER_FCFS_PERIODIC_HYBRID_CIRCULAR_HPP
#define DR_EVT_SIM_SCHEDULER_FCFS_PERIODIC_HYBRID_CIRCULAR_HPP

#include "sim/scheduler_fcfs_hybrid_circular.hpp"

#include <limits>

namespace dr_evt {

/**
 * @brief Event-driven FCFS dispatch and periodic hybrid/conservative backfill
 * using a circular queue.
 * @details State events run FCFS through schedule_event(); timer ticks run
 * backfill through schedule(). Both admission and protected-reservation
 * searches include capacity changes at the requested interval's endpoint,
 * matching Slurm. Capacity reservations remain half-open intervals.
 * The simulation driver controls these wakes.
 * @tparam ProtectReservations Preserve future capacity reservations when true.
 * False selects the diagnostic license-off model: consider later jobs when
 * an earlier job cannot fit now, without reserving future capacity.
 * @note Selected by --backfill_interval I (I > 0), --dispatch_interval 0,
 * --queue_impl circular, and FCFS ordering, with --backfill_policy hybrid
 * --reservation_depth N (N > 0), or --backfill_policy conservative.
 */
template <bool ProtectReservations>
class FCFSPeriodicHybridCircularSchedulerImpl final
    : public FCFSHybridCircularScheduler {
public:
  /**
   * @brief Run protected periodic backfill or a diagnostic current-capacity
   * pass.
   * @details Admission and future reservations share the same endpoint rule.
   * Repeated protected calls at one tick share the examination budget.
   * The diagnostic specialization scans all eligible jobs without a future
   * plan.
   * @param[in] free_nodes Nodes currently available for allocation.
   * @param[in] running_jobs Jobs already running at this instant.
   * @param[in] current_time Current backfill tick time.
   * @return Jobs selected to start at this tick.
   */
  std::vector<job_no_t> schedule(num_nodes_t free_nodes,
                                 const running_jobs_t &running_jobs,
                                 sim_time_t current_time) override;

  /**
   * @brief Construct the opt-in periodic backfill scheduler.
   * @param[in] total_nodes Cluster capacity available for allocations.
   * @param[in] initial_job_count Initially known jobs, used when capacity is
   * zero.
   * @param[in] initial_capacity Initial queue capacity; zero uses job count.
   * @param[in] overflow_policy Action when the queue is full.
   * @param[in] backfill_policy Conservative or hybrid policy.
   * @param[in] reservation_depth Waiting jobs protected by reservations.
   * @param[in] backfill_queue_depth Eligible jobs examined per backfill cycle,
   * that is per tick; zero is unlimited.
   */
  FCFSPeriodicHybridCircularSchedulerImpl(
      num_nodes_t total_nodes, size_t initial_job_count,
      size_t initial_capacity, CircularOverflowPolicy overflow_policy,
      BackfillPolicy backfill_policy, size_t reservation_depth,
      size_t backfill_queue_depth)
      : FCFSHybridCircularScheduler(
            total_nodes, initial_job_count, backfill_policy, initial_capacity,
            overflow_policy, reservation_depth, backfill_queue_depth) {}

  /**
   * @brief Perform event dispatch, stopping at the head only when protected.
   * @param[in] free_nodes Nodes currently available for allocation.
   * @param[in] running_jobs Jobs already running at this instant.
   * @param[in] current_time Current simulation time.
   * @return Jobs selected in queue order using currently available capacity.
   */
  std::vector<job_no_t> schedule_event(num_nodes_t free_nodes,
                                       const running_jobs_t &running_jobs,
                                       sim_time_t current_time) override;

protected:
  /**
   * @brief Count out-of-horizon jobs against the queue depth, as Slurm does.
   * @details One tick is one backfill cycle. Slurm increments its tested-job
   * count before it learns that a job's planned start lies beyond bf_window,
   * so such a job uses a slot, though it receives no reservation. This
   * replaces the event-driven rule inherited from the parent.
   * @return Always true.
   */
  bool horizon_skipped_job_uses_depth_slot() const override { return true; }
};

/// Production comparison model with protected future capacity.
using FCFSPeriodicHybridCircularScheduler =
    FCFSPeriodicHybridCircularSchedulerImpl<true>;
/// Diagnostic license-off model with no future capacity reservations.
using FCFSPeriodicNoReservationCircularScheduler =
    FCFSPeriodicHybridCircularSchedulerImpl<false>;

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_FCFS_PERIODIC_HYBRID_CIRCULAR_HPP
