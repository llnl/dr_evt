/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file placement_timeline.hpp
 * @brief Coarse dense filtering followed by exact identity-and-time searches.
 */
#ifndef DR_EVT_SIM_RESOURCE_PLACEMENT_TIMELINE_HPP
#define DR_EVT_SIM_RESOURCE_PLACEMENT_TIMELINE_HPP

#include "sim/resource/bitmap.hpp"
#include <cmath>
#include <functional>
#include <type_traits>
#include <variant>

namespace dr_evt {

/** @brief Owning exact placement result. */
struct BitmapPlacement {
  sim_time_t start;            ///< Exact start time, without bucket rounding.
  BitmapAllocation allocation; ///< Selected resource identities.
};

/** @brief Compile-time selector for the opt-in coarse-filter specialization.
 * @hideinitializer
 */
inline constexpr auto EnableCoarseFilter = [] { return true; };
/** @brief Default compile-time selector for exact-only searches without grid
 * storage.
 * @hideinitializer
 */
inline constexpr auto DisableCoarseFilter = [] { return false; };

namespace detail {
/** @brief Empty storage when the coarse filter is disabled.
 * @tparam Enabled Whether the selected timeline needs a coarse cache. */
template <bool Enabled> struct PlacementFilterStorage {};

/** @brief Timeline-owned cache only for the enabled specialization. */
template <> struct PlacementFilterStorage<true> {
  std::vector<std::uint64_t> words; ///< Row-major derived free unions.
  bool dirty = true; ///< Invalidated after successful exact interval insertion.
};
} // namespace detail

/**
 * @brief Exact reservation owner with an optional optimistic dense coarse
 * filter.
 * @tparam CoarseFilter Captureless constexpr lambda returning bool; evaluated
 * at compile time to select filtering behavior and cache storage; exact-only
 * searches are the default.
 * @details Exact intervals are authoritative. Future reservations own their
 * dense masks; live allocations are borrowed from the caller's existing owner.
 * The caller must keep borrowed allocations immutable and alive throughout the
 * timeline's lifetime. There is no second running-job collection.
 * The derived coarse grid has 64 contiguous rows. Each row is the union of
 * free identities over its time bucket, so it can reject impossible requests
 * but cannot prove a fit. Every accepted fit uses the exact intervals.
 * Successful interval changes invalidate the grid; the next query rebuilds it.
 * The grid and exact intervals share this timeline's lifetime. Queries may
 * mutate the enabled cache and are not thread-safe on one shared instance.
 * Disabled timelines omit the cache and its invalidation/rebuild operations.
 */
template <auto CoarseFilter = DisableCoarseFilter>
class BasicPlacementTimeline {
  static_assert(std::is_same_v<decltype(CoarseFilter()), bool>,
                "coarse filter selector must return bool");

public:
  /// Compile-time filter choice; no runtime flag is stored.
  static constexpr bool coarse_filter_enabled = CoarseFilter();

  /** @brief Exact half-open interval and its allocation ownership. */
  struct Interval {
    sim_time_t start;  ///< Inclusive blocked start.
    sim_time_t finish; ///< Exclusive blocked finish.
    job_no_t job;      ///< Caller job identity, or max() when anonymous.
    /// Owned reservation mask or borrowed live allocation, never both.
    std::variant<BitmapAllocation,
                 std::reference_wrapper<const BitmapAllocation>>
        masks;

    /** @brief Borrow this interval's exact identities.
     * @return Allocation view valid for the interval's lifetime; a live owner
     * must additionally outlive the timeline. */
    const BitmapAllocation &allocation() const noexcept {
      if (const auto *owned = std::get_if<BitmapAllocation>(&masks)) {
        return *owned;
      }
      return std::get<std::reference_wrapper<const BitmapAllocation>>(masks)
          .get();
    }
  };

private:
  static constexpr size_t buckets = 64;
  Bitmap m_pool;
  sim_time_t m_origin;
  std::vector<Interval> m_intervals; // Ordered by finish; owns future masks.
  [[no_unique_address]] mutable detail::PlacementFilterStorage<
      coarse_filter_enabled> m_filter;

