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

/// Context supplied by the caller; timeline identity is valid during the call.
struct ResourceContext {
  job_no_t job = std::numeric_limits<job_no_t>::max();
  sim_time_t time = 0;
  size_t pass = 0;
  const void *timeline = nullptr;
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

struct NoResourceObserver {
  static constexpr bool enabled = false;
};

/// Optional diagnostic observer. Single-threaded, no per-event heap allocation.
struct CountingResourceObserver {
  static constexpr bool enabled = true;
  static constexpr size_t operation_count =
      static_cast<size_t>(ResourceOperation::Count);
  inline static std::array<std::uint64_t, operation_count> calls{};
  inline static std::array<std::uint64_t, operation_count> quantities{};
  inline static std::uint64_t failed_fits = 0;

  static void reset() {
    calls.fill(0);
    quantities.fill(0);
    failed_fits = 0;
  }
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
