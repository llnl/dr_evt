/******************************************************************************
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
/** @file running_jobs.hpp
 * @brief Lifecycle-owned running resources with separate capacity/placement
 * layouts.
 */
#ifndef DR_EVT_SIM_RUNNING_JOBS_HPP
#define DR_EVT_SIM_RUNNING_JOBS_HPP
#include "sim/resource/placement_model.hpp"
#include <map>
#include <variant>
namespace dr_evt {
/** @brief Existing execution and forecast metadata, without placement storage.
 */
struct Running_Job {
  sim_time_t start_time; ///< Actual execution start.
  tdiff_t run_time;      ///< Forecast duration until reusable.
  num_nodes_t nodes; ///< Requested quantity, equal to allocation cardinality.
};
/** @brief Placement-only live owner retained through END and delayed RELEASE.
 */
struct Placement_Running_Job {
  Running_Job timing{};        ///< Same metadata used by count consumers.
  BitmapAllocation allocation; ///< Exact identities owned until release.
};
/** @brief One authoritative running store; count records retain their original
 * size.
 * @details Only the selected map exists. Placement masks never depend on
 * resident trace records. The occupied union is derived admission state: commit
 * adds the same mask owned by the map, erase removes it, and clear resets both.
 */
class running_jobs_t {
public:
  using CountMap = std::map<job_no_t, Running_Job>; ///< Capacity-only owners.
  using PlacementMap =
      std::map<job_no_t, Placement_Running_Job>; ///< Placement owners.
  using value_type = CountMap::value_type; ///< Compatibility count entry type.
  /** @brief Placement-only inventory and accounting, sharing the map lifetime.
   */
  struct PlacementState {
    PlacementMap jobs; ///< Authoritative per-job owners.
    Bitmap eligible;   ///< Immutable node inventory.
    Bitmap occupied;   ///< Derived union maintained at commit and release.
    /** @brief Create empty placement accounting.
     * @param[in] capacity Number of eligible node identities. */
    explicit PlacementState(num_nodes_t capacity)
        : eligible(capacity, true), occupied(capacity) {}
  };
  /** @brief Borrow key and existing count metadata without copying records. */
  struct EntryView {
    const job_no_t &first;     ///< Borrowed job identity.
    const Running_Job &second; ///< Borrowed metadata valid until erase.
  };
  /** @brief Read-only iterator adapting either existing owning map. */
  class const_iterator {
    std::variant<CountMap::const_iterator, PlacementMap::const_iterator> m_it;

