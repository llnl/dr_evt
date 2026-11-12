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

/**
 * @brief Scalar capacity operations with a compile-time observer selection.
 * @tparam Observer Observer with static enabled and, when enabled, observe().
 * Defaults to NoResourceObserver, which removes observation code at compile
 * time.
 * @details State belongs to the caller's timeline or live accounting records.
 */
template <typename Observer = NoResourceObserver> struct CountModel {
  using Request = num_nodes_t;
  using State = num_nodes_t;
  using Allocation = num_nodes_t;
  using Timeline = CountTimeline;
  /// Aggregate placement needs only time; the job retains its scalar request.
  using Placement = sim_time_t;

  /**
   * @brief Return the start time carried by an aggregate placement.
   * @param[in] placement Scalar start time returned by a fit search.
   * @return The placement's start time.
   */
  static sim_time_t start_time(Placement placement) { return placement; }

  /**
   * @brief Test an interval at its requested start without changing the plan.
   * @tparam Forecast Planner providing can_fit(start, quantity, duration).
   * @param[in] plan Borrowed availability or reservation timeline.
   * @param[in] quantity Requested capacity.
   * @param[in] duration Planned interval duration; CountTimeline treats a
   * nonpositive duration as a point-in-time capacity query.
   * @param[in] start Requested start within the planner's supported time domain.
   * @param[in] context Synchronous observation metadata. Any diagnostic owner
   * required by Observer must remain valid throughout this call.
   * @return true when the planner admits this interval; otherwise false.
   * @details An enabled observer receives the result and a non-owning timeline
   * identity. Observation does not change the planner's decision.
   */
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

  /**
   * @brief Find a feasible start using the forecast's timing semantics.
   * @tparam Forecast Planner providing earliest_fit(quantity, duration, start).
   * @param[in] plan Borrowed timeline, left unchanged by this search.
   * @param[in] quantity Requested capacity.
   * @param[in] duration Planned interval duration; CountTimeline treats a
   * nonpositive duration as a point-in-time capacity query.
   * @param[in] start Earliest requested start in the planner's supported domain.
   * @param[in] context Synchronous observation metadata with any counter owner
   * required by Observer valid for the duration of this call.
   * @return A scalar placement when the planner finds a fit, or std::nullopt.
   * @details The observer receives success or failure without changing the
   * result. A timeline identity supplied to it does not transfer ownership.
   */
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

  /**
   * @brief Apply a speculative reservation to the caller's planning timeline.
   * @tparam Forecast Planner providing reserve(start, quantity, duration).
   * @param[in,out] plan Timeline whose available capacity is reduced.
   * @param[in] placement Start of the already-admitted reservation.
   * @param[in] quantity Capacity to reserve.
   * @param[in] duration Reservation duration; CountTimeline reserves the
   * half-open interval and does nothing for a nonpositive duration.
   * @param[in] context Synchronous observation metadata with any required
   * diagnostic owner valid throughout this call.
   * @pre The interval must satisfy the planner's reservation preconditions.
   * @details Updates planning state only; it does not launch a job or charge
   * live allocation state. An enabled observer records the call even when the
   * planner treats the duration as a no-op.
   */
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

  /**
   * @brief Deplete pass-local availability for a projected immediate start.
   * @param[in,out] available Caller-owned planning capacity to decrement.
   * @param[in] quantity Admitted job's capacity demand.
   * @param[in] context Synchronous metadata with any required observer owner.
   * @pre quantity must not exceed available; this operation does not validate
   * admission or change the committed allocation counters.
   */
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

  /**
   * @brief Charge a committed start to the caller's allocated capacity.
   * @param[in,out] allocated Live allocation counter to increment.
   * @param[in] quantity Capacity committed to the starting job.
   * @param[in] context Synchronous metadata with any required observer owner.
   * @pre The sum must be representable by State. Admission is the caller's
   * responsibility; this operation neither validates fit nor updates a timeline.
   */
  static void allocate(State &allocated, Allocation quantity,
                       ResourceContext context = {}) {
    const State before = allocated;
    allocated += quantity;
    if constexpr (Observer::enabled) {
      Observer::observe({ResourceOperation::Allocate, ResourceScope::Allocated,
                         context, quantity, before, allocated});
    }
  }

  /**
   * @brief Remove capacity from a live allocated or cleanup counter.
   * @param[in,out] occupied Caller-owned counter to decrement.
   * @param[in] quantity Capacity leaving that accounting scope.
   * @param[in] context Synchronous metadata with any required observer owner.
   * @param[in] scope Accounting scope reported to the observer.
   * @pre quantity must not exceed occupied, and scope must describe that counter.
   * @details The caller owns event timing and must release each charge once.
   * This operation does not modify speculative reservation timelines.
   */
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

  /**
   * @brief Charge ended-job capacity to the cleanup counter until release.
   * @param[in,out] releasing Caller-owned cleanup counter to increment.
   * @param[in] quantity Ended-job capacity that remains unavailable.
   * @param[in] context Synchronous metadata with any required observer owner.
   * @pre The sum must be representable by State.
   * @details The caller separately removes this quantity from allocated state
   * and schedules its RELEASE event. Only cleanup state is updated here.
   */
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

} // namespace dr_evt
#endif
