/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_JOB_SUBMIT_COMMON_HPP
#define DR_EVT_SIM_JOB_SUBMIT_COMMON_HPP
#include "common.hpp"
#include <array>
#include <vector>

namespace dr_evt {
/** \addtogroup dr_evt_sim
 *  @{ */

/// Per-week submission counts for one hour-of-week slot.
using submit_hour_t = std::vector<num_jobs_t>;

/// Samples for all 168 hour-of-week slots, indexed by day and hour.
using submit_week_t = typename std::array<submit_hour_t, 7 * 24>;

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_SIM_JOB_SUBMIT_COMMON_HPP
