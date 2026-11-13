/******************************************************************************
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
/** @file test_placement_model.cpp
 * @brief Bitmap boundary, exact placement oracle, lifetime, and accounting
 * tests.
 */
#define DR_EVT_HAS_CONFIG 1
#include "sim/resource/placement_model.hpp"
#include <array>
#include <iostream>
#include <random>
#include <type_traits>

using namespace dr_evt;

/** @brief Fail a test invariant.
 * @param[in] ok Expected true condition.
 * @param[in] message Failure description.
 * @throws std::runtime_error if the condition is false. */
static void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}

/** @brief Require a specific failure without ignoring unrelated exceptions.
 * @tparam Error Expected exception type.
 * @tparam Action Callable type.
 * @param[in] action Operation expected to fail.
 * @throws std::runtime_error if the operation succeeds. */
template <typename Error, typename Action> static void rejects(Action action) {
  try {
    action();
  } catch (const Error &) {
    return;
  }
  throw std::runtime_error("expected exception was not raised");
}

/** @brief Check word boundaries, batch operations, and exact selection.
 * @throws std::runtime_error if an invariant fails. */
static void bitmap_checks() {
  std::mt19937 rng(1729);
  for (num_nodes_t capacity :
       {0U, 1U, 63U, 64U, 65U, 127U, 128U, 129U, 1025U}) {
    Bitmap full(capacity, true);
    require(full.count() == capacity, "padding counted as valid resources");
    for (unsigned trial = 0; trial < 8; ++trial) {
      std::vector<num_nodes_t> ids;
      for (num_nodes_t id = 0; id < capacity; ++id)
        if (trial == 0 || rng() % 3)
          ids.push_back(id);
      const auto source = Bitmap::from_ids(capacity, ids);
      for (num_nodes_t n = 0; n <= ids.size() + 1; ++n) {
        auto low = source.select<NodeOrder::LowIdFirst>(n);
        auto high = source.select<NodeOrder::HighIdFirst>(n);
        require(low.has_value() == (n <= ids.size()),
                "low selection feasibility");
        require(high.has_value() == low.has_value(),
                "high selection feasibility");
        auto retained_low = source, retained_high = source;
        require(retained_low.retain_first<NodeOrder::LowIdFirst>(n) ==
                    low.has_value(),
                "in-place low feasibility");
        require(retained_high.retain_first<NodeOrder::HighIdFirst>(n) ==
                    high.has_value(),
                "in-place high feasibility");
        if (!low) {
          require(retained_low == source && retained_high == source,
                  "failed in-place selection changed source");
          continue;
        }
        require(retained_low == *low && retained_high == *high,
                "in-place selection differs from owning selection");
        require(low->ids() ==
                    std::vector<num_nodes_t>(ids.begin(), ids.begin() + n),
                "low ID ordering");
        require(high->ids() ==
                    std::vector<num_nodes_t>(ids.end() - n, ids.end()),
                "high ID ordering");
        Bitmap state = full;
        state.remove(*low);
        state.add(*low);
        require(state == full, "batch removal/restore");
        state.toggle(*low);
        state.toggle(*low);
        require(state == full, "batch toggle");
      }
      std::vector<num_nodes_t> blocked_ids, free_ids;
      for (auto id : ids)
        (id % 2 ? free_ids : blocked_ids).push_back(id);
      const auto blocked = Bitmap::from_ids(capacity, blocked_ids);
      for (num_nodes_t n = 0; n <= free_ids.size() + 1; ++n) {
        const auto low =
            source.select_excluding<NodeOrder::LowIdFirst>(blocked, n);
        const auto high =
            source.select_excluding<NodeOrder::HighIdFirst>(blocked, n);
        require(low.has_value() == (n <= free_ids.size()) &&
                    high.has_value() == low.has_value(),
                "excluded selection feasibility");
        if (low) {
          require(low->ids() == std::vector<num_nodes_t>(free_ids.begin(),
                                                         free_ids.begin() + n),
                  "excluded low identities");
          require(high->ids() == std::vector<num_nodes_t>(free_ids.end() - n,
                                                          free_ids.end()),
                  "excluded high identities");
        }
      }
      rejects<std::invalid_argument>(
          [&] { source.select_excluding(Bitmap(capacity + 1), 0); });
      require(blocked.ids() == blocked_ids, "selection mutated blocked mask");
      require(source.ids() == ids, "selection mutated source");
      auto state = source;
      rejects<std::invalid_argument>([&] { state.add(Bitmap(capacity + 1)); });
      require(state == source, "invalid domain changed state");
      state.intersect_with(state);
      require(state == source, "self intersection");
      state.remove(state);
      require(state.count() == 0, "self removal");
    }
  }
  Bitmap movable(65, true);
  Bitmap moved(std::move(movable));
  require(moved.count() == 65 && movable.capacity() == 0 &&
              movable.words().empty(),
          "move construction left invalid source");
  movable = std::move(moved);
  require(movable.count() == 65 && moved.capacity() == 0 &&
              moved.words().empty(),
          "move assignment left invalid source");
  const auto *storage = movable.words().data();
  const auto replacement = Bitmap::from_ids(64, std::array<num_nodes_t, 1>{63});
  movable = replacement;
  require(movable == replacement && movable.words().data() == storage,
          "copy assignment did not reuse sufficient storage");
  const auto &self = movable;
  movable = self;
  require(movable == replacement, "self copy changed membership");
  const std::array<num_nodes_t, 2> duplicates{1, 1};
  rejects<std::invalid_argument>([&] { Bitmap::from_ids(2, duplicates); });
  const std::array<num_nodes_t, 1> outside{2};
  rejects<std::out_of_range>([&] { Bitmap::from_ids(2, outside); });
  std::vector<num_nodes_t> fragmented_ids{3, 4, 5, 6};
  for (num_nodes_t id = 12; id <= 21; ++id)
    fragmented_ids.push_back(id);
  fragmented_ids.push_back(58);
  for (num_nodes_t id = 252; id <= 755; ++id)
    fragmented_ids.push_back(id);
  const auto fragmented = Bitmap::from_ids(756, fragmented_ids);
  auto slurm_flat = fragmented.select_best_fit_runs<NodeOrder::LowIdFirst>(512);
  std::vector<num_nodes_t> expected;
  for (num_nodes_t id = 12; id <= 19; ++id)
    expected.push_back(id);
  for (num_nodes_t id = 252; id <= 755; ++id)
    expected.push_back(id);
  require(slurm_flat && slurm_flat->ids() == expected,
          "flat-topology run best fit differs from Slurm ordering");
}

