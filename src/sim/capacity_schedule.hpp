/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file capacity_schedule.hpp
 * @brief Scheduled changes to interchangeable cluster capacity.
 * @details Loads time,total_nodes changes used by the simulation driver and
 * future capacity forecasts. Capacity reductions do not preempt running
 * jobs. This is shared input support, not a selectable scheduler.
 * @note Enable with --capacity_schedule FILE and set the maximum with
 * --total_nodes N. Simulation_Params uses capacity_schedule and total_nodes;
 * InitRequest has no capacity_schedule field.
 */

#ifndef DR_EVT_SIM_CAPACITY_SCHEDULE_HPP
#define DR_EVT_SIM_CAPACITY_SCHEDULE_HPP

#include "dr_evt_types.hpp"
#include <string>
#include <vector>

namespace dr_evt {

/** One capacity change, effective at `time` until the next change. */
struct Capacity_Change {
  sim_time_t time;
  num_nodes_t total_nodes;
};

/**
 * Load a CSV containing `time,total_nodes` change points.
 *
 * The first data row selects epoch or calendar encoding for the whole file.
 * Times accept the same spellings as simple job traces. Rows must be strictly
 * increasing in time, and capacities may not exceed the configured maximum.
 * An empty filename returns an empty schedule.
 */
std::vector<Capacity_Change>
load_capacity_schedule(const std::string &filename,
                       num_nodes_t configured_maximum);

} // namespace dr_evt

#endif // DR_EVT_SIM_CAPACITY_SCHEDULE_HPP