  /** @brief Validate an exact query and compute its endpoint.
   * @param[in] start Exact start within the timeline domain.
   * @param[in] duration Duration; nonpositive values query one instant.
   * @return Exact endpoint, equal to start for a point query.
   * @throws std::invalid_argument for nonfinite, out-of-domain, overflowing,
   * or unrepresentably small positive intervals. */
  sim_time_t endpoint(sim_time_t start, tdiff_t duration) const {
    if (!std::isfinite(start) || start < m_origin || !std::isfinite(duration)) {
      throw std::invalid_argument("invalid placement interval");
    }
    const auto end = duration > 0 ? start + duration : start;
    if (!std::isfinite(end) || !std::isfinite(end - m_origin) ||
        (duration > 0 && end <= start)) {
      throw std::invalid_argument("unrepresentable placement interval");
    }
    return end;
  }

  /** @brief Test exact interval overlap, including point queries.
   * @param[in] interval Existing blocked interval.
   * @param[in] start Query start.
   * @param[in] end Query endpoint; start equals end for a point query.
   * @return true when the blocked interval affects this query. */
  static bool overlaps(const Interval &interval, sim_time_t start,
                       sim_time_t end) noexcept {
    return end == start ? interval.start <= start && start < interval.finish
                        : start < interval.finish && interval.start < end;
  }

  /** @brief Compute one coarse boundary from authoritative exact endpoints.
   * @param[in] index Boundary index from zero through buckets.
   * @return Boundary timestamp; it is never returned as a placement result. */
  sim_time_t boundary(size_t index) const noexcept {
    if (index == 0)
      return m_origin;
    if (index == buckets)
      return m_intervals.back().finish;
    return m_origin + (m_intervals.back().finish - m_origin) *
                          (static_cast<double>(index) / buckets);
  }

  /** @brief Lazily derive potentially free resources in each coarse bucket.
   * @throws std::bad_alloc if grid or temporary event storage cannot be
   * allocated.
   * @details Temporary event references point into stable intervals only for
   * this rebuild. They are destroyed before any interval mutation can occur.
   * All equal-time releases/starts are applied before observing the next state.
   * A failed rebuild leaves the cache dirty and exact state unchanged. */
  void rebuild_coarse() const {
    if constexpr (coarse_filter_enabled) {
      if (!m_filter.dirty || m_intervals.empty())
        return;
      struct Event {
        sim_time_t time;
        const BitmapAllocation *allocation;
        bool release;
      };
      std::vector<Event> events;
      events.reserve(m_intervals.size() * 2);
      for (const auto &interval : m_intervals) {
        events.push_back({interval.start, &interval.allocation(), false});
        events.push_back({interval.finish, &interval.allocation(), true});
      }
      std::sort(events.begin(), events.end(),
                [](const Event &left, const Event &right) {
                  return left.time != right.time ? left.time < right.time
                                                 : left.release > right.release;
                });
      const size_t width = m_pool.words().size();
      m_filter.words.assign(buckets * width, 0);
      Bitmap free = m_pool;
      size_t cursor = 0;
      const auto advance = [&]() {
        const auto time = events[cursor].time;
        do {
          const auto &event = events[cursor++];
          if (event.release)
            free.add(*event.allocation);
          else
            free.remove(*event.allocation);
        } while (cursor < events.size() && events[cursor].time == time);
      };
      for (size_t bucket = 0; bucket < buckets; ++bucket) {
        const auto left = boundary(bucket), right = boundary(bucket + 1);
        while (cursor < events.size() && events[cursor].time <= left)
          advance();
        if (left == right)
          continue; // Rounded empty buckets must not reject fits.
        const auto merge = [&]() {
          auto row = m_filter.words.data() + bucket * width;
          for (size_t word = 0; word < width; ++word)
            row[word] |= free.words()[word];
        };
        merge();
        while (cursor < events.size() && events[cursor].time < right) {
          advance();
          merge();
        }
      }
      m_filter.dirty = false;
    }
  }