/** @brief Compare exact identities against an independent dense node-time
 * oracle.
 * @tparam Order Resource selection order.
 * @tparam Capacity Node domain size, including multiword test cases.
 * @tparam CoarseFilter Compile-time lambda selecting coarse filtering.
 * @throws std::runtime_error if fit, ordering, or coarse rejection is wrong. */
template <NodeOrder Order, num_nodes_t Capacity = 9,
          auto CoarseFilter = DisableCoarseFilter>
static void timeline_oracle() {
  constexpr int horizon = 48;
  constexpr num_nodes_t capacity = Capacity;
  std::mt19937 rng(314159);
  for (int trial = 0; trial < 12; ++trial) {
    std::array<std::array<bool, capacity>, horizon> used{};
    BasicPlacementTimeline<CoarseFilter> plan(Bitmap(capacity, true), 0);
    const auto expected =
        [&](int start, int duration,
            num_nodes_t quantity) -> std::optional<std::vector<num_nodes_t>> {
      std::vector<num_nodes_t> ids;
      for (num_nodes_t node = 0; node < capacity; ++node) {
        bool available = true;
        for (int t = start;
             t < std::min(horizon, start + std::max(duration, 1)); ++t)
          available &= !used[t][node];
        if (available)
          ids.push_back(node);
      }
      if (ids.size() < quantity)
        return std::nullopt;
      if constexpr (Order == NodeOrder::LowIdFirst)
        ids.resize(quantity);
      else
        ids.erase(ids.begin(), ids.end() - quantity);
      return ids;
    };
    for (int step = 0; step < 30; ++step) {
      const int start = rng() % horizon, duration = 1 + rng() % 8;
      const num_nodes_t quantity = 1 + rng() % capacity;
      auto p = plan.template find_at<Order>(quantity, duration, start);
      if (p && start + duration <= horizon) {
        for (int t = start; t < start + duration; ++t)
          for (auto node : p->allocation.ids())
            used[t][node] = true;
        plan.reserve(std::move(*p), duration, step);
      }
      for (int query = 0; query < 16; ++query) {
        const int at = rng() % horizon, length = rng() % 10;
        const num_nodes_t n = rng() % (capacity + 2);
        auto want = expected(at, length, n);
        auto actual = plan.template find_at<Order>(n, length, at);
        require(actual.has_value() == want.has_value(),
                "exact feasibility differs from dense oracle");
        require(plan.can_fit(at, n, length) == want.has_value(),
                "boolean feasibility differs from dense oracle");
        if (actual) {
          require(actual->allocation.ids() == *want,
                  "exact identities differ from dense oracle");
          require(plan.coarse_possible(n, length, at),
                  "coarse filter has false negative");
        }
        int earliest = at;
        while (!expected(earliest, length, n) && earliest < horizon)
          ++earliest;
        auto future = plan.template earliest_fit<Order>(n, length, at);
        require(future.has_value() == (n <= capacity), "earliest feasibility");
        if (future)
          require(future->start == earliest,
                  "earliest time differs from dense oracle");
      }
    }
  }
}

