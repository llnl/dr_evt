/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file count_timeline.hpp
 * @brief Aggregate quantity reservation timeline; no resource identities.
 */

#ifndef DR_EVT_SIM_RESOURCE_COUNT_TIMELINE_HPP
#define DR_EVT_SIM_RESOURCE_COUNT_TIMELINE_HPP

#include "dr_evt_types.hpp"
#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace dr_evt {

/**
 * @brief Mutable free-node timeline used for reservation planning.
 * @details Each entry begins a constant-capacity segment.  A reservation
 * subtracts its nodes for its complete planned interval, so later searches
 * see the resource-time commitments made by earlier reservations. Timelines
 * have value semantics: a scheduler may copy a common baseline into separate
 * availability and protection plans before extending either plan.
 */
class CountTimeline {
  // The opt-in native forecast model retains event times before coalescing.
  friend class OneSecondEventGroupTimeline;
  using segment_t = std::pair<sim_time_t, num_nodes_t>;
  std::vector<segment_t> m_free_nodes;

  /**
   * @brief Return the timeline boundary at time, creating it if necessary.
   * @param[in] time Segment boundary to locate.
   * @return Index of the existing or newly inserted boundary.
   */
  size_t split_at(sim_time_t time) {
    auto boundary =
        std::lower_bound(m_free_nodes.begin(), m_free_nodes.end(), time,
                         [](const segment_t &segment, sim_time_t value) {
                           return segment.first < value;
                         });
    if (boundary == m_free_nodes.end() || boundary->first != time) {
      const size_t index =
          static_cast<size_t>(std::distance(m_free_nodes.begin(), boundary));
      m_free_nodes.insert(boundary, {time, std::prev(boundary)->second});
      return index;
    }
    return static_cast<size_t>(std::distance(m_free_nodes.begin(), boundary));
  }

public:
  /**
   * @brief Construct a timeline from ordered capacity segments.
   * @param[in] free_nodes Ordered pairs of segment start time and free nodes.
   */
  explicit CountTimeline(std::vector<segment_t> free_nodes)
      : m_free_nodes(std::move(free_nodes)) {}

  /**
   * @brief Construct a timeline from an ordered capacity map.
   * @param[in] free_nodes Segment start times and their free-node capacities.
   */
  explicit CountTimeline(
      const std::map<sim_time_t, num_nodes_t> &free_nodes)
      : m_free_nodes(free_nodes.begin(), free_nodes.end()) {}

  /**
   * @brief Find the earliest start whose requested interval has enough nodes.
   * @details A nonpositive duration is a point-in-time capacity query. It
   * returns the first segment at or after @p start with enough free nodes
   * without scanning a full interval.
   * @param[in] nodes Number of nodes required by the interval.
   * @param[in] duration Interval duration, or zero for a capacity query.
   * @param[in] start Earliest time the interval may begin.
   * @tparam IncludeEndpoint When true, require enough free nodes at
   * start + duration too: a reservation beginning exactly when the job ends
   * can block the fit. When false, check only [start, start + duration).
   * This changes the fit test, not the duration of capacity reservations.
   * @return The earliest fitting start time, or std::nullopt when no timeline
   *         segment has sufficient capacity.
   */
  template <bool IncludeEndpoint = false>
  std::optional<sim_time_t> earliest_fit(num_nodes_t nodes, tdiff_t duration,
                                         sim_time_t start) const {
    if (m_free_nodes.empty()) {
      return std::nullopt;
    }
    auto segment = std::prev(
        std::upper_bound(m_free_nodes.begin(), m_free_nodes.end(), start,
                         [](sim_time_t value, const segment_t &entry) {
                           return value < entry.first;
                         }));
    if (duration <= 0) {
      while (segment != m_free_nodes.end() && segment->second < nodes) {
        ++segment;
      }
      if (segment == m_free_nodes.end()) {
        return std::nullopt;
      }
      return segment->first <= start ? start : segment->first;
    }
    for (;;) {
      const sim_time_t end = start + duration;
      const auto after_interval = [end](sim_time_t boundary) {
        if constexpr (IncludeEndpoint) {
          return boundary > end;
        } else {
          return boundary >= end;
        }
      };
      auto scan = segment;
      while (scan != m_free_nodes.end() &&
             (scan == segment || !after_interval(scan->first)) &&
             scan->second >= nodes) {
        ++scan;
      }
      if (scan == m_free_nodes.end() ||
          (scan != segment && after_interval(scan->first))) {
        return start;
      }
      do {
        ++scan;
      } while (scan != m_free_nodes.end() && scan->second < nodes);
      if (scan == m_free_nodes.end()) {
        return std::nullopt;
      }
      segment = scan;
      start = scan->first;
    }
  }

  /**
   * @brief Check whether an interval fits without changing the plan.
   * @details Inspect only the requested half-open interval and stop at the
   * first capacity shortage. Do not search for a later fitting start.
   * @param[in] start Interval start time.
   * @param[in] nodes Number of nodes required by the interval.
   * @param[in] duration Interval duration.
   * @return true when the requested interval has sufficient free capacity;
   *         otherwise false.
   */
  bool can_fit(sim_time_t start, num_nodes_t nodes, tdiff_t duration) const {
    if (m_free_nodes.empty()) {
      return false;
    }
    auto segment = std::prev(
        std::upper_bound(m_free_nodes.begin(), m_free_nodes.end(), start,
                         [](sim_time_t value, const segment_t &entry) {
                           return value < entry.first;
                         }));
    if (segment->second < nodes) {
      return false;
    }
    if (duration <= 0) {
      return true;
    }
    const sim_time_t end = start + duration;
    for (++segment; segment != m_free_nodes.end() && segment->first < end;
         ++segment) {
      if (segment->second < nodes) {
        return false;
      }
    }
    return true;
  }

  /**
   * @brief Reserve nodes over [start, start + duration).
   * @param[in] start Reservation start time.
   * @param[in] nodes Nodes unavailable during the reservation.
   * @param[in] duration Reservation duration.
   */
  void reserve(sim_time_t start, num_nodes_t nodes, tdiff_t duration) {
    if (duration <= 0) {
      return;
    }
    const size_t first = split_at(start);
    size_t last = split_at(start + duration);
    for (size_t segment = first; segment != last; ++segment) {
      m_free_nodes[segment].second -= nodes;
    }
    const auto coalesce = [this](size_t boundary) {
      if (boundary != 0 &&
          m_free_nodes[boundary - 1].second == m_free_nodes[boundary].second) {
        m_free_nodes.erase(m_free_nodes.begin() + boundary);
        return true;
      }
      return false;
    };
    if (coalesce(first)) {
      --last;
    }
    coalesce(last);
  }
};

} // namespace dr_evt
#endif