  /** @brief Intersect optimistic free unions for all touched coarse buckets.
   * @param[in,out] candidates Caller scratch, initially equal to the pool.
   * @param[in] start Exact query start.
   * @param[in] end Exact query endpoint.
   * @throws std::bad_alloc if a dirty grid cannot be rebuilt.
   * @details Times beyond the last interval require no coarse row. No time
   * rounding is used to accept or reject an exact placement. */
  void filter(Bitmap &candidates, sim_time_t start, sim_time_t end) const {
    if constexpr (coarse_filter_enabled) {
      if (m_intervals.empty())
        return;
      rebuild_coarse();
      const size_t width = m_pool.words().size();
      for (size_t bucket = 0; bucket < buckets; ++bucket) {
        const auto left = boundary(bucket), right = boundary(bucket + 1);
        const bool touched =
            left < right && (start == end ? left <= start && start < right
                                          : start < right && left < end);
        if (touched) {
          candidates.intersect_words(
              std::span(m_filter.words).subspan(bucket * width, width));
        }
      }
    }
  }

  /** @brief Remove identities blocked anywhere in the exact query interval.
   * @param[in,out] candidates Optimistic candidates to refine.
   * @param[in] start Exact query start.
   * @param[in] end Exact query endpoint.
   * @details Sparse temporal records carry dense contiguous resource words. */
  void refine(Bitmap &candidates, sim_time_t start, sim_time_t end) const {
    auto first =
        std::upper_bound(m_intervals.begin(), m_intervals.end(), start,
                         [](sim_time_t time, const Interval &interval) {
                           return time < interval.finish;
                         });
    for (; first != m_intervals.end(); ++first) {
      if (overlaps(*first, start, end))
        candidates.remove(first->allocation());
    }
  }

  /** @brief Refine and select using a caller-local reusable candidate buffer.
   * @tparam Order Compile-time selection order.
   * @param[in,out] candidates Owned query scratch; refreshed from inventory.
   * @param[in] quantity Requested identities.
   * @param[in] duration Requested duration; nonpositive means a point query.
   * @param[in] start Exact candidate start.
   * @return true with selected membership, false when no fit exists.
   * @throws std::invalid_argument for invalid query timing.
   * @throws std::bad_alloc if scratch or filter storage cannot be allocated.
   * @details Scratch never survives this public query or becomes a cache. */
  template <NodeOrder Order>
  bool select_at(Bitmap &candidates, num_nodes_t quantity, tdiff_t duration,
                 sim_time_t start) const {
    const auto end = endpoint(start, duration);
    candidates = m_pool;
    if constexpr (coarse_filter_enabled)
      filter(candidates, start, end);
    if (candidates.count() < quantity)
      return false;
    refine(candidates, start, end);
    return candidates.template retain_first<Order>(quantity);
  }

  /** @brief Append and order an exact interval after admission.
   * @tparam Masks Owning bitmap or borrowed bitmap reference wrapper.
   * @param[in] start Inclusive blocked start.
   * @param[in] end Exclusive blocked finish.
   * @param[in] job Caller job identity.
   * @param[in] masks Allocation to own or borrow, as selected by its type.
   * @throws std::bad_alloc if interval or owning mask storage cannot be
   * allocated.
   * @details Vector growth precedes emplacement's ownership transfer. Interval
   * moves are noexcept. Cache invalidation follows successful insertion. */
  template <typename Masks>
  void insert(sim_time_t start, sim_time_t end, job_no_t job, Masks &&masks) {
    const size_t index =
        std::upper_bound(m_intervals.begin(), m_intervals.end(), end,
                         [](sim_time_t time, const Interval &interval) {
                           return time < interval.finish;
                         }) -
        m_intervals.begin();
    m_intervals.emplace_back(start, end, job, std::forward<Masks>(masks));
    if (index + 1 != m_intervals.size()) {
      auto appended = std::move(m_intervals.back());
      std::move_backward(m_intervals.begin() + index, m_intervals.end() - 1,
                         m_intervals.end());
      m_intervals[index] = std::move(appended);
    }
    if constexpr (coarse_filter_enabled)
      m_filter.dirty = true;
  }