/** @brief Recording observer for synchronous metadata checks. */
struct Observer {
  static constexpr bool enabled = true;
  inline static std::vector<ResourceObservation> events;
  /** @brief Save a snapshot for this test only.
   * @param[in] event Synchronous resource observation. */
  static void observe(const ResourceObservation &event) {
    events.push_back(event);
  }
};
using ExactTimeline = BasicPlacementTimeline<[] { return false; }>;
using ExactModel = PlacementModel<NoResourceObserver, NodeOrder::HighIdFirst,
                                  [] { return false; }>;
static_assert(!ExactTimeline::coarse_filter_enabled);
static_assert(!ExactModel::Timeline::coarse_filter_enabled);
static_assert(!PlacementTimeline::coarse_filter_enabled);
static_assert(!PlacementModel<>::Timeline::coarse_filter_enabled);
static_assert(std::is_same_v<PlacementModel<>::Timeline, PlacementTimeline>);
static_assert(std::is_empty_v<detail::PlacementFilterStorage<false>>);
static_assert(sizeof(ExactTimeline) == sizeof(PlacementTimeline));
static_assert(sizeof(PlacementTimeline) <
              sizeof(BasicPlacementTimeline<EnableCoarseFilter>));
static_assert(std::is_empty_v<ExactModel>);
static_assert(std::is_empty_v<PlacementModel<>>);
static_assert(std::is_empty_v<PlacementModel<Observer>>);
static_assert(!std::is_polymorphic_v<PlacementModel<>>);
static_assert(
    std::is_nothrow_move_constructible_v<PlacementTimeline::Interval>);

/** @brief Check ownership, endpoint behavior, error atomicity, and
 * observations.
 * @tparam CoarseFilter Compile-time lambda selecting coarse filtering.
 * @throws std::runtime_error if an invariant fails. */
