/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file resource_model.hpp
 * @brief Compile-time resource observation contract.
 * @details Models define Request, State, Placement, Allocation, and Timeline.
 * Observers provide static enabled and observe(); disabled observers require
 * neither storage nor construction of observation records.
 */

#ifndef DR_EVT_SIM_RESOURCE_RESOURCE_MODEL_HPP
#define DR_EVT_SIM_RESOURCE_RESOURCE_MODEL_HPP

#include "dr_evt_types.hpp"
#include <array>

namespace dr_evt {

enum class ResourceOperation {
  FitNow, FindFit, Reserve, ProjectedStart, Allocate, Release, Hold, Count
};
enum class ResourceScope { Timeline, Available, Allocated, Releasing };

struct ResourceDiagnostics;

/**
 * @brief Borrowed job, pass, timeline, and counter context for an observation.
 * @details Timeline and diagnostic-owner pointers are valid during the
 * synchronous observation call. They transfer no ownership. A simulation
 * observer requires a non-null diagnostics owner; other observers may ignore it.
 */
struct ResourceContext {
  job_no_t job = std::numeric_limits<job_no_t>::max();
  sim_time_t time = 0;
  size_t pass = 0;
  const void *timeline = nullptr;
  ResourceDiagnostics *diagnostics = nullptr; ///< Non-owning counter owner.
};

/// Scalar snapshots apply to Available/Allocated/Releasing, not Timeline.
struct ResourceObservation {
  ResourceOperation operation;
  ResourceScope scope;
  ResourceContext context;
  num_nodes_t quantity;
  num_nodes_t before = 0;
  num_nodes_t after = 0;
  sim_time_t start = 0;
  tdiff_t duration = 0;
  bool success = true;
};

/** @brief Counters owned by one diagnostic simulation or trace. */
struct ResourceDiagnostics {
  static constexpr size_t operation_count =
      static_cast<size_t>(ResourceOperation::Count);
  std::array<std::uint64_t, operation_count> calls{};
  std::array<std::uint64_t, operation_count> quantities{};
  std::uint64_t failed_fits = 0;

  /** @brief Clear all counters without altering resource state. */
  void reset() { *this = {}; }
  /** @brief Accumulate an observation without retaining its context.
   * @param[in] event Resource operation to count.
   * @pre event.operation must identify an operation, not the Count sentinel.
   */
  void observe(const ResourceObservation &event) {
    const auto index = static_cast<size_t>(event.operation);
    ++calls[index];
    quantities[index] += event.quantity;
    if (!event.success) {
      ++failed_fits;
    }
  }
};

/** @brief Observe into the counter owner carried by the operation context. */
struct SimulationResourceObserver {
  static constexpr bool enabled = true;
  /** @brief Count an operation in its simulation-owned counters.
   * @param[in] event Observation whose context has a valid diagnostics owner.
   * @details The owner must outlive this synchronous call. No context is saved.
   */
  static void observe(const ResourceObservation &event) {
    event.context.diagnostics->observe(event);
  }
};

/** @brief Disable observation without requiring an observe() implementation. */
struct NoResourceObserver {
  static constexpr bool enabled = false;
};

/**
 * @brief Explicit test observer with shared static operation counters.
 * @details Intended for single-threaded tests; it does not own per-simulation
 * counters and performs no per-event heap allocation.
 */
struct CountingResourceObserver {
  static constexpr bool enabled = true;
  static constexpr size_t operation_count =
      static_cast<size_t>(ResourceOperation::Count);
  inline static std::array<std::uint64_t, operation_count> calls{};
  inline static std::array<std::uint64_t, operation_count> quantities{};
  inline static std::uint64_t failed_fits = 0;

  /** @brief Clear this test observer's shared counters. */
  static void reset() {
    calls.fill(0);
    quantities.fill(0);
    failed_fits = 0;
  }
  /** @brief Count an operation without retaining its metadata.
   * @param[in] event Resource operation to count.
   * @pre event.operation must identify an operation, not the Count sentinel. */
  static void observe(const ResourceObservation &event) {
    const auto index = static_cast<size_t>(event.operation);
    ++calls[index];
    quantities[index] += event.quantity;
    if (!event.success) {
      ++failed_fits;
    }
  }
};

} // namespace dr_evt
#endif
