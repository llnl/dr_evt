/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/
/** @file reservation_profile.hpp
 * @brief Exact-identity planning for periodic backfill that books only waiting
 * jobs, ported from scripts/python_periodic_backfill_placement_scheduler.py.
 */
#ifndef DR_EVT_SIM_RESOURCE_RESERVATION_PROFILE_HPP
#define DR_EVT_SIM_RESOURCE_RESERVATION_PROFILE_HPP
#include "sim/resource/bitmap.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace dr_evt {

/** @brief Contiguous time segments [begin, end) holding identities still free
 * of bookings made during one backfill cycle.
 * @details The profile stores only pass-local waiting-job bookings. Running
 * allocations are represented separately by ReleaseRecord and contribute
 * identities to grouped_release_fit() when their requested limits expire. */
class ReservationProfile {
public:
  /** @brief One profile segment. */
  struct Segment {
    sim_time_t begin; ///< Inclusive segment start.
    sim_time_t end;   ///< Exclusive segment end.
    Bitmap free;      ///< Identities not booked in this segment.
  };

  /** @brief Start with one segment covering [now, until).
   * @param[in] pool Eligible identities.
   * @param[in] now Cycle time.
   * @param[in] until Planning window end. */
  ReservationProfile(const Bitmap &pool, sim_time_t now, sim_time_t until)
      : m_pool(pool) {
    m_segments.push_back({now, until, pool});
  }

  /** @brief Remove identities from every segment within [begin, end).
   * @param[in] begin Requested booking start; clipped to the profile origin.
   * @param[in] end Requested booking end. An empty or reversed interval is
   * normalized to one time unit after the clipped start.
   * @param[in] nodes Identities to remove during the normalized interval.
   * @throws std::invalid_argument if bitmap domains differ.
   * @throws std::bad_alloc if splitting the profile requires allocation.
   * @details Splits at both endpoints, then coalesces at most one adjacent
   * pair with identical free sets, starting just before the booking. A booking
   * beginning beyond the represented profile has no effect. */
  void book(sim_time_t begin, sim_time_t end, const Bitmap &nodes) {
    auto &segs = m_segments;
    begin = std::max(begin, segs.front().begin);
    end = std::max(end, begin + 1);
    size_t before = 0, k = 0;
    bool found = false;
    while (k < segs.size()) {
      if (segs[k].end > begin) {
        Segment tail{begin, segs[k].end, segs[k].free};
        segs[k].end = begin;
        segs.insert(segs.begin() + static_cast<std::ptrdiff_t>(k + 1),
                    std::move(tail));
        found = true;
        break;
      }
      if (segs[k].end == begin) {
        found = true;
        break;
      }
      before = k;
      ++k;
    }
    std::ptrdiff_t after = -1;
    while (found) {
      ++k;
      if (k >= segs.size())
        break;
      if (end < segs[k].end) {
        Segment tail{end, segs[k].end, segs[k].free};
        segs[k].end = end;
        segs.insert(segs.begin() + static_cast<std::ptrdiff_t>(k + 1),
                    std::move(tail));
      }
      segs[k].free.remove(nodes);
      if (end == segs[k].end) {
        if (k + 1 < segs.size())
          after = static_cast<std::ptrdiff_t>(k + 1);
        break;
      }
    }
    for (auto at = static_cast<std::ptrdiff_t>(before); at != after;) {
      const auto next = static_cast<size_t>(at + 1);
      if (next >= segs.size())
        break;
      if (segs[static_cast<size_t>(at)].free != segs[next].free) {
        at = static_cast<std::ptrdiff_t>(next);
        continue;
      }
      segs[static_cast<size_t>(at)].end = segs[next].end;
      segs.erase(segs.begin() + static_cast<std::ptrdiff_t>(next));
      break;
    }
  }

  /** @brief Test whether a segment overlapping [begin, end) lacks a node.
   * @param[in] nodes Identities whose availability is required.
   * @param[in] begin Inclusive interval start.
   * @param[in] end Exclusive interval end.
   * @return true if any overlapping segment lacks a required identity.
   * @throws std::invalid_argument if bitmap domains differ. */
  bool collides(const Bitmap &nodes, sim_time_t begin, sim_time_t end) const {
    return std::ranges::any_of(m_segments, [&](const Segment &seg) {
      return seg.end > begin && seg.begin < end && !seg.free.contains(nodes);
    });
  }

