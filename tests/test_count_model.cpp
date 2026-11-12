/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file test_count_model.cpp
 * @brief Independent interval oracle, observation, and event accounting checks.
 */
#define DR_EVT_HAS_CONFIG 1
#include "sim/resource/count_model.hpp"
#include "sim/scheduler_fcfs_pass_hybrid_circular.hpp"
#include "sim/sim.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <type_traits>

using namespace dr_evt;

static void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct RecordingObserver {
  static constexpr bool enabled = true;
  inline static std::vector<ResourceObservation> events;
  static void observe(const ResourceObservation &event) {
    events.push_back(event);
  }
};
using ObservedCount = CountModel<RecordingObserver>;
static_assert(sizeof(CountModel<>::State) == sizeof(num_nodes_t));
static_assert(sizeof(CountModel<>::Allocation) == sizeof(num_nodes_t));
static_assert(sizeof(CountModel<>::Placement) == sizeof(sim_time_t));
static_assert(std::is_empty_v<CountModel<>>);
static_assert(std::is_same_v<Reservation_Timeline, CountModel<>::Timeline>);

static void interval_oracle() {
  // A dense, independent integer-time oracle; reservations are selected only
  // when every second fits. Compare every fit against the compressed timeline.
  constexpr int horizon = 64;
  std::mt19937 rng(2718);
  for (int trial = 0; trial < 80; ++trial) {
    std::array<num_nodes_t, horizon> free{};
    std::map<sim_time_t, num_nodes_t> segments;
    for (int t = 0; t < horizon; ++t) {
      free[t] = 1 + rng() % 8;
      segments.emplace(t, free[t]);
    }
    // Beyond the oracle horizon, no positive request fits.
    segments.emplace(horizon, 0);
    ObservedCount::Timeline plan(segments);
    const auto fits = [&free](int start, unsigned quantity, int duration) {
      if (start >= horizon || start + duration > horizon) {
        return false;
      }
      for (int t = start; t < start + std::max(duration, 1); ++t) {
        if (free[t] < quantity) {
          return false;
        }
      }
      return true;
    };
    for (int step = 0; step < 24; ++step) {
      const int start = rng() % horizon;
      const int duration = rng() % 9;
      const unsigned quantity = 1 + rng() % 9;
      int expected = start;
      while (expected < horizon && !fits(expected, quantity, duration)) {
        ++expected;
      }
      const ResourceContext context{static_cast<job_no_t>(step),
                                    static_cast<sim_time_t>(start),
                                    static_cast<size_t>(trial)};
      const auto placement = ObservedCount::find_fit(
          plan, quantity, duration, start, context);
      require(placement.has_value() == (expected != horizon),
              "earliest fit disagrees with independent oracle");
      if (placement) {
        require(ObservedCount::start_time(*placement) == expected,
                "placement must retain earliest start");
        ObservedCount::reserve(plan, *placement, quantity, duration, context);
        for (int t = expected; t < expected + duration; ++t) {
          free[t] -= quantity;
        }
      }
      for (int t = 0; t < horizon; ++t) {
        for (unsigned q = 1; q <= 9; ++q) {
          require(CountModel<>::can_fit(plan, q, 1, t) == fits(t, q, 1),
                  "reservation changed capacity outside its interval");
        }
      }
    }
  }
}