  /** @brief Check specific identities against exact interval commitments.
   * @param[in] placement Already selected resource identities and start.
   * @param[in] end Exact interval endpoint.
   * @throws std::invalid_argument if domains differ, resources are ineligible,
   * or an overlapping interval already blocks a selected identity. */
  void admit(const BitmapPlacement &placement, sim_time_t end) const {
    if (!m_pool.contains(placement.allocation)) {
      throw std::invalid_argument("placement includes ineligible resources");
    }
    for (const auto &interval : m_intervals) {
      if (overlaps(interval, placement.start, end) &&
          interval.allocation().intersects(placement.allocation)) {
        throw std::invalid_argument(
            "placement conflicts with an exact interval");
      }
    }
  }

public:
  /** @brief Construct a planner over eligible identities.
   * @param[in] pool Owning eligible-resource bitmap, transferred into the
   * planner.
   * @param[in] origin Earliest supported query time.
   * @throws std::invalid_argument if origin is nonfinite. */
  BasicPlacementTimeline(Bitmap pool, sim_time_t origin)
      : m_pool(std::move(pool)), m_origin(origin) {
    if (!std::isfinite(origin))
      throw std::invalid_argument("nonfinite timeline origin");
  }

  /** @brief Copy exact planning state without copying its derived filter.
   * @param[in] other Planner to copy. Borrowed live owners must outlive both
   * copies.
   * @throws std::bad_alloc if exact state cannot be copied. */
  BasicPlacementTimeline(const BasicPlacementTimeline &other)
      : m_pool(other.m_pool), m_origin(other.m_origin),
        m_intervals(other.m_intervals) {}
  /** @brief Transfer exact state, borrowed views, and cache ownership.
   * @param[in] other Planner to move; it must not be queried afterward. */
  BasicPlacementTimeline(BasicPlacementTimeline &&other) noexcept = default;
  /** @brief Disallow assignment that could obscure borrowed-owner lifetimes.
   * @param[in] other Unused source; assignment is deleted.
   * @return No result because this operation is deleted. */
  BasicPlacementTimeline &
  operator=(const BasicPlacementTimeline &other) = delete;

  /** @brief Seed an empty planner from an existing running-record range.
   * @tparam Range Borrowed iterable of existing running records.
   * @tparam Describe Callable returning an Interval with a borrowed mask.
   * @param[in] records Existing records; the range itself is not retained.
   * @param[in] describe Maps a record to its exact release and borrowed
   * allocation.
   * @throws std::invalid_argument if the planner is nonempty, a mask is owned,
   * timing or domains are invalid, or live allocations overlap.
   * @throws std::bad_alloc if interval or temporary validation storage cannot
   * be allocated.
   * @details Each interval must begin at this planner's origin. Allocations
   * must remain immutable and alive through this planner and its copies. A
   * temporary union validates disjointness; interval metadata sorts once and
   * transfers into the planner only after the complete batch is admitted.
   * Failure leaves it unchanged. Exceptions from the descriptor propagate
   * before ownership transfer. */
  template <typename Range, typename Describe>
  void seed_running(Range &&records, Describe describe) {
    if (!m_intervals.empty())
      throw std::invalid_argument("batch seed requires an empty timeline");
    std::vector<Interval> initial;
    if constexpr (requires { records.size(); })
      initial.reserve(records.size());
    Bitmap occupied(m_pool.capacity());
    for (const auto &record : records) {
      auto interval = describe(record);
      if (interval.start != m_origin ||
          !std::holds_alternative<
              std::reference_wrapper<const BitmapAllocation>>(interval.masks))
        throw std::invalid_argument(
            "batch interval must borrow a live allocation at the origin");
      const auto end = endpoint(m_origin, interval.finish - m_origin);
      if (end <= m_origin)
        throw std::invalid_argument("running release must be in the future");
      interval.finish = end;
      const auto &allocation = interval.allocation();
      if (!m_pool.contains(allocation) || occupied.intersects(allocation))
        throw std::invalid_argument(
            "invalid or overlapping running allocation");
      if (allocation.count() == 0)
        continue;
      occupied.add(allocation);
      initial.push_back(std::move(interval));
    }
    std::sort(initial.begin(), initial.end(),
              [](const Interval &left, const Interval &right) {
                return left.finish != right.finish ? left.finish < right.finish
                                                   : left.job < right.job;
              });
    m_intervals = std::move(initial);
    if constexpr (coarse_filter_enabled)
      m_filter.dirty = true;
  }