  /** @brief Intersect free sets over [earliest, finish], end inclusive.
   * @param[in] earliest Earliest candidate start.
   * @param[in] finish Requested endpoint, which participates in feasibility.
   * @return Usable identities and the next profile transition worth retrying;
   * zero means no later represented transition can improve the answer.
   * @throws std::invalid_argument if internal bitmap domains differ.
   * @throws std::bad_alloc if bitmap scratch allocation fails. */
  std::pair<Bitmap, sim_time_t> usable(sim_time_t earliest,
                                       sim_time_t finish) const {
    const auto &segs = m_segments;
    Bitmap result = m_pool;
    sim_time_t retry_at = 0;
    // Segment ends are ordered; those ending by earliest contribute nothing.
    const auto first = std::ranges::partition_point(
        segs, [earliest](const Segment &seg) { return seg.end <= earliest; });
    for (auto k = static_cast<size_t>(first - segs.begin()); k < segs.size();
         ++k) {
      const auto &seg = segs[k];
      // Every free set is a subset of m_pool, so the next segment's usable
      // identities are exactly its free set.
      if (k + 1 < segs.size() && retry_at == 0 &&
          !result.intersection_contains(seg.free, segs[k + 1].free))
        retry_at = seg.end;
      if (seg.begin <= finish) {
        result.intersect_with(seg.free);
      } else {
        if (retry_at == 0 && k + 2 < segs.size())
          retry_at = segs[k + 1].end;
        break;
      }
    }
    return {std::move(result), retry_at};
  }

  /** @brief Intersect identities with free sets over [earliest, finish].
   * @param[in] base Identities to restrict, in the profile's domain.
   * @param[in] earliest Inclusive window start.
   * @param[in] finish Requested endpoint, which participates in feasibility.
   * @return base restricted to identities free in every overlapping segment.
   * @throws std::invalid_argument if bitmap domains differ.
   * @details Uses the same overlap rule as usable() without computing a
   * retry boundary. */
  Bitmap free_during(Bitmap base, sim_time_t earliest,
                     sim_time_t finish) const {
    const auto &segs = m_segments;
    const auto first = std::ranges::partition_point(
        segs, [earliest](const Segment &seg) { return seg.end <= earliest; });
    for (auto it = first; it != segs.end() && it->begin <= finish; ++it)
      base.intersect_with(it->free);
    return base;
  }

  /** @brief Borrow segments for inspection.
   * @return Reference valid until the next non-const operation or destruction.
   */
  const std::vector<Segment> &segments() const noexcept { return m_segments; }

  /** @brief Borrow the complete eligible identity domain.
   * @return Reference valid for the profile's lifetime. */
  const Bitmap &pool() const noexcept { return m_pool; }

private:
  Bitmap m_pool;                   ///< Complete eligible identity domain.
  std::vector<Segment> m_segments; ///< Ordered pass-local free profiles.
};

/** @brief A running job's limit end and identities, as seen by planning. */
struct ReleaseRecord {
  sim_time_t limit_end; ///< Start plus planned duration.
  Bitmap nodes;         ///< Exact identities held.
};

/** @brief Repeated ReservationProfile::usable() queries over a forward-moving
 * window.
 * @details Returns exactly what usable() returns. When neither earliest nor
 * finish decreases between calls, the window of segments only slides
 * forward, and its intersection is kept with two stacks: suffix rows for the
 * older segments and one running row for the newer ones. Each segment enters
 * and leaves once, so a query costs a constant number of row operations
 * amortized, plus the scan for the retry boundary, which stops at the first
 * useful boundary. A window that moves backward is rebuilt from scratch. The
 * profile must outlive this object and stay unchanged while it is used. */
class ProfileWindow {
public:
  /** @brief Start with an empty window.
   * @param[in] profile Profile to query. */
  explicit ProfileWindow(const ReservationProfile &profile)
      : m_profile(profile), m_back(profile.pool()) {}

