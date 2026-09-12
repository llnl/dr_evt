/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file dr_evt_types.hpp
 * @brief Project-wide scalar types, limits, event constants, and queue enums.
 */

#ifndef DR_EVT_DR_EVT_TYPES_HPP
#define DR_EVT_DR_EVT_TYPES_HPP
#include <cstddef> // size_t
#include <cstdint> // uint64_t
#include <limits>  // std::numeric_limits
#include <utility> // std::pair

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

namespace dr_evt {
/** \addtogroup dr_evt_global
 *  @{ */

/// Latest supported textual timestamp.
constexpr const char *const max_tstamp = "2118-12-31 23:59:59.0";

/// Three-letter names indexed by day_of_week.
constexpr const char *const week_day_str[] = {"Sun", "Mon", "Tue", "Wed",
                                              "Thu", "Fri", "Sat"};

/** @brief Day-of-week values used by calendar and submission statistics. */
enum day_of_week {
  Sun = 0,
  Mon = 1,
  Tue = 2,
  Wed = 3,
  Thu = 4,
  Fri = 5,
  Sat = 6
};

/// Duration in seconds.
using tdiff_t = double;
/// Simulation clock value in seconds.
using sim_time_t = tdiff_t;
using timeout_t = unsigned;   ///< time limit in seconds
using num_nodes_t = unsigned; ///< Number of compute nodes type
using num_jobs_t = size_t;    ///< Number of jobs type
using job_no_t = num_jobs_t;  ///< Job number type
using num_cols_t = unsigned;  ///< Number of columns type
using col_no_t = num_cols_t;  ///< Column-number type.

/**
 *  Describes a period [t_start, t_end). Both t_start, and t_end are the
 *  amount of time-passed since a specific reference point in the past.
 */
using trange_t = std::pair<tdiff_t, tdiff_t>;

/// Total number of nodes on Lassen
constexpr const unsigned total_nodes = 795u;

/// Maximum timeout that can be set for a batch job in seconds
constexpr const tdiff_t max_batch_job_time = static_cast<tdiff_t>(12 * 60 * 60);

/// Largest practical simulation time, below floating-point maximum.
constexpr const sim_time_t max_sim_time =
    std::numeric_limits<sim_time_t>::max() * 0.9;

/// Arrival event
constexpr const bool arrival = true;
/// Departure event
constexpr const bool departure = false;

/** @brief Supported trace queue identities.
 * @details The fixed-width values are shared by both trace-input modes.
 * QueueUnknown is 0, while Queue1 is 1. The external q_id value is the same
 * numeric value as its internal queue enum. Legacy queue names are mapped to
 * these identities in parse_utils.cpp.
 */
#if PBATCH_GROUP
enum job_queue_t : std::uint16_t {
  QueueUnknown = 0,
  Queue1 = 1,
  Queue2,
  Queue3,
  Queue4,
  Queue5,
  Queue6,
  Queue7,
  Queue8,
  Queue9,
  Queue10
};
/** @brief Test whether a queue identifier denotes a batch queue.
 * @param[in] _q Queue identifier to test. */
#define _Is_Batch(_q) ((_q) == Queue1)
/** @brief Test whether a queue identifier denotes an exclusive queue.
 * @param[in] _q Queue identifier to test. */
#if DR_EVT_LEGACY_QUEUE_INPUT
#define _Is_Exclusive(_q) ((_q) == Queue2)
#else
#define _Is_Exclusive(_q) (false)
#endif
#else
enum job_queue_t : std::uint16_t {
  QueueUnknown = 0,
  Queue1 = 1,
  Queue2,
  Queue3,
  Queue4,
  Queue5,
  Queue6,
  Queue7,
  Queue8,
  Queue9,
  Queue10,
  Queue11,
  Queue12,
  Queue13,
  Queue14
};
/** @brief Test whether a queue identifier denotes a batch queue.
 * @param[in] _q Queue identifier to test. */
#define _Is_Batch(_q)                                                          \
  ((_q) == Queue1 || (_q) == Queue2 || (_q) == Queue3 || (_q) == Queue4 ||     \
   (_q) == Queue5)
/** @brief Test whether a queue identifier denotes an exclusive queue.
 * @param[in] _q Queue identifier to test. */
#if DR_EVT_LEGACY_QUEUE_INPUT
#define _Is_Exclusive(_q) ((_q) == Queue6)
#else
#define _Is_Exclusive(_q) (false)
#endif
#endif

/// CSV substring range stored as [start position, length].
using substr_pos_t = std::pair<size_t, size_t>;

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_DR_EVT_TYPES_HPP
