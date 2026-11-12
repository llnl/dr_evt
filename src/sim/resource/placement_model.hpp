/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file placement_model.hpp
 * @brief Compile-time bitmap placement, exact reservations, and accounting.
 */
#ifndef DR_EVT_SIM_RESOURCE_PLACEMENT_MODEL_HPP
#define DR_EVT_SIM_RESOURCE_PLACEMENT_MODEL_HPP

#include "sim/resource/placement_timeline.hpp"
#include "sim/resource/resource_model.hpp"

namespace dr_evt {

/** @brief Compile-time selectors for independently measurable placement
 * features.
 * @hideinitializer */
inline constexpr auto AllPlacementOptimizations = [] { return 15U; };
/** @brief Baseline placement operations, with all optional optimizations
 * disabled.
 * @hideinitializer */
inline constexpr auto NoPlacementOptimizations = [] { return 0U; };

/**
 * @brief Stateless whole-node placement operations over caller-owned bitmaps.
 * @tparam Observer Static observer; NoResourceObserver removes hooks at compile
 * time.
 * @tparam Order Compile-time low-ID or high-ID selection, independent of
 * storage.
 * @tparam CoarseFilter Captureless constexpr lambda returning bool; selects
 * timeline filtering and cache storage at compile time; defaults to exact-only.
 * @tparam Features Captureless constexpr lambda returning an unsigned mask:
 * 1 immediate fast path, 2 batch seeding, 4 reusable scratch, 8 boolean
 * fitting.
 * @details Requests are quantities, but placements and committed allocations
 * retain exact identities. This model is separate from scalar CapacityModel.
 * Planning and live accounting states remain distinct and caller-owned. Neither
 * resource inventory nor per-job state is stored in a model instance.
 */
template <typename Observer = NoResourceObserver,
          NodeOrder Order = NodeOrder::LowIdFirst,
          auto CoarseFilter = DisableCoarseFilter,
          auto Features = AllPlacementOptimizations>
class PlacementModel {
public:
  static constexpr bool immediate_fast_path =
      (Features() & 1U) != 0; ///< Immediate selection strategy.
  static constexpr bool batch_seeding =
      (Features() & 2U) != 0; ///< Initial running admission strategy.
  static constexpr bool reusable_scratch =
      (Features() & 4U) != 0; ///< Query storage strategy.
  static constexpr bool boolean_only =
      (Features() & 8U) != 0; ///< Feasibility result strategy.
  /// Number of interchangeable whole nodes requested.
  using Request = num_nodes_t;
  /// Caller-owned resource membership for one accounting scope.
  using State = Bitmap;
  /// Owning exact node identities, stored as dense contiguous word masks.
  using Allocation = BitmapAllocation;
  /// Exact start and owning selected identities.
  using Placement = BitmapPlacement;
  /// Exact sparse intervals with the selected optional coarse dense filter.
  using Timeline = BasicPlacementTimeline<CoarseFilter>;

  /** @brief Return a placement's exact start.
   * @param[in] placement Borrowed placement result.
   * @return Exact start time, never a coarse bucket boundary. */
  static sim_time_t start_time(const Placement &placement) noexcept {
    return placement.start;
  }

  /** @brief Select an immediate allocation before future commitments exist.
   * @param[in] eligible Caller-owned inventory, not retained.
   * @param[in] occupied Caller-owned allocation and cleanup union, not
   * retained.
   * @param[in] quantity Requested identities.
   * @param[in] duration Planned duration; nonpositive means a point query.
   * @param[in] start Exact immediate start.
   * @param[in] context Synchronous observation metadata, not retained.
   * @return Owning placement or std::nullopt when insufficient.
   * @throws std::invalid_argument for mismatched domains or invalid timing.
   * @throws std::bad_alloc if result or diagnostic storage cannot be allocated.
   * @pre No future reservation or resource outage constrains these resources.
   * @details Currently free nodes remain available; after future commitments
   * exist, use the timeline fitting operations. */
  static std::optional<Placement>
  find_available(const State &eligible, const State &occupied, Request quantity,
                 tdiff_t duration, sim_time_t start,
                 ResourceContext context = {}) {
    const auto end = duration > 0 ? start + duration : start;
    if (!std::isfinite(start) || !std::isfinite(duration) ||
        !std::isfinite(end) || !std::isfinite(end - start) ||
        (duration > 0 && end <= start))
      throw std::invalid_argument("invalid placement interval");
    auto allocation =
        eligible.template select_excluding<Order>(occupied, quantity);
    if constexpr (Observer::enabled) {
      auto free = eligible;
      free.remove(occupied);
      const auto count = static_cast<num_nodes_t>(free.count());
      Observer::observe({ResourceOperation::FitNow, ResourceScope::Available,
                         context, quantity, count, count, start, duration,
                         allocation.has_value()});
    }
    if (!allocation)
      return std::nullopt;
    return Placement{start, std::move(*allocation)};
  }