  /** @brief Borrow an existing running allocation for this planning pass.
   * @param[in] allocation Immutable live allocation; caller retains ownership.
   * @param[in] finish Forecast release strictly later than the origin.
   * @param[in] job Caller job identity, or max() for an anonymous interval.
   * @throws std::invalid_argument for invalid timing, pool mismatch, or
   * conflict.
   * @throws std::bad_alloc if interval metadata cannot be allocated.
   * @details The allocation must remain alive and unchanged until this timeline
   * and any copies are destroyed. Its job metadata and lifecycle are unchanged.
   * Empty allocations are validated but do not create an interval. */
  void borrow_running(const BitmapAllocation &allocation, sim_time_t finish,
                      job_no_t job = std::numeric_limits<job_no_t>::max()) {
    const auto end = endpoint(m_origin, finish - m_origin);
    if (end <= m_origin)
      throw std::invalid_argument("running release must be in the future");
    // Admission borrows the supplied bitmap; it does not create an ID side map.
    if (!m_pool.contains(allocation))
      throw std::invalid_argument("ineligible running resources");
    for (const auto &interval : m_intervals) {
      if (overlaps(interval, m_origin, end) &&
          interval.allocation().intersects(allocation)) {
        throw std::invalid_argument(
            "running allocation overlaps an exact interval");
      }
    }
    if (allocation.count() != 0)
      insert(m_origin, end, job, std::cref(allocation));
  }

  /** @brief Test the optimistic coarse rejection filter.
   * @param[in] quantity Number of resources required.
   * @param[in] duration Requested duration; nonpositive means a point query.
   * @param[in] start Exact query start.
   * @return false proves infeasibility; true requires exact refinement. When
   * disabled, only pool cardinality is checked and no cache is accessed.
   * @throws std::invalid_argument for invalid query timing.
   * @throws std::bad_alloc if scratch or filter storage cannot be allocated. */
  bool coarse_possible(num_nodes_t quantity, tdiff_t duration,
                       sim_time_t start) const {
    const auto end = endpoint(start, duration);
    if constexpr (coarse_filter_enabled) {
      Bitmap candidates = m_pool;
      filter(candidates, start, end);
      return candidates.count() >= quantity;
    } else {
      return m_pool.count() >= quantity;
    }
  }

  /** @brief Find selected identities at an exact start, without changing the
   * plan.
   * @tparam Order Compile-time resource selection order.
   * @param[in] quantity Resources requested; zero returns an empty allocation.
   * @param[in] duration Requested duration; nonpositive means a point query.
   * @param[in] start Exact query start.
   * @return Exact owning placement or std::nullopt when no identities fit.
   * @throws std::invalid_argument for invalid query timing.
   * @throws std::bad_alloc if result, scratch, or filter storage cannot be
   * allocated. */
  template <NodeOrder Order = NodeOrder::LowIdFirst>
  std::optional<BitmapPlacement> find_at(num_nodes_t quantity, tdiff_t duration,
                                         sim_time_t start) const {
    Bitmap candidates;
    if (!select_at<Order>(candidates, quantity, duration, start))
      return std::nullopt;
    return BitmapPlacement{start, std::move(candidates)};
  }