template <auto CoarseFilter = DisableCoarseFilter>
static void lifecycle_checks() {
  using Model = PlacementModel<Observer, NodeOrder::LowIdFirst, CoarseFilter>;
  using Timeline = typename Model::Timeline;
  Observer::events.clear();
  auto running = *Bitmap(4, true).select(2);
  Timeline plan(Bitmap(4, true), 0);
  plan.borrow_running(running, 10, 42);
  require(&plan.intervals()[0].allocation() == &running,
          "running allocation copied");
  auto copy = plan;
  require(&copy.intervals()[0].allocation() == &running,
          "copied planner lost borrowed owner");
  require(plan.coarse_possible(4, 1, 10), "release endpoint is unavailable");
  require(!plan.find_at(4, 1, 9), "overlapping execution admitted");
  auto p = Model::find_fit(plan, 4, 1, 0, {7, 0, 3});
  require(p && p->start == 10, "earliest release endpoint");
  Model::reserve(plan, *p, 4, 1, {7, 0, 3});
  require(!plan.find_at(4, 1, 10), "coarse cache not invalidated");
  require(copy.find_at(4, 1, 10).has_value(), "copy changed with source");
  const auto before_query = Observer::events.size();
  require(!Model::can_fit(plan, 4, 1, 10, {8, 10, 3}),
          "boolean query ignored reservation");
  require(Observer::events.size() == before_query + 1 &&
              Observer::events.back().operation == ResourceOperation::FitNow &&
              Observer::events.back().context.timeline == &plan &&
              !Observer::events.back().success,
          "boolean observer snapshot");
  rejects<std::invalid_argument>([&] { Model::can_fit(plan, 1, INFINITY, 0); });
  const auto size = plan.intervals().size();
  rejects<std::invalid_argument>([&] { plan.reserve(*p, 1); });
  require(plan.intervals().size() == size,
          "failed reservation changed intervals");
  rejects<std::invalid_argument>([&] { plan.find_at(1, 1, -1); });
  rejects<std::invalid_argument>([&] { plan.find_at(1, INFINITY, 0); });
  rejects<std::invalid_argument>([&] { plan.find_at(1, 1, NAN); });
  rejects<std::invalid_argument>([&] { plan.borrow_running(running, 0); });
  typename Model::State allocated(4), releasing(4), available(4, true);
  Model::consume_available(available, running);
  Model::allocate(allocated, running);
  auto saved = allocated;
  rejects<std::invalid_argument>([&] { Model::allocate(allocated, running); });
  require(allocated == saved, "duplicate charge changed state");
  Model::hold(releasing, running);
  Model::release(allocated, running);
  Model::release(releasing, running, {}, ResourceScope::Releasing);
  rejects<std::invalid_argument>([&] { Model::release(allocated, running); });
  require(Observer::events.back().before == 2 &&
              Observer::events.back().after == 0,
          "observer state snapshots");
  require(Observer::events.front().context.timeline == &plan &&
              Observer::events.front().context.job == 7,
          "observer planning metadata");
  allocated = running;
  Model::release(allocated, allocated);
  require(Observer::events.back().quantity == 2 && allocated.count() == 0,
          "aliased release lost observed quantity");
  // Within one coarse bucket each node becomes free, but neither is free
  // throughout the request. The optimistic pass must still refine exactly.
  Timeline optimistic(Bitmap(3, true), 0);
  const std::array<num_nodes_t, 1> id0{0}, id1{1}, id2{2};
  optimistic.reserve({0, Bitmap::from_ids(3, id0)}, 0.5);
  optimistic.reserve({0.5, Bitmap::from_ids(3, id1)}, 0.5);
  optimistic.reserve({0, Bitmap::from_ids(3, id2)}, 64);
  require(optimistic.coarse_possible(2, 1, 0),
          "coarse union must be optimistic");
  require(!optimistic.find_at(2, 1, 0),
          "coarse false positive accepted as exact fit");
  auto eligible = Bitmap::from_ids(3, id0);
  Timeline subset(eligible, 0);
  rejects<std::invalid_argument>(
      [&] { subset.reserve({0, Bitmap::from_ids(3, id1)}, 1); });
  require(subset.find_at(1, 1, 0)->allocation == eligible,
          "eligibility not preserved");
  Timeline empty(Bitmap(0), 0);
  auto zero = empty.find_at(0, 1, 0);
  empty.reserve(*zero, 1);
  require(empty.intervals().empty(), "zero allocation retained interval");
  const double origin = 1e16;
  Timeline collapsed(Bitmap(4, true), origin);
  collapsed.borrow_running(running, origin + 2);
  require(collapsed.find_at(2, 2, origin).has_value(),
          "collapsed buckets reject exact fit");
  require(collapsed.find_at(4, 2, origin + 2).has_value(),
          "collapsed endpoint rejected");
  rejects<std::invalid_argument>([&] { collapsed.find_at(1, 0.1, origin); });
  Timeline fractional(Bitmap(4, true), 0.125);
  fractional.borrow_running(running, 0.375);
  auto exact = fractional.earliest_fit(4, 0.125, 0.125);
  require(exact && exact->start == 0.375, "fractional start was quantized");
  require(fractional.find_at(4, 0, 0.375).has_value(),
          "point release boundary");
  require(!fractional.find_at(4, -1, 0.125),
          "negative duration point semantics");
}

/** @brief Check immediate selection and atomic batch seeding over borrowed
 * owners.
 * @tparam CoarseFilter Compile-time lambda selecting coarse filtering.
 * @throws std::runtime_error if selection, admission, or ownership is
 * incorrect. */
