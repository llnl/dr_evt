/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file count_model.hpp
 * @brief Scalar consumable capacity with compile-time observation hooks.
 * @details Planning consumes free capacity; committed events charge occupied
 * capacity. These are separate states, never two charges to the same state.
 */

#ifndef DR_EVT_SIM_RESOURCE_COUNT_MODEL_HPP
#define DR_EVT_SIM_RESOURCE_COUNT_MODEL_HPP

#include "sim/resource/count_timeline.hpp"
#include "sim/resource/resource_model.hpp"

namespace dr_evt {

template <typename Observer = NoResourceObserver> struct CountModel {
  using Request = num_nodes_t;
  using State = num_nodes_t;
  using Allocation = num_nodes_t;
  using Timeline = CountTimeline;
  /// Aggregate placement needs only time; the job retains its scalar request.
  using Placement = sim_time_t;

  static sim_time_t start_time(Placement placement) { return placement; }

  template <typename Forecast>
  static bool can_fit(const Forecast &plan, Request quantity, tdiff_t duration,
                      sim_time_t start, ResourceContext context = {}) {
    const bool fits = plan.can_fit(start, quantity, duration);
    if constexpr (Observer::enabled) {
      context.timeline = &plan;
      Observer::observe({ResourceOperation::FitNow, ResourceScope::Timeline,
                         context, quantity, 0, 0, start, duration, fits});
    }
    return fits;
  }

  template <typename Forecast>
  static std::optional<Placement>
  find_fit(const Forecast &plan, Request quantity, tdiff_t duration,
           sim_time_t start, ResourceContext context = {}) {
    const auto at = plan.earliest_fit(quantity, duration, start);
    if constexpr (Observer::enabled) {
      context.timeline = &plan;
      Observer::observe({ResourceOperation::FindFit, ResourceScope::Timeline,
                         context, quantity, 0, 0, at.value_or(start), duration,
                         at.has_value()});
    }
    return at;
  }

  template <typename Forecast>
  static void reserve(Forecast &plan, const Placement &placement,
                      Request quantity, tdiff_t duration,
                      ResourceContext context = {}) {
    plan.reserve(start_time(placement), quantity, duration);
    if constexpr (Observer::enabled) {
      context.timeline = &plan;
      Observer::observe({ResourceOperation::Reserve, ResourceScope::Timeline,
                         context, quantity, 0, 0, start_time(placement), duration});
    }
  }

  static void consume_available(State &available, Request quantity,
                                ResourceContext context = {}) {
    const State before = available;
    available -= quantity;
    if constexpr (Observer::enabled) {
      Observer::observe({ResourceOperation::ProjectedStart,
                         ResourceScope::Available, context, quantity, before,
                         available});
    }
  }

  static void allocate(State &allocated, Allocation quantity,
                       ResourceContext context = {}) {
    const State before = allocated;
    allocated += quantity;
    if constexpr (Observer::enabled) {
      Observer::observe({ResourceOperation::Allocate, ResourceScope::Allocated,
                         context, quantity, before, allocated});
    }
  }

  static void release(State &occupied, Allocation quantity,
                      ResourceContext context = {},
                      ResourceScope scope = ResourceScope::Allocated) {
    const State before = occupied;
    occupied -= quantity;
    if constexpr (Observer::enabled) {
      Observer::observe({ResourceOperation::Release, scope, context, quantity,
                         before, occupied});
    }
  }

  /// END moves capacity into cleanup; it is unavailable until a RELEASE event.
  static void hold(State &releasing, Allocation quantity,
                   ResourceContext context = {}) {
    const State before = releasing;
    releasing += quantity;
    if constexpr (Observer::enabled) {
      Observer::observe({ResourceOperation::Hold, ResourceScope::Releasing,
                         context, quantity, before, releasing});
    }
  }
};

// Resource diagnostics select the observer, not the resource model.
// Currently CountModel is the only model wired to this build option.
#if DR_EVT_RESOURCE_MODEL_DIAGNOSTICS
using ActiveCountModel = CountModel<CountingResourceObserver>;
#else
using ActiveCountModel = CountModel<>;
#endif

} // namespace dr_evt
#endif
