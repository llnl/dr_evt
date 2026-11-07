/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file reservation_search_cache.hpp
 * @brief Per-pass lower bounds for repeated reservation interval searches.
 */

#ifndef DR_EVT_SIM_RESERVATION_SEARCH_CACHE_HPP
#define DR_EVT_SIM_RESERVATION_SEARCH_CACHE_HPP

#include "sim/scheduler_base.hpp"

namespace dr_evt {

/** @brief Search bounds retained across verified capacity subtraction. */
class Reservation_Search_Cache {
  /// Node-count/duration pair identifying a request shape.
  using key_t = std::pair<num_nodes_t, tdiff_t>;
  /** @brief Previous answer for a shape at a specific query origin. */
  struct Bound {
    /// Earliest permitted start used by the previous query.
    sim_time_t origin;
    /// Previous earliest fit, or no value when the query failed.
    std::optional<sim_time_t> earliest;
  };
  /** @brief Last successful query used to verify the next reservation. */
  struct Fit {
    /// Node count whose availability was verified.
    num_nodes_t nodes;
    /// Duration covered by the successful query.
    tdiff_t duration;
    /// Verified interval start.
    sim_time_t start;
  };
  /// Maximum request shapes retained within one scheduling pass.
  static constexpr size_t max_cached_shapes = 256;
  /// Independently owned capacity plan for the current pass.
  Reservation_Timeline m_plan;
  /// Per-shape lower bounds; successful answers are rechecked before use.
  mutable std::map<key_t, Bound> m_bounds;
  /// Successful query covering a capacity update, if one is available.
  mutable std::optional<Fit> m_last_fit;

public:
  /**
   * @brief Wrap an independent reservation plan with bounded search state.
   * @param[in] plan Initial capacity timeline, moved into this wrapper.
   */
  explicit Reservation_Search_Cache(Reservation_Timeline plan)
      : m_plan(std::move(plan)) {}

  /**
   * @brief Find the earliest fit using an earlier answer as a lower bound.
   * @details Capacity subtraction cannot move an identical request earlier.
   * A successful previous answer is tested again against the current plan;
   * a previous failure remains valid until an unverified update clears it.
   * Bounds are specific to node count, duration, and query origin. At most
   * 256 interval shapes are retained for the lifetime of this plan.
   * @param[in] nodes Requested node count.
   * @param[in] duration Requested duration; nonpositive values query capacity.
   * @param[in] start Earliest permitted start time.
   * @return Earliest fitting start, or std::nullopt when none exists.
   */
  std::optional<sim_time_t> earliest_fit(num_nodes_t nodes, tdiff_t duration,
                                         sim_time_t start) const {
    m_last_fit.reset();
    const key_t key{nodes, std::max(duration, tdiff_t{0})};
    const auto cached = m_bounds.find(key);
    sim_time_t search_start = start;
    if (cached != m_bounds.end() && cached->second.origin == start) {
      if (!cached->second.earliest) {
        return std::nullopt;
      }
      search_start = *cached->second.earliest;
    }
    const auto fit = m_plan.earliest_fit(nodes, duration, search_start);
    if (cached != m_bounds.end()) {
      cached->second = {start, fit};
    } else if (m_bounds.size() < max_cached_shapes) {
      m_bounds.emplace(key, Bound{start, fit});
    }
    if (fit) {
      m_last_fit = Fit{nodes, duration, *fit};
    }
    return fit;
  }

  /**
   * @brief Apply a reservation and retain only justified search bounds.
   * @details Bounds survive when the update is covered by the last successful
   * query. Other updates clear them before delegating to the original plan,
   * whose unsigned capacity subtraction can wrap for an infeasible interval.
   * Nonpositive durations leave the plan and bounds unchanged.
   * @param[in] start Reservation start time.
   * @param[in] nodes Nodes to subtract for the interval.
   * @param[in] duration Reservation duration.
   */
  void reserve(sim_time_t start, num_nodes_t nodes, tdiff_t duration) {
    if (duration <= 0) {
      return;
    }
    // Rounding can lengthen the committed interval after its search. Keep
    // bounds only when the last successful query covered the actual update.
    if (!m_last_fit || m_last_fit->nodes != nodes ||
        m_last_fit->start != start || duration > m_last_fit->duration) {
      m_bounds.clear();
    }
    m_plan.reserve(start, nodes, duration);
    m_last_fit.reset();
  }
};

} // namespace dr_evt
#endif