template <auto CoarseFilter = DisableCoarseFilter>
static void optimized_lifecycle_checks() {
  using Model = PlacementModel<Observer, NodeOrder::LowIdFirst, CoarseFilter>;
  using Timeline = typename Model::Timeline;
  using Interval = typename Timeline::Interval;
  const auto low = Bitmap::from_ids(129, std::array<num_nodes_t, 2>{0, 64});
  const auto high = Bitmap::from_ids(129, std::array<num_nodes_t, 2>{63, 128});
  const Bitmap inventory(129, true), empty(129);
  const auto occupied = low;
  auto immediate = Model::find_available(inventory, occupied, 3, 10, 0, {9, 0});
  require(immediate && immediate->allocation.ids() ==
                           std::vector<num_nodes_t>({1, 2, 3}),
          "immediate selection identities");
  require(Observer::events.back().scope == ResourceScope::Available &&
              Observer::events.back().before == 127 &&
              Observer::events.back().after == 127,
          "immediate observer snapshots");
  require(!Model::find_available(inventory, occupied, 128, 10, 0),
          "immediate over-allocation");
  require(Model::find_available(inventory, occupied, 0, -1, 0)
                  ->allocation.count() == 0,
          "zero immediate request");
  rejects<std::invalid_argument>(
      [&] { Model::find_available(inventory, Bitmap(128), 0, 1, 0); });
  rejects<std::invalid_argument>(
      [&] { Model::find_available(inventory, occupied, 1, INFINITY, 0); });
  rejects<std::invalid_argument>(
      [&] { Model::find_available(inventory, occupied, 1, 1, NAN); });
  rejects<std::invalid_argument>(
      [&] { Model::find_available(inventory, occupied, 1, 0.1, 1e16); });
  rejects<std::invalid_argument>(
      [&] { Model::find_available(inventory, occupied, 1, 1e308, 1e308); });
  require(occupied == low, "immediate selection mutated live accounting");
  std::array<Interval, 3> records{{{0, 20, 2, std::cref(high)},
                                   {0, 10, 1, std::cref(low)},
                                   {0, 30, 3, std::cref(empty)}}};
  const auto describe = [](const Interval &interval) { return interval; };
  Timeline batch(inventory, 0), scalar(inventory, 0);
  batch.seed_running(records, describe);
  for (const auto &record : records)
    scalar.borrow_running(record.allocation(), record.finish, record.job);
  require(batch.intervals().size() == 2 &&
              &batch.intervals()[0].allocation() == &low &&
              &batch.intervals()[1].allocation() == &high,
          "batch lost release ordering or borrowed ownership");
  for (int t = 0; t <= 30; ++t) {
    auto want = scalar.earliest_fit(128, 5, t);
    auto actual = batch.earliest_fit(128, 5, t);
    require(want && actual && want->start == actual->start &&
                want->allocation == actual->allocation,
            "batch differs from scalar seed");
  }
  auto copy = batch;
  require(&copy.intervals()[0].allocation() == &low, "batch copy lost owner");
  rejects<std::invalid_argument>(
      [&] { batch.seed_running(records, describe); });
  Timeline failed(inventory, 0);
  auto invalid = records;
  invalid[1].masks = std::cref(high);
  rejects<std::invalid_argument>(
      [&] { failed.seed_running(invalid, describe); });
  require(failed.intervals().empty(), "overlap partially seeded planner");
  invalid = records;
  invalid[1].masks = low;
  rejects<std::invalid_argument>(
      [&] { failed.seed_running(invalid, describe); });
  invalid = records;
  invalid[1].finish = 0;
  rejects<std::invalid_argument>(
      [&] { failed.seed_running(invalid, describe); });
  invalid[1].finish = INFINITY;
  rejects<std::invalid_argument>(
      [&] { failed.seed_running(invalid, describe); });
  invalid = records;
  invalid[1].start = 1;
  rejects<std::invalid_argument>(
      [&] { failed.seed_running(invalid, describe); });
  const Bitmap other_domain(128);
  invalid = records;
  invalid[1].masks = std::cref(other_domain);
  rejects<std::invalid_argument>(
      [&] { failed.seed_running(invalid, describe); });
  rejects<std::runtime_error>([&] {
    failed.seed_running(records, [&](const Interval &interval) {
      if (interval.job == 1)
        throw std::runtime_error("descriptor failed");
      return interval;
    });
  });
  require(failed.intervals().empty(),
          "failed descriptor partially seeded planner");
  failed.seed_running(std::array<Interval, 0>{}, describe);
  require(failed.intervals().empty(), "empty batch seeded intervals");
  Timeline subset(low, 0);
  rejects<std::invalid_argument>(
      [&] { subset.seed_running(records, describe); });
}

/** @brief Run the focused placement checks.
 * @return Zero on success, one on failure. */
int main() {
  try {
    bitmap_checks();
    optimized_lifecycle_checks<>();
    optimized_lifecycle_checks<EnableCoarseFilter>();
    timeline_oracle<NodeOrder::LowIdFirst>();
    timeline_oracle<NodeOrder::HighIdFirst>();
    timeline_oracle<NodeOrder::LowIdFirst, 129>();
    timeline_oracle<NodeOrder::HighIdFirst, 129>();
    timeline_oracle<NodeOrder::LowIdFirst, 9, EnableCoarseFilter>();
    timeline_oracle<NodeOrder::HighIdFirst, 9, EnableCoarseFilter>();
    timeline_oracle<NodeOrder::LowIdFirst, 129, EnableCoarseFilter>();
    timeline_oracle<NodeOrder::HighIdFirst, 129, EnableCoarseFilter>();
    lifecycle_checks<>();
    lifecycle_checks<EnableCoarseFilter>();
    std::cout << "Placement model checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