  /** @brief Same result as ReservationProfile::usable(earliest, finish).
   * @param[in] earliest Earliest candidate start.
   * @param[in] finish Requested endpoint, which participates in feasibility.
   * @return Usable identities and the next profile transition worth retrying;
   * zero means no later represented transition can improve the answer.
   * @throws std::invalid_argument if internal bitmap domains differ.
   * @throws std::bad_alloc if row storage cannot grow. */
  std::pair<Bitmap, sim_time_t> usable(sim_time_t earliest, sim_time_t finish) {
    const auto &segs = m_profile.segments();
    if (earliest < m_earliest || finish < m_finish)
      reset(0);
    m_earliest = earliest;
    m_finish = finish;
    // Window [first, last): segments ending after earliest that begin by
    // finish.
    size_t first = m_lo;
    while (first < segs.size() && segs[first].end <= earliest)
      ++first;
    if (first > m_hi)
      reset(first);
    while (m_hi < segs.size() && segs[m_hi].begin <= finish)
      m_back.intersect_with(segs[m_hi++].free);
    if (first > m_lo) {
      m_lo = first;
      if (m_lo >= m_mid)
        rebuild_front();
    }
    Bitmap result = m_back;
    if (m_lo < m_mid)
      result.intersect_words(row(m_lo));
    // Same boundary rule as usable(): the first segment after which the next
    // segment frees an identity, or the fallback past the window's end. Until
    // the boundary is found, each next free set is within the running
    // intersection, so that intersection equals the current free set.
    sim_time_t retry_at = 0;
    for (size_t k = first; k < segs.size(); ++k) {
      if (k + 1 < segs.size() && !segs[k].free.contains(segs[k + 1].free)) {
        retry_at = segs[k].end;
        break;
      }
      if (segs[k].begin > finish) {
        if (k + 2 < segs.size())
          retry_at = segs[k + 1].end;
        break;
      }
    }
    return {std::move(result), retry_at};
  }

private:
  /** @brief Empty the window at a segment index.
   * @param[in] at Segment index where the empty window starts. */
  void reset(size_t at) {
    m_lo = m_mid = m_hi = at;
    m_back = m_profile.pool();
  }

  /** @brief Move [m_lo, m_hi) into suffix rows and empty the running row. */
  void rebuild_front() {
    const auto &segs = m_profile.segments();
    const auto &pool = m_profile.pool().words();
    const size_t width = pool.size();
    m_mid = m_hi;
    m_base = m_lo;
    m_front.resize((m_mid - m_lo) * width);
    for (size_t index = m_mid; index-- > m_lo;) {
      const auto words = segs[index].free.words();
      const auto next = index + 1 < m_mid ? row(index + 1) : pool;
      auto *out = m_front.data() + (index - m_base) * width;
      for (size_t word = 0; word < width; ++word)
        out[word] = next[word] & words[word];
    }
    m_back = m_profile.pool();
  }

  /** @brief Borrow the suffix row for one segment.
   * @param[in] index Segment index in [m_base, m_mid).
   * @return Pool intersected with segments [index, m_mid); valid until the
   * next rebuild. */
  std::span<const std::uint64_t> row(size_t index) const {
    const size_t width = m_profile.pool().words().size();
    return std::span(m_front).subspan((index - m_base) * width, width);
  }

  const ReservationProfile &m_profile; ///< Borrowed, unchanged profile.
  size_t m_lo = 0;   ///< Window start; suffix rows cover [m_lo, m_mid).
  size_t m_mid = 0;  ///< End of suffix rows; m_back covers [m_mid, m_hi).
  size_t m_hi = 0;   ///< Window end.
  size_t m_base = 0; ///< Segment index of the first stored suffix row.
  std::vector<std::uint64_t> m_front; ///< Suffix rows from m_base.
  Bitmap m_back;                      ///< Pool intersected with [m_mid, m_hi).
  sim_time_t m_earliest = -std::numeric_limits<sim_time_t>::infinity();
  sim_time_t m_finish = -std::numeric_limits<sim_time_t>::infinity();
};

/** @brief Initial clock-aligned release-group width in seconds. */
inline constexpr tdiff_t release_group_initial = 30;
/** @brief Widening threshold cap in seconds. */
inline constexpr tdiff_t release_group_cap = 365.0 * 86400.0;

/** @brief Cumulative unions of running allocations in limit-end order.
 * @details Row i is the union of the first i records. Rows are extended on
 * demand, so repeated queries with growing prefixes during one planning call
 * cost one union per record. The borrowed records must outlive this object
 * and stay unchanged while it is used. */
