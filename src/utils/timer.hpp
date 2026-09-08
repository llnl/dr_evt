/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file timer.hpp
 * @brief Monotonic elapsed-time helper for performance measurement.
 */

#ifndef DR_EVT_UTILS_TIMER_HPP
#define DR_EVT_UTILS_TIMER_HPP

#include <chrono>

namespace dr_evt {
/** \addtogroup dr_evt_utils
 *  @{ */

/**
 * @brief Return the current monotonic clock reading in seconds.
 * @details Uses std::chrono::steady_clock, so differences between two return
 * values are suitable for elapsed-time measurement and are unaffected by wall
 * clock adjustments. The absolute value has no calendar-time meaning.
 * @return Steady-clock time since its unspecified epoch as double seconds.
 */
inline double get_time() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(
           steady_clock::now().time_since_epoch()).count();
}

/**@}*/
} // namespace dr_evt

#endif  // DR_EVT_UTILS_TIMER_HPP