  /** @brief Test immediate exact interval feasibility.
   * @param[in] plan Borrowed caller-owned planner.
   * @param[in] quantity Number of resources requested.
   * @param[in] duration Duration; nonpositive means a point query.
   * @param[in] start Requested exact start within the planner's domain.
   * @param[in] context Synchronous observation context, not retained.
   * @return true when exact identities fit throughout the interval.
   * @throws std::invalid_argument for invalid timing.
   * @throws std::bad_alloc if planner scratch or filter storage cannot be
   * allocated. */
  static bool can_fit(const Timeline &plan, Request quantity, tdiff_t duration,
                      sim_time_t start, ResourceContext context = {}) {
    const bool result = plan.template can_fit<boolean_only, reusable_scratch>(
        start, quantity, duration);
    if constexpr (Observer::enabled) {
      context.timeline = &plan;
      Observer::observe({ResourceOperation::FitNow, ResourceScope::Timeline,
                         context, quantity, 0, 0, start, duration, result});
    }
    return result;
  }

  /** @brief Select identities at an exact immediate start.
   * @param[in] plan Borrowed caller-owned planner.
   * @param[in] quantity Number of resources requested.
   * @param[in] duration Duration; nonpositive means a point query.
   * @param[in] start Requested exact start within the planner's domain.
   * @param[in] context Synchronous observation context, not retained.
   * @return Owning exact placement, or std::nullopt when it cannot start here.
   * @throws std::invalid_argument for invalid timing.
   * @throws std::bad_alloc if planner scratch or result storage cannot be
   * allocated. */
  static std::optional<Placement> find_at(const Timeline &plan,
                                          Request quantity, tdiff_t duration,
                                          sim_time_t start,
                                          ResourceContext context = {}) {
    auto result = plan.template find_at<Order, reusable_scratch>(
        quantity, duration, start);
    if constexpr (Observer::enabled) {
      context.timeline = &plan;
      Observer::observe({ResourceOperation::FitNow, ResourceScope::Timeline,
                         context, quantity, 0, 0, start, duration,
                         result.has_value()});
    }
    return result;
  }

  /** @brief Find the earliest exact identity-and-time placement.
   * @param[in] plan Borrowed caller-owned planner.
   * @param[in] quantity Number of resources requested.
   * @param[in] duration Duration; nonpositive means a point query.
   * @param[in] start Earliest requested start within the planner's domain.
   * @param[in] context Synchronous observation context, not retained.
   * @return Owning placement, or std::nullopt when the request is
   * unsatisfiable.
   * @throws std::invalid_argument for invalid timing.
   * @throws std::bad_alloc if planner scratch or result storage cannot be
   * allocated. */
  static std::optional<Placement> find_fit(const Timeline &plan,
                                           Request quantity, tdiff_t duration,
                                           sim_time_t start,
                                           ResourceContext context = {}) {
    auto result = plan.template earliest_fit<Order, reusable_scratch>(
        quantity, duration, start);
    if constexpr (Observer::enabled) {
      context.timeline = &plan;
      Observer::observe({ResourceOperation::FindFit, ResourceScope::Timeline,
                         context, quantity, 0, 0,
                         result ? result->start : start, duration,
                         result.has_value()});
    }
    return result;
  }

  /** @brief Copy exact identities into a speculative reservation.
   * @param[in,out] plan Caller-owned exact planner and derived filter.
   * @param[in] placement Admitted exact identities and start; caller retains
   * ownership.
   * @param[in] quantity Must equal placement's resource count.
   * @param[in] duration Reservation duration; nonpositive means no interval
   * change.
   * @param[in] context Synchronous metadata; job identity labels the interval.
   * @throws std::invalid_argument for count mismatch, invalid timing, or
   * conflict.
   * @throws std::bad_alloc if reservation storage cannot be allocated.
   * @details Planning does not charge live state or launch the job. */
  static void reserve(Timeline &plan, const Placement &placement,
                      Request quantity, tdiff_t duration,
                      ResourceContext context = {}) {
    if (placement.allocation.count() != quantity) {
      throw std::invalid_argument("placement count differs from request");
    }
    plan.reserve(placement, duration, context.job);
    if constexpr (Observer::enabled) {
      context.timeline = &plan;
      Observer::observe({ResourceOperation::Reserve, ResourceScope::Timeline,
                         context, quantity, 0, 0, placement.start, duration});
    }
  }