static void observation_checks() {
  RecordingObserver::events.clear();
  ObservedCount::Timeline plan(
      std::map<sim_time_t, num_nodes_t>{{0, 2}, {10, 4}});
  const ResourceContext context{7, 0, 3};
  require(!ObservedCount::can_fit(plan, 3, 5, 0, context),
          "insufficient immediate capacity must fail");
  auto placement = ObservedCount::find_fit(plan, 3, 5, 0, context);
  require(placement && ObservedCount::start_time(*placement) == 10,
          "future fit missing");
  ObservedCount::reserve(plan, *placement, 3, 5, context);
  unsigned available = 4, allocated = 0, releasing = 0;
  ObservedCount::consume_available(available, 3, {7, 10, 4});
  ObservedCount::allocate(allocated, 3, {7, 10});
  ObservedCount::release(allocated, 3, {7, 15});
  ObservedCount::hold(releasing, 3, {7, 15});
  require(allocated == 0 && releasing == 3,
          "completion with delay must keep resources occupied");
  ObservedCount::release(releasing, 3, {7, 20}, ResourceScope::Releasing);
  require(available == 1 && allocated == 0 && releasing == 0,
          "planning and committed state must stay independent");
  const auto &events = RecordingObserver::events;
  require(events.size() == 8 && !events[0].success,
          "hooks must include failed fit and every resource operation");
  require(events[1].start == 10 && events[2].duration == 5 &&
              events[2].context.timeline == &plan &&
              events[2].context.job == 7 && events[2].context.pass == 3,
          "reservation hook must identify job, pass, timeline, and interval");
  require(events[3].scope == ResourceScope::Available &&
              events[3].before == 4 && events[3].after == 1 &&
              events[4].scope == ResourceScope::Allocated &&
              events[4].before == 0 && events[4].after == 3 &&
              events[7].scope == ResourceScope::Releasing &&
              events[7].context.time == 20,
          "hooks must distinguish projected and live changes");

  CountTimeline endpoints(
      std::map<sim_time_t, num_nodes_t>{{0, 2}, {10, 0}, {20, 2}});
  require(endpoints.earliest_fit(2, 10, 0) == 0 &&
              endpoints.earliest_fit<true>(2, 10, 0) == 20,
          "extraction must preserve endpoint-sensitive forecast behavior");
  CountTimeline empty(std::map<sim_time_t, num_nodes_t>{});
  require(!CountModel<>::find_fit(empty, 1, 1, 0) &&
              !CountModel<>::can_fit(empty, 1, 1, 0),
          "empty inventory must not fit");
}

/** @brief Check explicit counting-observer selection and failed-fit accounting.
 */
static void counting_observer_checks() {
  using Model = CountModel<CountingResourceObserver>;
  CountingResourceObserver::reset();
  Model::Timeline plan(std::map<sim_time_t, num_nodes_t>{{0, 2}, {10, 4}});
  require(!Model::can_fit(plan, 3, 5, 0),
          "counting observer must preserve immediate fit failure");
  const auto placement = Model::find_fit(plan, 3, 5, 0);
  require(placement && *placement == 10,
          "counting observer must preserve future fit result");
  Model::reserve(plan, *placement, 3, 5);
  require(!Model::can_fit(plan, 2, 1, 10),
          "observed reservation must consume future capacity");
  require(!Model::find_fit(plan, 5, 1, 0),
          "impossible demand must fail with observation enabled");
  const auto fit_now = static_cast<size_t>(ResourceOperation::FitNow);
  const auto find_fit = static_cast<size_t>(ResourceOperation::FindFit);
  const auto reserve = static_cast<size_t>(ResourceOperation::Reserve);
  require(CountingResourceObserver::calls[fit_now] == 2 &&
              CountingResourceObserver::quantities[fit_now] == 5 &&
              CountingResourceObserver::calls[find_fit] == 2 &&
              CountingResourceObserver::quantities[find_fit] == 8 &&
              CountingResourceObserver::calls[reserve] == 1 &&
              CountingResourceObserver::quantities[reserve] == 3 &&
              CountingResourceObserver::failed_fits == 3,
          "explicit observer must count operations, demand, and failed fits");
  CountingResourceObserver::reset();
  for (auto calls : CountingResourceObserver::calls) {
    require(calls == 0, "reset must clear operation counts");
  }
  for (auto quantity : CountingResourceObserver::quantities) {
    require(quantity == 0, "reset must clear quantity totals");
  }
  require(CountingResourceObserver::failed_fits == 0,
          "reset must clear failed fits");
}

/**
 * @brief Check runtime observer selection, delayed release, and counter isolation.
 * @param[in] diagnostics Whether this test simulation enables observation.
 * @param[in] priority Shared placement-pass or OpenPBS forecast policy to test.
 * @throws std::runtime_error if an accounting or observer invariant fails.
 */
