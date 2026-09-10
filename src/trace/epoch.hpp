/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file epoch.hpp
 * @brief Timestamp, time-period, conversion, and calendar helpers.
 */

#ifndef DR_EVT_TRACE_EPOCH_HPP
#define DR_EVT_TRACE_EPOCH_HPP

#include "common.hpp"
#include <array>
#include <ctime>
#include <iostream>
#include <string>

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

/**
 * @brief Timestamp stored as integral and fractional seconds since the epoch.
 */
using epoch_t = std::pair<time_t, float>;
/// Inclusive-start, exclusive-end time period.
using period_t = std::pair<epoch_t, epoch_t>;

/// range from 0 to 7*24-1 as day of week [0-7) and hour [0-24)
using hour_bin_id_t = unsigned;

/** @brief Compare timestamps by integral then fractional seconds.
 * @param[in] t1 Left timestamp.
 * @param[in] t2 Right timestamp.
 * @return `true` when @p t1 precedes @p t2. */
inline bool operator<(const epoch_t &t1, const epoch_t &t2) {
  return ((t1.first < t2.first) ||
          ((t1.first == t2.first) && (t1.second < t2.second)));
}

/** @brief Test timestamps for exact equality.
 * @param[in] t1 Left timestamp.
 * @param[in] t2 Right timestamp.
 * @return `true` when both timestamp components are equal. */
inline bool operator==(const epoch_t &t1, const epoch_t &t2) {
  return ((t1.first == t2.first) && (t1.second == t2.second));
}

/** @brief Order periods by start time and then end time.
 * @param[in] t1 Left period.
 * @param[in] t2 Right period.
 * @return `true` when @p t1 sorts before @p t2. */
inline bool operator<(const period_t &t1, const period_t &t2) {
  return ((t1.first < t2.first) ||
          ((t1.first == t2.first) && (t1.second < t2.second)));
}

/** @brief Return the signed time difference between two timestamps.
 * @param[in] t1 Timestamp serving as the minuend.
 * @param[in] t2 Timestamp serving as the subtrahend.
 * @return Difference `t1 - t2` in seconds as tdiff_t. */
inline tdiff_t operator-(const epoch_t &t1, const epoch_t &t2) {
  return std::difftime(t1.first, t2.first) + (t1.second - t2.second);
}

/** @brief Format an epoch timestamp. @param[in] t Timestamp to format. @return
 * Text representation. */
std::string to_string(const epoch_t &t);

/** @brief Write an epoch timestamp. @param[in,out] os Destination stream.
 * @param[in] t Timestamp. @return os. */
std::ostream &operator<<(std::ostream &os, const epoch_t &t);

/**
 * @brief Format simulation time for trace output.
 *
 * Format a sim_time_t for trace/resource-trace output. Default (msec=false)
 * truncates to whole seconds as a plain integer, matching all existing
 * traces and tests, which only ever use integer-second submit times.
 * When msec=true, formats with millisecond precision (3 decimal places)
 * instead, for traces that need sub-second timing. Shared by Simulation
 * and Trace, since both write this same trace/resource-trace format.
 * @param[in] t Simulation time in seconds.
 * @param[in] msec Whether to retain millisecond precision.
 * @return Formatted timestamp as std::string.
 */
std::string format_sim_time(sim_time_t t, bool msec);

/**
 * @brief Check whether text represents a valid timestamp.
 * @param[in] time_str Candidate timestamp text.
 * @return true when parsing succeeds.
 */
bool is_timestamp(const std::string &time_str);

/**
 * @brief Convert timestamp text to epoch seconds and fraction.
 * @param[in] time_str Input timestamp text.
 * @return Parsed epoch_t value.
 */
epoch_t convert_time(const std::string &time_str);

/** @brief Convert an epoch timestamp to a scalar seconds value.
 * @tparam T Arithmetic result type.
 * @param[in] e Timestamp to convert.
 * @return Integral and fractional components combined as T. */
template <typename T = double> inline T convert_epoch(const epoch_t &e) {
  return static_cast<T>(e.first) + static_cast<T>(e.second);
}

/**
 * @brief Parse ISO timestamp with timezone offset and convert to UTC
 *
 * Parses timestamps like "2024-01-01T12:00:00-08:00" and converts to UTC.
 * Returns both the UTC epoch and the extracted timezone offset string.
 *
 * @param[in] time_str ISO timestamp with timezone (e.g.,
 * "2024-01-01T12:00:00-08:00").
 * @return Pair of UTC epoch_t and timezone-offset string (e.g., "-08:00").
 */
std::pair<epoch_t, std::string>
parse_time_with_timezone(const std::string &time_str);

/**
 * @brief Convert epoch_t to local time string with timezone offset
 *
 * @param[in] t UTC epoch time.
 * @param[in] tz_offset Timezone offset string (e.g., "-08:00").
 * @return Local-time string (e.g., "2024-01-01 12:00:00").
 */
std::string to_local_time_string(const epoch_t &t,
                                 const std::string &tz_offset);

/**
 * @brief Return the hour-of-week bin for a timestamp.
 * @param[in] t Time in integral epoch seconds.
 * @return Hour bin in [0, 167].
 */
hour_bin_id_t get_hour_bin_id(const std::time_t t);

/** @brief Return the day of week. @param[in] e Timestamp. @return day_of_week
 * value. */
day_of_week weekday(const epoch_t &e);

/** @brief Return next week boundary. @param[in] t Timestamp. @return Integral
 * epoch seconds. */
std::time_t get_time_of_next_week_start(const std::time_t t);
/** @brief Return next week boundary for a fractional epoch timestamp.
 * @param[in] t Timestamp.
 * @return Integral epoch seconds. */
std::time_t get_time_of_next_week_start(const epoch_t &t);

/** @brief Return current week boundary. @param[in] t Timestamp. @return
 * Integral epoch seconds. */
std::time_t get_time_of_cur_week_start(const std::time_t t);
/** @brief Return current week boundary for a fractional epoch timestamp.
 * @param[in] t Timestamp.
 * @return Integral epoch seconds. */
std::time_t get_time_of_cur_week_start(const epoch_t &t);

/** @brief Fill all hour boundaries for the week containing a timestamp.
 * @param[in] t Timestamp in the target week.
 * @param[out] bo Array of 169 integral epoch boundaries. */
void hour_boundaries_of_week(const std::time_t t,
                             std::array<std::time_t, 7 * 24 + 1> &bo);

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_EPOCH_HPP
