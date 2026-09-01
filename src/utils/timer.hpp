/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_UTILS_TIMER_HPP
#define DR_EVT_UTILS_TIMER_HPP

#include <chrono>

namespace dr_evt {
/** \addtogroup dr_evt_utils
 *  @{ */

inline double get_time() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(
           steady_clock::now().time_since_epoch()).count();
}

/**@}*/
} // namespace dr_evt

#endif  // DR_EVT_UTILS_TIMER_HPP