  /** @brief Find the earliest exact placement after a requested start.
   * @tparam Order Compile-time resource selection order.
   * @param[in] quantity Number of resources requested.
   * @param[in] duration Requested duration; nonpositive means a point query.
   * @param[in] start Earliest exact start.
   * @return Earliest owning placement, or std::nullopt for an unsatisfiable
   * request.
   * @throws std::invalid_argument for invalid or unrepresentable query timing.
   * @throws std::bad_alloc if result, scratch, or filter storage cannot be
   * allocated.
   * @details Availability can improve only at exact interval ends. Coarse
   * boundaries are never used as candidate starts or as final admission tests.
   */
  template <NodeOrder Order = NodeOrder::LowIdFirst>
  std::optional<BitmapPlacement>
  earliest_fit(num_nodes_t quantity, tdiff_t duration, sim_time_t start) const {
    endpoint(start, duration);
    if (quantity > m_pool.count())
      return std::nullopt;
    Bitmap candidates;
    if (select_at<Order>(candidates, quantity, duration, start))
      return BitmapPlacement{start, std::move(candidates)};
    sim_time_t previous = start;
    for (const auto &interval : m_intervals) {
      if (interval.finish <= previous)
        continue;
      previous = interval.finish;
      if (select_at<Order>(candidates, quantity, duration, previous))
        return BitmapPlacement{previous, std::move(candidates)};
    }
    return std::nullopt;
  }

  /** @brief Check an exact interval's identity feasibility.
   * @param[in] start Requested exact start.
   * @param[in] quantity Number of resources requested.
   * @param[in] duration Requested duration; nonpositive means a point query.
   * @return true when one fixed set of identities fits throughout the interval.
   * @throws std::invalid_argument for invalid query timing.
   * @throws std::bad_alloc if scratch or filter storage cannot be allocated. */
  bool can_fit(sim_time_t start, num_nodes_t quantity, tdiff_t duration) const {
    const auto end = endpoint(start, duration);
    Bitmap candidates = m_pool;
    if constexpr (coarse_filter_enabled)
      filter(candidates, start, end);
    if (candidates.count() < quantity)
      return false;
    refine(candidates, start, end);
    return candidates.count() >= quantity;
  }

  /** @brief Copy an admitted future reservation into exact planning state.
   * @param[in] placement Exact start and resource identities to reserve.
   * @param[in] duration Reserved duration; nonpositive duration does nothing.
   * @param[in] job Caller job identity, or max() when anonymous.
   * @throws std::invalid_argument for invalid timing, mismatched pool, or
   * conflict.
   * @throws std::bad_alloc if interval or mask storage cannot be allocated.
   * @details The caller retains its placement; live allocation state is
   * unchanged. Empty allocations are validated but do not create an interval.
   */
  void reserve(const BitmapPlacement &placement, tdiff_t duration,
               job_no_t job = std::numeric_limits<job_no_t>::max()) {
    const auto end = endpoint(placement.start, duration);
    if (end == placement.start)
      return;
    admit(placement, end);
    if (placement.allocation.count() == 0)
      return;
    insert(placement.start, end, job, placement.allocation);
  }

  /** @brief Transfer an admitted reservation's masks into exact planning state.
   * @param[in] placement Owning placement; masks move on successful insertion.
   * @param[in] duration Reserved duration; nonpositive duration does nothing.
   * @param[in] job Caller job identity, or max() when anonymous.
   * @throws std::invalid_argument for invalid timing, mismatched pool, or
   * conflict.
   * @throws std::bad_alloc if interval storage cannot be allocated.
   * @details Validation happens before ownership transfer. Empty allocations
   * are validated but do not create an interval or transfer ownership. */
  void reserve(BitmapPlacement &&placement, tdiff_t duration,
               job_no_t job = std::numeric_limits<job_no_t>::max()) {
    const auto end = endpoint(placement.start, duration);
    if (end == placement.start)
      return;
    admit(placement, end);
    if (placement.allocation.count() == 0)
      return;
    insert(placement.start, end, job, std::move(placement.allocation));
  }

  /** @brief Borrow exact intervals for inspection or trace generation.
   * @return Read-only view invalidated by reservation insertion or destruction.
   * @details Borrowed live allocation owners must additionally remain valid. */
  std::span<const Interval> intervals() const noexcept { return m_intervals; }
};

/// Default timeline performs exact-only searches without coarse grid storage.
using PlacementTimeline = BasicPlacementTimeline<>;

} // namespace dr_evt
#endif