  /** @brief Transfer exact identities into a speculative reservation.
   * @param[in,out] plan Caller-owned exact planner and derived filter.
   * @param[in] placement Owning placement; masks transfer on successful
   * reservation.
   * @param[in] quantity Must equal placement's resource count.
   * @param[in] duration Reservation duration; nonpositive means no ownership
   * transfer.
   * @param[in] context Synchronous metadata; job identity labels the interval.
   * @throws std::invalid_argument for count mismatch, invalid timing, or
   * conflict.
   * @throws std::bad_alloc if reservation storage cannot be allocated. */
  static void reserve(Timeline &plan, Placement &&placement, Request quantity,
                      tdiff_t duration, ResourceContext context = {}) {
    const auto start = placement.start;
    if (placement.allocation.count() != quantity) {
      throw std::invalid_argument("placement count differs from request");
    }
    plan.reserve(std::move(placement), duration, context.job);
    if constexpr (Observer::enabled) {
      context.timeline = &plan;
      Observer::observe({ResourceOperation::Reserve, ResourceScope::Timeline,
                         context, quantity, 0, 0, start, duration});
    }
  }

  /** @brief Remove exact nodes from pass-local availability.
   * @param[in,out] available Caller-owned available-node bitmap.
   * @param[in] allocation Exact admitted nodes.
   * @param[in] context Synchronous observer metadata, not retained.
   * @throws std::invalid_argument for pool mismatch or unavailable nodes. */
  static void consume_available(State &available, const Allocation &allocation,
                                ResourceContext context = {}) {
    change<ResourceOperation::ProjectedStart>(available, allocation, context,
                                              ResourceScope::Available);
  }

  /** @brief Charge exact nodes to live allocated membership.
   * @param[in,out] allocated Caller-owned allocated-node bitmap.
   * @param[in] allocation Exact nodes committed to the job.
   * @param[in] context Synchronous observer metadata, not retained.
   * @throws std::invalid_argument for pool mismatch or an already allocated
   * node. */
  static void allocate(State &allocated, const Allocation &allocation,
                       ResourceContext context = {}) {
    change<ResourceOperation::Allocate>(allocated, allocation, context,
                                        ResourceScope::Allocated);
  }

  /** @brief Release the same exact nodes previously charged to this scope.
   * @param[in,out] occupied Caller-owned allocated or cleanup membership.
   * @param[in] allocation Exact nodes leaving the scope; each must be present.
   * @param[in] context Synchronous observer metadata, not retained.
   * @param[in] scope Accounting scope reported to the observer.
   * @throws std::invalid_argument for pool mismatch or a missing charged node.
   * @details The caller owns event timing and must release each charge once. */
  static void release(State &occupied, const Allocation &allocation,
                      ResourceContext context = {},
                      ResourceScope scope = ResourceScope::Allocated) {
    change<ResourceOperation::Release>(occupied, allocation, context, scope);
  }

  /** @brief Hold exact ended-job nodes during cleanup.
   * @param[in,out] releasing Caller-owned cleanup membership.
   * @param[in] allocation Exact nodes remaining unavailable until release.
   * @param[in] context Synchronous observer metadata, not retained.
   * @throws std::invalid_argument for pool mismatch or a duplicate cleanup
   * charge.
   * @details The caller separately removes the live allocation charge. */
  static void hold(State &releasing, const Allocation &allocation,
                   ResourceContext context = {}) {
    change<ResourceOperation::Hold>(releasing, allocation, context,
                                    ResourceScope::Releasing);
  }

private:
  /** @brief Validate and mutate one exact membership accounting scope.
   * @tparam Operation Compile-time accounting operation.
   * @param[in,out] state Caller-owned membership to update.
   * @param[in] allocation Exact identities to charge or release.
   * @param[in] context Synchronous observer context, not retained.
   * @param[in] scope Accounting scope reported to the observer.
   * @throws std::invalid_argument for mismatched domains or invalid ownership.
   * @details Disabled observation adds no population-count work. */
  template <ResourceOperation Operation>
  static void change(State &state, const Allocation &allocation,
                     ResourceContext context, ResourceScope scope) {
    constexpr bool removing = Operation == ResourceOperation::ProjectedStart ||
                              Operation == ResourceOperation::Release;
    if constexpr (removing) {
      if (!state.contains(allocation))
        throw std::invalid_argument("resources are not owned by this scope");
    } else {
      if (state.intersects(allocation))
        throw std::invalid_argument("resources already charged to this scope");
    }
    num_nodes_t before = 0;
    num_nodes_t quantity = 0;
    if constexpr (Observer::enabled) {
      before = static_cast<num_nodes_t>(state.count());
      quantity = static_cast<num_nodes_t>(allocation.count());
    }
    if constexpr (removing)
      state.remove(allocation);
    else
      state.add(allocation);
    if constexpr (Observer::enabled) {
      Observer::observe({Operation, scope, context, quantity, before,
                         static_cast<num_nodes_t>(state.count())});
    }
  }
};

} // namespace dr_evt
#endif