class ReleasePrefix {
public:
  /** @brief Start with only the empty prefix.
   * @param[in] running Running records sorted by nondecreasing limit end.
   * @param[in] width Word count of the record bitmaps. */
  ReleasePrefix(const std::vector<ReleaseRecord> &running, size_t width)
      : m_running(running), m_width(width), m_rows(width, 0) {}

  /** @brief Borrow the union of the first count records.
   * @param[in] count Prefix length, at most the record count.
   * @return Words valid until the next call or destruction.
   * @throws std::bad_alloc if row storage cannot grow. */
  std::span<const std::uint64_t> before(size_t count) {
    if (m_width == 0)
      return {};
    for (size_t built = m_rows.size() / m_width - 1; built < count; ++built) {
      const auto words = m_running[built].nodes.words();
      m_rows.resize(m_rows.size() + m_width);
      const auto previous =
          m_rows.end() - 2 * static_cast<std::ptrdiff_t>(m_width);
      const auto row = m_rows.end() - static_cast<std::ptrdiff_t>(m_width);
      for (size_t word = 0; word < m_width; ++word)
        row[static_cast<std::ptrdiff_t>(word)] =
            previous[static_cast<std::ptrdiff_t>(word)] | words[word];
    }
    return std::span(m_rows).subspan(count * m_width, m_width);
  }

private:
  const std::vector<ReleaseRecord> &m_running; ///< Borrowed sorted records.
  size_t m_width;                              ///< Words per row.
  std::vector<std::uint64_t> m_rows;           ///< Built prefix unions.
};

/** @brief Select nodes by releasing running jobs in widening groups.
 * @tparam Select Callable returning exactly quantity identities from a
 * candidate bitmap known to contain enough identities.
 * @param[in] running Running jobs sorted by nondecreasing limit end.
 * @param[in] idle Identities idle now.
 * @param[in] usable Identities free of bookings throughout the window.
 * @param[in] quantity Requested node count.
 * @param[in] now Cycle time.
 * @param[in] earliest Release cutoff already available to this attempt.
 * @param[in] bound Nonzero stops releasing jobs whose limit ends after it;
 * zero is unbounded.
 * @param[in] select Node-selection callable, invoked only after feasibility.
 * @param[in,out] prefix Optional cumulative unions of running; when given,
 * the releases by earliest are added as one union instead of per record.
 * @return Latest selected release time and allocation, or std::nullopt when
 * the bounded releases cannot satisfy the request. The returned time may be
 * before earliest and is clamped by plan_profile_job().
 * @throws std::invalid_argument if bitmap domains differ.
 * @throws std::bad_alloc if candidate or release storage cannot be allocated.
 * @details Groups are aligned to multiples of the initial width on the
 * caller's clock. The start is the latest limit end among released jobs whose
 * identities were chosen. */
