/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_openpbs_circular.hpp
 * @brief Opt-in FCFS scheduler reproducing OpenPBS future-event grouping.
 */

#ifndef DR_EVT_SIM_SCHEDULER_FCFS_OPENPBS_CIRCULAR_HPP
#define DR_EVT_SIM_SCHEDULER_FCFS_OPENPBS_CIRCULAR_HPP

#include "sim/scheduler_fcfs_pass_hybrid_circular.hpp"

namespace dr_evt {

/**
 * @brief Event-driven FCFS backfill with OpenPBS 23.06 forecast timing.
 * @details Selected by --priority_policy fcfs_openpbs with a circular queue
 * and event-driven dispatch. Reuses placement passes but checks future fits
 * after inclusive one-second groups of predicted events. Actual arrivals,
 * completions, and requested reservation intervals are unchanged. Native
 * comparisons use integer-second inputs and unlimited candidate examination.
 */
class FCFSOpenPBSCircularScheduler final
    : public FCFSPassHybridCircularScheduler {
public:
  /** @brief Construct using the shared FCFS placement-pass parameters. */
  using FCFSPassHybridCircularScheduler::FCFSPassHybridCircularScheduler;

  /** @copydoc SchedulerBase::schedule */
  std::vector<job_no_t> schedule(num_nodes_t free_nodes,
                               const running_jobs_t &running_jobs,
                               sim_time_t current_time) override;
};

} // namespace dr_evt
#endif
