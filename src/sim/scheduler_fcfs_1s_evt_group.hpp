/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_1s_evt_group.hpp
 * @brief FCFS backfill using one-second future-event grouping.
 * @details Check the current time first. In the hypothetical forecast, group
 * events through one second after the earliest remaining event, including
 * the endpoint; check fit at the last event actually processed. A lone
 * release at 10 is checked at 10; releases at 10 and 11 are checked together
 * at 11. Actual arrivals and completions retain their times. This does not
 * model native node placement.
 * @note CLI: --priority_policy fcfs_1s_evt_group --queue_impl circular or
 * deque, with --dispatch_interval 0 and --backfill_interval 0. Choose
 * --backfill_policy easy, hybrid, conservative, or none; hybrid requires
 * --reservation_depth R > 0. Omit --planning_horizon for unlimited look-ahead.
 * --per_pass_accounting is not required. --diagnostic_no_reservations cannot
 * be combined with this policy.
 * Protobuf: Simulation_Params and InitRequest use the same field names
 * without the leading --; see dr_evt_params.proto and dr_evt_service.proto.
 */

#ifndef DR_EVT_SIM_SCHEDULER_FCFS_1S_EVT_GROUP_HPP
#define DR_EVT_SIM_SCHEDULER_FCFS_1S_EVT_GROUP_HPP

#include "sim/scheduler_fcfs_pass_hybrid.hpp"

namespace dr_evt {

/**
 * @brief Event-driven FCFS backfill with OpenPBS 23.06 forecast timing.
 * @tparam Queue Wait-queue storage policy: CircularWaitQueue or
 * DequeWaitQueue.
 * @details Selected by --priority_policy fcfs_1s_evt_group with a circular or
 * deque queue and event-driven dispatch. Reuses placement passes but checks
 * future fits after inclusive one-second groups of predicted events. Actual
 * arrivals, completions, and requested reservation intervals are unchanged.
 * Native comparisons use integer-second inputs and unlimited candidate
 * examination.
 */
template <typename Queue>
class FCFSOneSecondEventGroupScheduler final
    : public FCFSPassHybridScheduler<Queue> {
public:
  /** @brief Construct using the shared FCFS placement-pass parameters. */
  using FCFSPassHybridScheduler<Queue>::FCFSPassHybridScheduler;

  /** @copydoc SchedulerBase::schedule */
  std::vector<job_no_t> schedule(num_nodes_t free_nodes,
                                 const running_jobs_t &running_jobs,
                                 sim_time_t current_time) override;
};

/** @brief One-second event-group scheduler over a circular queue. */
using FCFSOneSecondEventGroupCircularScheduler =
    FCFSOneSecondEventGroupScheduler<CircularWaitQueue>;
/** @brief One-second event-group scheduler over a deque queue. */
using FCFSOneSecondEventGroupDequeScheduler =
    FCFSOneSecondEventGroupScheduler<DequeWaitQueue>;

// Defined in scheduler_fcfs_pass_hybrid.cpp for both queues.
extern template class FCFSOneSecondEventGroupScheduler<CircularWaitQueue>;
extern template class FCFSOneSecondEventGroupScheduler<DequeWaitQueue>;

} // namespace dr_evt
#endif