template <typename Select>
std::optional<std::pair<sim_time_t, Bitmap>>
grouped_release_fit(const std::vector<ReleaseRecord> &running,
                    const Bitmap &idle, const Bitmap &usable,
                    num_nodes_t quantity, sim_time_t now, sim_time_t earliest,
                    sim_time_t bound, Select select,
                    ReleasePrefix *prefix = nullptr) {
  Bitmap candidates = usable;
  candidates.intersect_with(idle);
  if (earliest <= now && candidates.count() >= quantity)
    return std::pair{now, select(candidates)};
  const auto add_usable = [&](const Bitmap &nodes) {
    candidates.add_intersection(nodes, usable);
  };
  // Releases by earliest form a prefix of the sorted records; the rest are
  // pending, scanned from index first.
  size_t first = 0;
  if (earliest > now) {
    first = static_cast<size_t>(
        std::ranges::partition_point(running,
                                     [earliest](const ReleaseRecord &record) {
                                       return record.limit_end <= earliest;
                                     }) -
        running.begin());
    if (prefix)
      candidates.add_intersection(prefix->before(first), usable);
    else
      for (size_t index = 0; index < first; ++index)
        add_usable(running[index].nodes);
    if (candidates.count() >= quantity)
      return std::pair{now, select(candidates)};
  }
  tdiff_t width = release_group_initial;
  std::optional<sim_time_t> group_edge;
  size_t k = first;
  while (true) {
    bool took_any = false, exhausted = true;
    while (true) {
      if (k >= running.size() || (bound != 0 && running[k].limit_end > bound))
        break;
      const auto &record = running[k++];
      if (!record.nodes.intersects(usable))
        continue;
      if (!group_edge)
        group_edge =
            record.limit_end + (width - std::fmod(record.limit_end, width));
      add_usable(record.nodes);
      took_any = true;
      if (k >= running.size())
        break;
      if (running[k].limit_end > *group_edge + width) {
        exhausted = false;
        break;
      }
    }
    if (!took_any)
      return std::nullopt;
    if (candidates.count() >= quantity) {
      auto picked = select(candidates);
      // picked lies within usable, so only released records can intersect
      // it; the latest such release sets the start.
      sim_time_t start = now;
      for (size_t index = k; index-- > first;)
        if (running[index].nodes.intersects(picked)) {
          start = std::max(now, running[index].limit_end);
          break;
        }
      return std::pair{start, std::move(picked)};
    }
    if (exhausted)
      return std::nullopt;
    const auto upcoming = running[k].limit_end;
    while (width <= release_group_cap) {
      width *= 2;
      if (!(upcoming > *group_edge + width))
        break;
    }
  }
}

/** @brief Find (start, nodes) for one examined job.
 * @tparam Select Callable returning exactly quantity identities from a
 * feasible candidate bitmap.
 * @param[in] profile Waiting-job bookings made during this cycle.
 * @param[in] running Running allocations sorted by nondecreasing limit end.
 * @param[in] idle Identities available at now.
 * @param[in] quantity Requested whole-node count.
 * @param[in] duration Requested planning duration.
 * @param[in] now Current cycle time.
 * @param[in] now_only Whether the booking budget is spent, so only an
 * immediate start is allowed.
 * @param[in] select Node-selection callable.
 * @return Planned start and identities, or std::nullopt.
 * @throws std::invalid_argument if bitmap domains differ.
 * @throws std::bad_alloc if planning scratch allocation fails.
 * @details Feasibility includes the requested endpoint. A collision retries
 * at the next useful profile boundary; when a collision starts exactly at the
 * attempted boundary, one time unit is added to guarantee progress. now_only
 * suppresses every future retry and accepts only idle identities at now. */
template <typename Select>
std::optional<std::pair<sim_time_t, Bitmap>>
plan_profile_job(const ReservationProfile &profile,
                 const std::vector<ReleaseRecord> &running, const Bitmap &idle,
                 num_nodes_t quantity, tdiff_t duration, sim_time_t now,
                 bool now_only, Select select) {
  if (now_only) {
    // Only idle identities free throughout [now, now + duration] qualify,
    // the same set the general loop's first attempt would select from.
    if (idle.count() < quantity)
      return std::nullopt;
    auto candidates = profile.free_during(idle, now, duration + now);
    if (candidates.count() < quantity)
      return std::nullopt;
    return std::pair{now, select(candidates)};
  }
  // Shared by this call's retries, whose windows and release prefixes only
  // grow.
  ProfileWindow window(profile);
  std::optional<ReleasePrefix> prefix;
  sim_time_t retry = now;
  while (true) {
    const sim_time_t earliest = retry;
    const sim_time_t finish = duration + std::max(earliest, now);
    auto [usable, next_retry] = window.usable(earliest, finish);
    retry = next_retry;
    if (usable.count() < quantity) {
      if (retry != 0)
        continue;
      return std::nullopt;
    }
    if (earliest > now && !prefix)
      prefix.emplace(running, idle.words().size());
    auto fit =
        grouped_release_fit(running, idle, usable, quantity, now, earliest,
                            retry, select, prefix ? &*prefix : nullptr);
    if (!fit) {
      if (retry != 0)
        continue;
      return std::nullopt;
    }
    fit->first = std::max(fit->first, earliest);
    const auto start = fit->first;
    if (start > now && profile.collides(fit->second, start, start + duration)) {
      retry = start + (start == earliest ? 1 : 0);
      continue;
    }
    if (retry != 0 && start > retry)
      continue;
    return fit;
  }
}

} // namespace dr_evt
#endif