static void integrated_accounting(bool diagnostics, PriorityPolicy priority) {
  const auto directory = std::filesystem::temp_directory_path() /
                         ("dr-evt-count-model-" + std::to_string(
                             std::random_device{}()));
  std::filesystem::create_directory(directory);
  const auto input = directory / "empty.csv";
  std::ofstream(input) << "job_submit_time,num_nodes,time_limit\n";
  CountingResourceObserver::reset();
  Sim_Params params;
  params.m_infile = input.string();
  params.set_outfile((directory / "out.csv").string());
  params.m_total_nodes = 4;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_backfill_policy = BackfillPolicy::HYBRID;
  params.m_reservation_depth = 1;
  params.m_per_pass_accounting = priority != PriorityPolicy::FCFS_1S_EVT_GROUP;
  params.m_priority_policy = priority;
  params.m_resource_model_diagnostics = diagnostics;
  params.m_node_release_delay = 3;
  Simulation simulation(params);
  const auto initial = simulation.get_resource_diagnostics();
  require(initial.has_value() == diagnostics,
          "construction must select diagnostics from runtime configuration");
  if (initial) {
    for (auto calls : initial->calls) {
      require(calls == 0, "new simulation must start with empty counters");
    }
  }
#if DR_EVT_LEGACY_QUEUE_INPUT
  const char *queue = "pbatch";
#else
  const char *queue = "1";
#endif
  simulation.append_job(0, 4, queue, 10);
  simulation.append_job(0, 4, queue, 5);
  simulation.advance_to(30);
  auto status = simulation.get_job_statuses({0, 1});
  require(status[0].start_time == 0 && status[1].start_time == 13 &&
              status[1].end_time == 18,
          "live allocation must respect delayed release");
  for (auto calls : CountingResourceObserver::calls) {
    require(calls == 0, "default model must leave observer counters untouched");
  }
  if (diagnostics) {
    const auto snapshot = simulation.get_resource_diagnostics();
    const auto calls = [&snapshot](ResourceOperation operation) {
      return snapshot->calls[static_cast<size_t>(operation)];
    };
    require(
        calls(ResourceOperation::Allocate) == 2 &&
            calls(ResourceOperation::ProjectedStart) == 2 &&
            calls(ResourceOperation::Hold) == 2 &&
            calls(ResourceOperation::Release) == 4 &&
            calls(ResourceOperation::Reserve) >= 2,
        "runtime observer must cover planning and committed delayed release");
    params.set_outfile((directory / "second.csv").string());
    Simulation other(params);
    other.append_job(0, 4, queue, 2);
    other.advance_to(10);
    require(other.get_resource_diagnostics()
                    ->calls[static_cast<size_t>(ResourceOperation::Allocate)] ==
                1,
            "each simulation must own its counters");
    require(simulation.get_resource_diagnostics()->calls == snapshot->calls,
            "another simulation must not change existing counters");
    std::ostringstream report;
    simulation.print_stats(report);
    require(report.str().find("Resource operation counters") !=
                std::string::npos,
            "enabled diagnostics must be available in reports");
#if defined(DR_EVT_HAS_SER20)
    std::stringstream checkpoint(std::ios::in | std::ios::out |
                                 std::ios::binary);
    simulation.save_checkpoint(checkpoint);
    checkpoint.seekg(0);
    simulation.load_checkpoint(checkpoint);
    const auto restored = simulation.get_resource_diagnostics();
    for (auto count : restored->calls) {
      require(count == 0,
              "successful restore must restart diagnostic counters");
    }
#endif
  }
  std::filesystem::remove_all(directory);
}

int main() {
  interval_oracle();
  observation_checks();
  counting_observer_checks();
  require(!Sim_Params{}.m_resource_model_diagnostics,
          "runtime diagnostics must default to disabled");
  for (auto priority :
       {PriorityPolicy::FCFS, PriorityPolicy::FCFS_1S_EVT_GROUP}) {
    integrated_accounting(false, priority);
    integrated_accounting(true, priority);
  }
  std::cout << "count-model oracle, hook, and event accounting checks passed\n";
}
