/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file openpbs_forecast_timeline.hpp
 * @brief OpenPBS 23.06 future-event grouping over exact capacity intervals.
 */

#ifndef DR_EVT_SIM_OPENPBS_FORECAST_TIMELINE_HPP
#define DR_EVT_SIM_OPENPBS_FORECAST_TIMELINE_HPP

#include "sim/scheduler_base.hpp"

namespace dr_evt {

/**
 * @brief Search capacity only at OpenPBS-style forecast group endpoints.
 * @details The current time is checked first. Subsequent simulated events
 * through one second after the earliest remaining event form a group. After
 * a group releases capacity, its last actual event time is checked. A lone
 * event at 10 is checked at 10; events at 10 and 11 are checked together at 11.
 * Capacity intervals remain exact and half-open, and actual execution times
 * are unchanged. Separate event records retain starts/ends even when equal
 * capacities cause the underlying interval timeline to coalesce a boundary.
 * This models opt_backfill_fuzzy=off on integer-second inputs, not node
 * placement, fairshare, scheduling-cycle time limits, or server/MOM behavior.
 */
class OpenPBS_Forecast_Timeline {
  /// Exact capacity commitments used to check the whole requested interval.
  Reservation_Timeline m_plan;
  /// Event times; true means at least one event releases capacity.
  std::map<sim_time_t, bool> m_events;

public:
  /**
   * @brief Construct an independent forecast from a capacity timeline.
   * @param[in] plan Current capacity and future releases/changes.
   */
  explicit OpenPBS_Forecast_Timeline(Reservation_Timeline plan)
      : m_plan(std::move(plan)) {
    for (size_t i = 1; i < m_plan.m_free_nodes.size(); ++i) {
      // Baseline boundaries are releases or scheduled capacity changes.
      // A capacity change can affect eligibility even without a net increase.
      m_events.emplace(m_plan.m_free_nodes[i].first, true);
    }
  }

  /**
   * @brief Find a fit at now or at the end of a simulated event group.
   * @param[in] nodes Requested capacity.
   * @param[in] duration Requested interval duration; zero queries a point.
   * @param[in] start Current simulated time, checked before future groups.
   * @return First fitting tested time, or std::nullopt if none exists.
   */
  std::optional<sim_time_t> earliest_fit(num_nodes_t nodes, tdiff_t duration,
                                        sim_time_t start) const {
    if (m_plan.can_fit(start, nodes, duration)) {
      return start;
    }
    auto event = m_events.upper_bound(start);
    while (event != m_events.end()) {
      const sim_time_t endpoint = event->first + 1.0;
      sim_time_t tested_time = event->first;
      bool released = false;
      do {
        tested_time = event->first;
        released = released || event->second;
        ++event;
      } while (event != m_events.end() && event->first <= endpoint);
      if (released && m_plan.can_fit(tested_time, nodes, duration)) {
        return tested_time;
      }
    }
    return std::nullopt;
  }

  /**
   * @brief Commit exact capacity and add the planned start/end events.
   * @param[in] start Planned start, without rounding or shifting.
   * @param[in] nodes Capacity consumed for the entire interval.
   * @param[in] duration Requested interval duration.
   */
  void reserve(sim_time_t start, num_nodes_t nodes, tdiff_t duration) {
    if (duration <= 0) {
      return;
    }
    m_plan.reserve(start, nodes, duration);
    m_events.emplace(start, false);
    m_events[start + duration] = true;
  }
};

} // namespace dr_evt
#endif