  public:
    /** @brief Construct a count-map iterator.
     * @param[in] it Existing map position, not an owning copy. */
    explicit const_iterator(CountMap::const_iterator it) : m_it(it) {}
    /** @brief Construct a placement-map iterator.
     * @param[in] it Existing map position, not an owning copy. */
    explicit const_iterator(PlacementMap::const_iterator it) : m_it(it) {}
    /** @brief Borrow the current metadata.
     * @return View valid while the owning entry exists. */
    EntryView operator*() const {
      return std::visit(
          [](auto it) -> EntryView {
            if constexpr (std::is_same_v<decltype(it),
                                         CountMap::const_iterator>)
              return {it->first, it->second};
            else
              return {it->first, it->second.timing};
          },
          m_it);
    }
    /** @brief Advance within the owning map.
     * @return This iterator after increment. */
    const_iterator &operator++() {
      std::visit([](auto &it) { ++it; }, m_it);
      return *this;
    }
    /** @brief Compare positions in the same selected map.
     * @param[in] other Position to compare.
     * @return True for equal positions. */
    bool operator==(const const_iterator &other) const {
      return m_it == other.m_it;
    }
  };

private:
  std::variant<CountMap, PlacementState> m_state;

public:
  /** @brief Construct an empty capacity store. */
  running_jobs_t() = default;
  /** @brief Construct existing capacity metadata for scheduler callers.
   * @param[in] entries Initial count entries, copied into the owner. */
  running_jobs_t(std::initializer_list<value_type> entries)
      : m_state(CountMap(entries)) {}
  /** @brief Select placement storage before any jobs are admitted.
   * @param[in] capacity Eligible node count.
   * @throws std::logic_error if running records already exist.
   * @throws std::bad_alloc if inventory storage cannot be allocated. */
  void enable_placement(num_nodes_t capacity) {
    if (!empty())
      throw std::logic_error(
          "resource selection requires an empty running store");
    m_state.emplace<PlacementState>(capacity);
  }
  /** @brief Borrow the selected placement owner.
   * @return Pointer valid until resource selection or destruction; null for
   * capacity. */
  PlacementState *placement() { return std::get_if<PlacementState>(&m_state); }
  /** @brief Borrow read-only placement accounting.
   * @return Pointer valid until resource selection or destruction; null for
   * capacity. */
  const PlacementState *placement() const {
    return std::get_if<PlacementState>(&m_state);
  }
  /** @brief Count live and cleanup records. @return Existing owner count. */
  size_t size() const {
    return std::visit(
        [](const auto &state) {
          if constexpr (std::is_same_v<std::decay_t<decltype(state)>, CountMap>)
            return state.size();
          else
            return state.jobs.size();
        },
        m_state);
  }
  /** @brief Test whether no records remain. @return True for an empty owner. */
  bool empty() const { return size() == 0; }
  /** @brief Borrow the first metadata entry. @return Iterator valid until
   * erase. */
  const_iterator begin() const {
    if (auto *state = placement())
      return const_iterator(state->jobs.begin());
    return const_iterator(std::get<CountMap>(m_state).begin());
  }
  /** @brief Borrow the end position. @return Sentinel for the selected map. */
  const_iterator end() const {
    if (auto *state = placement())
      return const_iterator(state->jobs.end());
    return const_iterator(std::get<CountMap>(m_state).end());
  }
  /** @brief Access execution metadata, creating a count record when necessary.
   * @param[in] id Existing trace job identity.
   * @return Mutable metadata; placement allocation ownership is preserved.
   * @throws std::out_of_range if a placement record was not committed.
   * @throws std::bad_alloc if a new capacity entry cannot be allocated. */
  Running_Job &operator[](job_no_t id) {
    if (auto *state = placement())
      return state->jobs.at(id).timing;
    return std::get<CountMap>(m_state)[id];
  }
  /** @brief Restore or insert count metadata.
   * @param[in] id Job identity.
   * @param[in] record Count metadata to copy.
   * @throws std::logic_error if placement ownership was not supplied.
   * @throws std::bad_alloc if entry storage cannot be allocated. */
  void emplace(job_no_t id, const Running_Job &record) {
    if (placement())
      throw std::logic_error(
          "placement restore requires allocation identities");
    std::get<CountMap>(m_state).emplace(id, record);
  }
  /** @brief Commit a selected allocation into its existing lifecycle owner.
   * @param[in] id Job identity, not already running.
   * @param[in] timing Execution and forecast metadata.
   * @param[in] allocation Owning selected identities; transferred into the map.
   * @throws std::invalid_argument for mismatched quantity, domain, or overlap.
   * @throws std::logic_error for capacity mode or an existing running owner.
   * @throws std::bad_alloc if entry storage cannot be allocated.
   * @details Occupied membership changes only after map admission succeeds. */
  void commit(job_no_t id, Running_Job timing, BitmapAllocation allocation) {
    auto *state = placement();
    if (!state)
      throw std::logic_error("allocation requires placement mode");
    if (timing.nodes == 0 || timing.nodes != allocation.count() ||
        !state->eligible.contains(allocation) ||
        state->occupied.intersects(allocation))
      throw std::invalid_argument("invalid live placement allocation");
    auto [it, inserted] = state->jobs.emplace(
        id, Placement_Running_Job{timing, std::move(allocation)});
    if (!inserted)
      throw std::logic_error("job already owns a placement allocation");
    state->occupied.add(it->second.allocation);
  }
  /** @brief Release exact identities and destroy their running owner.
   * @param[in] id Released job identity; absent IDs are ignored.
   * @return Number of owners removed, zero or one.
   * @throws std::invalid_argument if occupied membership is inconsistent. */
  size_t erase(job_no_t id) {
    if (auto *state = placement()) {
      auto it = state->jobs.find(id);
      if (it == state->jobs.end())
        return 0;
      PlacementModel<>::release(state->occupied, it->second.allocation);
      state->jobs.erase(it);
      return 1;
    }
    return std::get<CountMap>(m_state).erase(id);
  }
  /** @brief Destroy all running owners and reset their admission union. */
  void clear() {
    if (auto *state = placement()) {
      state->jobs.clear();
      state->occupied.remove(state->occupied);
    } else
      std::get<CountMap>(m_state).clear();
  }
};
/** @brief Owning immediate decision transferred from planner to execution. */
struct Placement_Decision {
  job_no_t job; ///< Existing job identity.
  sim_time_t
      forecast_finish; ///< Planned release used while selection is speculative.
  BitmapAllocation
      allocation; ///< Moves into the running owner at START admission.
};
/** @brief Selected decisions with separate count and placement element layouts.
 */
class Scheduling_Decisions {
  std::variant<std::vector<job_no_t>, std::vector<Placement_Decision>> m_jobs;

public:
  /** @brief Transfer a capacity-only selection.
   * @param[in] jobs Owning selected identifiers. */
  Scheduling_Decisions(std::vector<job_no_t> jobs) : m_jobs(std::move(jobs)) {}
  /** @brief Transfer a placement selection.
   * @param[in] jobs Owning selected allocations. */
  Scheduling_Decisions(std::vector<Placement_Decision> jobs)
      : m_jobs(std::move(jobs)) {}
  /** @brief Count selections. @return Number of immediate decisions. */
  size_t size() const {
    return std::visit([](const auto &jobs) { return jobs.size(); }, m_jobs);
  }
  /** @brief Test whether no job was selected. @return True for no decisions. */
  bool empty() const { return size() == 0; }
  /** @brief Read one selected job identity.
   * @param[in] index Index strictly below size().
   * @return Existing trace job identity. */
  job_no_t job(size_t index) const {
    return std::visit(
        [index](const auto &jobs) -> job_no_t {
          if constexpr (std::is_same_v<std::decay_t<decltype(jobs)>,
                                       std::vector<job_no_t>>)
            return jobs[index];
          else
            return jobs[index].job;
        },
        m_jobs);
  }
  /** @brief Transfer one selected placement allocation into execution.
   * @param[in] index Placement decision index strictly below size().
   * @return Owning exact identities, consumed once by the caller.
   * @throws std::bad_variant_access if this is a capacity selection. */
  BitmapAllocation take_allocation(size_t index) {
    return std::move(
        std::get<std::vector<Placement_Decision>>(m_jobs)[index].allocation);
  }
};
} // namespace dr_evt
#endif
