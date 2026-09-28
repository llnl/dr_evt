/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#define DR_EVT_HAS_CONFIG 1
#include "sim/scheduler_easy_power.hpp"
#include "sim/scheduler_fcfs_easy_custom.hpp"
#include "sim/sim.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace dr_evt;

#if DR_EVT_LEGACY_QUEUE_INPUT
constexpr const char *kTestQueue = "pbatch";
#else
constexpr const char *kTestQueue = "1";
#endif

job_cost_t cost_from_job_order(job_no_t job_id, sim_time_t, tdiff_t,
                               num_nodes_t) {
  return static_cast<job_cost_t>(job_id);
}

std::optional<size_t>
select_lowest_cost(const backfill_candidates_t &candidates) {
  if (candidates.empty()) {
    return std::nullopt;
  }
  const auto selected =
      std::min_element(candidates.begin(), candidates.end(),
                       [](const auto &lhs, const auto &rhs) {
                         return lhs.cost < rhs.cost;
                       });
  return static_cast<size_t>(std::distance(candidates.begin(), selected));
}

void test_external_backfill_selection() {
  backfill_candidates_t observed;
  auto observe_and_select_lowest =
      [&](const backfill_candidates_t &candidates) -> std::optional<size_t> {
    observed = candidates;
    return select_lowest_cost(candidates);
  };

  FCFSEasyCustomScheduler scheduler(100, 0, BackfillPolicy::EASY, 2,
                                    cost_from_job_order,
                                    observe_and_select_lowest);
  scheduler.insert_job(0, 0.0, 100.0, 70);
  scheduler.insert_job(1, 0.0, 200.0, 50);
  scheduler.insert_job(2, 0.0, 50.0, 20);
  scheduler.insert_job(3, 0.0, 20.0, 10);
  scheduler.insert_job(4, 0.0, 30.0, 10);

  const auto selected = scheduler.schedule(100, {}, 0.0);
  assert((selected == std::vector<job_no_t>{0, 2}));
  assert((observed == backfill_candidates_t{{1, 2}, {2, 3}}));

  // The comparator considers only cost, so std::min_element keeps the first
  // candidate when costs tie.
  assert(select_lowest_cost({{7, 4}, {8, 2}, {9, 2}}) == 1);
}

void test_selector_must_return_a_candidate() {
  FCFSEasyCustomScheduler scheduler(
      100, 0, BackfillPolicy::EASY, 1,
      [](job_no_t, sim_time_t, tdiff_t, num_nodes_t) { return 0; },
      [](const backfill_candidates_t &) {
        return std::optional<size_t>{99};
      });
  scheduler.insert_job(0, 0.0, 100.0, 70);
  scheduler.insert_job(1, 0.0, 200.0, 50);
  scheduler.insert_job(2, 0.0, 20.0, 10);

  bool threw = false;
  try {
    (void)scheduler.schedule(100, {}, 0.0);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);
}

class TestableEASYPowerScheduler final : public EASYPowerScheduler {
public:
  TestableEASYPowerScheduler(
      double maximum_power = 12.0, double initial_target = 10.0,
      double target_weight = 1.0, double maximum_weight = 1.0,
      double dominant_cost = 12.0 * 12.0, double replay_horizon = 1.0,
      double replay_running_energy = 0.0,
      std::array<double, 4> powers = {10.0, 2.1, 1.0, 3.0},
      bool cap_backfill_power = false, bool cap_fcfs_power = false)
      : EASYPowerScheduler(
            100, 4, 4, maximum_power, initial_target, target_weight,
            maximum_weight, dominant_cost,
            [powers](job_no_t job_id, sim_time_t, tdiff_t, num_nodes_t) {
              return powers.at(job_id);
            },
            [replay_horizon, replay_running_energy](
                const std::vector<EASYPowerJob> &,
                const std::vector<EASYPowerJob> &, double, num_nodes_t) {
              return EASYPowerReplayResult{replay_horizon,
                                           replay_running_energy};
            },
            0, CircularOverflowPolicy::GROW, cap_backfill_power,
            cap_fcfs_power) {
    for (job_no_t job_id = 0; job_id < 4; ++job_id) {
      insert_job(job_id, 0.0, 10.0, 1);
    }
  }

  std::optional<size_t> choose(const backfill_candidates_t &candidates) {
    const running_jobs_t running{{0, {0.0, 10.0, 1, 10.0}}};
    const auto selected =
        select_backfill_candidate(candidates, 99, running, 0.0);
    return selected ? std::optional<size_t>{candidates[*selected].queue_index}
                    : std::nullopt;
  }

  double cost(double projected_power) const {
    return candidate_power_cost(projected_power);
  }

  void refresh_target() { on_jobs_became_eligible(0, 4, 100, {}, 0.0); }
};

void test_easypower_semiclamped_candidate_cost() {
  // Use non-unit parameters so each term in the specified equation is
  // independently observable. P == P_max belongs to the target branch.
  const TestableEASYPowerScheduler weighted(12.0, 10.0, 2.0, 3.0, 400.0);
  if (weighted.cost(8.0) != 8.0 || weighted.cost(12.0) != 8.0 ||
      weighted.cost(13.0) != 403.0) {
    throw std::runtime_error(
        "EASYPower candidate costs do not match the specified equation");
  }

  TestableEASYPowerScheduler scheduler;

  // Projected powers are 12.1 and 11.0. Plain target distance would select
  // 12.1, but the dominant above-P_max branch must select 11.0.
  if (scheduler.choose({{1, 2.1}, {2, 1.0}}) != 2) {
    throw std::runtime_error(
        "EASYPower semi-clamping did not prefer an in-limit candidate");
  }

  // Semi-clamping is not a hard cap: if every candidate exceeds P_max, the
  // least-overshooting candidate is selected.
  if (scheduler.choose({{1, 2.1}, {3, 3.0}}) != 1) {
    throw std::runtime_error(
        "EASYPower semi-clamping did not minimize above-limit excess");
  }

  TestableEASYPowerScheduler capped_backfill(12.0, 10.0, 1.0, 1.0, 144.0, 1.0,
                                             0.0, {10.0, 2.1, 1.0, 3.0}, true,
                                             false);
  if (capped_backfill.choose({{1, 2.1}, {3, 3.0}})) {
    throw std::runtime_error(
        "EASYPower backfill cap admitted an above-limit candidate");
  }

  TestableEASYPowerScheduler capped_fcfs(12.0, 10.0, 1.0, 1.0, 144.0, 1.0, 0.0,
                                         {10.0, 2.1, 1.0, 3.0}, false, true);
  const auto fcfs_starts = capped_fcfs.schedule(100, {}, 0.0);
  if (fcfs_starts != std::vector<job_no_t>{0}) {
    throw std::runtime_error(
        "EASYPower FCFS cap did not stop the power-exceeding prefix job");
  }

  bool rejected_non_dominant_cost = false;
  try {
    TestableEASYPowerScheduler invalid(12.0, 10.0, 2.0, 1.0, 287.0);
    (void)invalid;
  } catch (const std::invalid_argument &) {
    rejected_non_dominant_cost = true;
  }
  if (!rejected_non_dominant_cost) {
    throw std::runtime_error("EASYPower accepted C_dom below w * P_max^2");
  }

  // At large scales, C_dom + 1 can round back to C_dom. Candidate ordering
  // must still preserve the mathematical dominance of the over-limit branch.
  constexpr double large_maximum = 120000000.0;
  TestableEASYPowerScheduler rounded(large_maximum, large_maximum, 1.0, 1.0,
                                     large_maximum * large_maximum, 1.0, 0.0,
                                     {0.0, large_maximum + 1.0, 0.0, 0.0});
  if (rounded.choose(
          {{1, large_maximum + 1.0}, {2, 0.0}}) != 2) {
    throw std::runtime_error(
        "EASYPower floating-point rounding broke branch dominance");
  }

  // E_Q = (10 + 2.1 + 1 + 3) * 10 = 161 and E_R = 20. The
  // unclamped target is therefore (E_R + E_Q) / H = 1.81.
  TestableEASYPowerScheduler target(12.0, 0.0, 1.0, 1.0, 144.0, 100.0, 20.0);
  target.refresh_target();
  if (std::abs(target.power_target() - 1.81) > 1e-12) {
    throw std::runtime_error("EASYPower target does not equal (E_R + E_Q) / H");
  }

  TestableEASYPowerScheduler clamped(12.0, 0.0, 1.0, 1.0, 144.0, 1.0, 20.0);
  clamped.refresh_target();
  if (clamped.power_target() != 12.0) {
    throw std::runtime_error("EASYPower target was not clamped to P_max");
  }
}

void test_easypower_load_admission_limits() {
  constexpr const char *trace_path = "/tmp/easypower_admission_limits.csv";
  {
    std::ofstream trace(trace_path);
#if DR_EVT_LEGACY_QUEUE_INPUT
    trace << "job_submit_time,num_nodes,queue,time_limit,avgpcon,minpcon,"
             "maxpcon\n"
          << "0,1,pbatch,1,5,4,6\n"
          << "0,101,pbatch,1,2,1,3\n"
          << "0,1,pbatch,1,13,12,14\n"
          << "0,100,pbatch,1,12,11,12\n"
          << "0,1,pbatch,1,3,2,4\n";
#else
    trace << "job_submit_time,num_nodes,q_id,time_limit,avgpcon,minpcon,"
             "maxpcon\n"
          << "0,1,1,1,5,4,6\n"
          << "0,101,1,1,2,1,3\n"
          << "0,1,1,1,13,12,14\n"
          << "0,100,1,1,12,11,12\n"
          << "0,1,1,1,3,2,4\n";
#endif
  }

  auto scheduler = std::make_unique<EASYPowerScheduler>(
      100, 0, 4, 12.0, 10.0, 1.0, 1.0, 144.0, job_power_function_t{},
      [](const std::vector<EASYPowerJob> &,
         const std::vector<EASYPowerJob> &, double, num_nodes_t) {
        return EASYPowerReplayResult{1.0, 0.0};
      },
      0, CircularOverflowPolicy::GROW, true, true);
  assert(scheduler->maximum_job_power_for_admission() == 12.0);

  Sim_Params params;
  params.m_infile = trace_path;
  params.m_total_nodes = 100;
  params.m_trace_type = TraceType::PCON;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_run_time_mode = RunTimeMode::LIMIT;

  std::ostringstream diagnostics;
  auto *original_stderr = std::cerr.rdbuf(diagnostics.rdbuf());
  try {
    PconSimulation simulation(params, std::move(scheduler));
    simulation.run();
    assert(simulation.get_trace().data().size() == 3u);
    assert(simulation.get_statistics().jobs_completed == 3u);
    assert(simulation.get_trace().data()[0].pcon().avgpcon == 5.0);
    assert(simulation.get_trace().data()[1].pcon().avgpcon == 12.0);
    assert(simulation.get_trace().data()[2].pcon().avgpcon == 3.0);
  } catch (...) {
    std::cerr.rdbuf(original_stderr);
    std::remove(trace_path);
    throw;
  }
  std::cerr.rdbuf(original_stderr);
  std::remove(trace_path);

  const std::string messages = diagnostics.str();
  if (messages.find("Dropped trace row 2") == std::string::npos ||
      messages.find("maximum allowed nodes") == std::string::npos ||
      messages.find("Dropped trace row 3") == std::string::npos ||
      messages.find("maximum allowed power") == std::string::npos) {
    throw std::runtime_error(
        "EASYPower did not report both loader admission rejections");
  }
}

void test_arrival_scans_only_new_jobs() {
  constexpr const char *trace_path =
      "/tmp/dr_evt_custom_scheduler_arrival_scan.csv";
  {
    std::ofstream trace(trace_path);
    trace << "job_submit_time,num_nodes,time_limit\n";
  }

  Sim_Params params;
  params.m_infile = trace_path;
  params.m_total_nodes = 100;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_run_time_mode = RunTimeMode::LIMIT;
  params.m_backfill_policy = BackfillPolicy::EASY;
  params.m_num_max_candidates = 4;

  std::vector<backfill_candidates_t> selections;
  auto selector =
      [&selections](
          const backfill_candidates_t &candidates) -> std::optional<size_t> {
    selections.push_back(candidates);
    if (selections.size() == 1) {
      return std::nullopt;
    }
    return size_t{0};
  };

  Simulation simulation(params, cost_from_job_order, selector);
  simulation.get_trace().load_data(0);
  simulation.append_job(0.0, 70, kTestQueue, 100.0);
  simulation.append_job(0.0, 50, kTestQueue, 200.0);
  simulation.append_job(0.0, 20, kTestQueue, 20.0);
  simulation.advance_to(0.0);

  // Job 2 was feasible but deliberately declined at t=0. At an arrival-only
  // event, only the newly eligible job is reconsidered.
  simulation.append_job(10.0, 10, kTestQueue, 10.0);
  simulation.advance_to(10.0);

  // When job 3 completes, resources change and the full waiting queue is
  // eligible for reconsideration, including the previously declined job 2.
  simulation.advance_to(20.0);

  assert(selections.size() == 3);
  assert((selections[0] == backfill_candidates_t{{1, 2}}));
  assert((selections[1] == backfill_candidates_t{{2, 3}}));
  assert((selections[2] == backfill_candidates_t{{1, 2}}));
}

class ObservingCustomScheduler final : public FCFSEasyCustomScheduler {
public:
  ObservingCustomScheduler()
      : FCFSEasyCustomScheduler(100, 0, BackfillPolicy::EASY, 2,
                                cost_from_job_order, select_lowest_cost) {}

  size_t selection_calls = 0;
  size_t arrival_updates = 0;
  size_t jobs_in_arrival_update = 0;
  size_t candidate_preparations = 0;
  bool fcfs_started_before_candidates = false;
  size_t completed_cycles = 0;
  size_t waiting_at_completion = 0;

protected:
  void on_jobs_became_eligible(size_t newly_eligible_begin, size_t eligible_end,
                               num_nodes_t, const running_jobs_t &,
                               sim_time_t) override {
    ++arrival_updates;
    jobs_in_arrival_update = eligible_end - newly_eligible_begin;
  }

  void on_backfill_candidates_ready(const backfill_candidates_t &, num_nodes_t,
                                    const running_jobs_t &, sim_time_t,
                                    bool fcfs_jobs_started) override {
    ++candidate_preparations;
    fcfs_started_before_candidates = fcfs_jobs_started;
  }

  std::optional<size_t> select_backfill_candidate(
      const backfill_candidates_t &candidates, num_nodes_t available_nodes,
      const running_jobs_t &running_jobs, sim_time_t current_time) override {
    ++selection_calls;
    return FCFSEasyCustomScheduler::select_backfill_candidate(
        candidates, available_nodes, running_jobs, current_time);
  }

  void on_scheduling_cycle_complete(num_nodes_t, const running_jobs_t &,
                                    sim_time_t) override {
    ++completed_cycles;
    waiting_at_completion = 0;
    const auto &entries = queued_jobs();
    for (size_t index = 0; index < eligible_job_end(); ++index) {
      waiting_at_completion += entries[index].removed ? 0 : 1;
    }
  }
};

void test_subclass_extension_hooks() {
  ObservingCustomScheduler scheduler;
  scheduler.insert_job(0, 0.0, 100.0, 70);
  scheduler.insert_job(1, 0.0, 200.0, 50);
  scheduler.insert_job(2, 0.0, 50.0, 20);
  scheduler.insert_job(3, 0.0, 20.0, 10);

  running_jobs_t running;
  const auto first = scheduler.schedule(100, running, 0.0);
  assert((first == std::vector<job_no_t>{0, 2}));
  running[0] = {0.0, 100.0, 70};
  running[2] = {0.0, 50.0, 20};

  const auto second = scheduler.schedule(10, running, 0.0);
  assert((second == std::vector<job_no_t>{3}));
  running[3] = {0.0, 20.0, 10};

  assert(scheduler.schedule(0, running, 0.0).empty());
  assert(scheduler.selection_calls == 2);
  // All four t=0 arrivals are exposed by one end-of-batch update.
  assert(scheduler.arrival_updates == 1);
  assert(scheduler.jobs_in_arrival_update == 4);
  assert(scheduler.candidate_preparations == 1);
  assert(scheduler.fcfs_started_before_candidates);
  assert(scheduler.completed_cycles == 1);
  assert(scheduler.waiting_at_completion == 1);
}

void test_current_utilization_api() {
  constexpr const char *trace_path = "/tmp/dr_evt_custom_scheduler_empty.csv";
  {
    std::ofstream trace(trace_path);
    trace << "job_submit_time,num_nodes,time_limit\n";
  }

  Sim_Params params;
  params.m_infile = trace_path;
  params.m_total_nodes = 100;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_run_time_mode = RunTimeMode::LIMIT;
  params.m_num_max_candidates = 4;

  Simulation simulation(params, cost_from_job_order, select_lowest_cost);
  simulation.get_trace().load_data(0);
  simulation.append_job(0.0, 25, kTestQueue, 100.0);
  simulation.advance_to(0.0);
  assert(std::abs(simulation.get_current_utilization() - 0.25) < 1e-12);
  simulation.advance_to(100.0);
  assert(simulation.get_current_utilization() == 0.0);
}

void test_batch_resource_area_accounting() {
  constexpr const char *trace_path =
      "/tmp/dr_evt_custom_scheduler_resource_area.csv";
  {
    std::ofstream trace(trace_path);
    trace << "job_submit_time,num_nodes,time_limit\n"
          << "0,20,10\n"
          << "0,30,10\n"
          << "10,15,100\n"
          << "10,26,100\n";
  }

  Sim_Params params;
  params.m_infile = trace_path;
  params.m_total_nodes = 100;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_run_time_mode = RunTimeMode::LIMIT;
  params.m_backfill_policy = BackfillPolicy::EASY;
  params.m_num_max_candidates = 4;

  Simulation simulation(params, cost_from_job_order, select_lowest_cost);
  simulation.run();

  // At t=10 two jobs release 20+30 nodes while two jobs start using 15+26.
  // The settled allocation after all four same-time events is 41 nodes.
  const auto stats = simulation.get_statistics();
  assert(std::abs(stats.resource_area - 4600.0) < 1e-12);
  assert(std::abs(stats.utilization - 4600.0 / (100.0 * 110.0)) < 1e-12);
  assert(std::abs(simulation.get_resource_area() - 4600.0) < 1e-12);
}

void test_warm_start_resource_area_accounting() {
  constexpr const char *trace_path =
      "/tmp/dr_evt_custom_scheduler_warm_area.csv";
  {
    std::ofstream trace(trace_path);
    trace << "job_submit_time,begin_time,end_time,num_nodes,exit_status,"
             "time_limit\n"
          << "0,1,5,2,0,4\n"
          << "3,3,4,2,0,1\n";
  }

  Sim_Params params;
  params.m_infile = trace_path;
  params.m_total_nodes = 4;
  params.m_sim_start_time = 3.0;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_run_time_mode = RunTimeMode::ACTUAL;
  params.m_num_max_candidates = 2;

  Simulation simulation(params, cost_from_job_order, select_lowest_cost);
  simulation.run();

  const auto stats = simulation.get_statistics();
  assert(stats.jobs_completed == 1);
  assert(std::abs(stats.resource_area - 6.0) < 1e-12);
  assert(std::abs(stats.utilization - 0.75) < 1e-12);
  assert(std::abs(simulation.get_resource_area() - 6.0) < 1e-12);
}

void test_matches_default_circular_easy() {
  constexpr const char *trace_path =
      "/tmp/dr_evt_custom_scheduler_equivalence.csv";
  {
    std::ofstream trace(trace_path);
    trace << "job_submit_time,num_nodes,time_limit\n";
  }

  Sim_Params params;
  params.m_infile = trace_path;
  params.m_total_nodes = 100;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_run_time_mode = RunTimeMode::LIMIT;
  params.m_backfill_policy = BackfillPolicy::EASY;
  params.m_queue_impl = QueueImplementation::CIRCULAR;
  params.m_num_max_candidates = 3;

  Simulation standard(params);
  Simulation custom(params, cost_from_job_order, select_lowest_cost);
  standard.get_trace().load_data(0);
  custom.get_trace().load_data(0);

  struct InputJob {
    sim_time_t submit_time;
    num_nodes_t nodes;
    tdiff_t run_time;
  };
  const std::vector<InputJob> jobs = {
      {0.0, 70, 100.0}, {0.0, 50, 200.0}, {0.0, 20, 50.0},
      {0.0, 10, 20.0},  {0.0, 10, 30.0},
  };

  for (const auto &job : jobs) {
    standard.append_job(job.submit_time, job.nodes, kTestQueue, job.run_time);
    custom.append_job(job.submit_time, job.nodes, kTestQueue, job.run_time);
  }
  standard.advance_to(300.0);
  custom.advance_to(300.0);

  for (job_no_t id = 0; id < jobs.size(); ++id) {
    const auto &expected = standard.get_trace().job_at(id);
    const auto &actual = custom.get_trace().job_at(id);
    assert(expected.is_scheduled() == actual.is_scheduled());
    assert(expected.get_begin_time() == actual.get_begin_time());
    assert(expected.get_end_time() == actual.get_end_time());
  }
  assert(standard.get_statistics().jobs_completed ==
         custom.get_statistics().jobs_completed);
}

// --backfill_queue_depth bounds the custom scheduler's scan before the
// candidate cap, exactly as in the built-in FCFS EASY scheduler: with the
// lowest-cost (job order) selector both produce the same schedule, whether
// the cap admits one feasible job or many.
void test_queue_depth_matches_fcfs_easy() {
  constexpr const char *trace_path = "/tmp/dr_evt_custom_scheduler_depth.csv";
  {
    std::ofstream trace(trace_path);
    trace << "job_submit_time,num_nodes,time_limit\n";
  }

  struct InputJob {
    sim_time_t submit_time;
    num_nodes_t nodes;
    tdiff_t run_time;
  };
  std::vector<InputJob> jobs;
  unsigned long state = 12345;
  const auto next = [&state](unsigned long bound) {
    state = state * 6364136223846793005UL + 1442695040888963407UL;
    return (state >> 33) % bound;
  };
  sim_time_t submit = 0.0;
  for (int i = 0; i < 400; ++i) {
    submit += static_cast<sim_time_t>(next(40));
    jobs.push_back({submit, static_cast<num_nodes_t>(1 + next(60)),
                    static_cast<tdiff_t>(10 + next(500))});
  }

  const auto run = [&](size_t depth, size_t max_candidates, bool use_custom) {
    Sim_Params params;
    params.m_infile = trace_path;
    params.m_total_nodes = 100;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_run_time_mode = RunTimeMode::LIMIT;
    params.m_backfill_policy = BackfillPolicy::EASY;
    params.m_queue_impl = QueueImplementation::CIRCULAR;
    params.m_num_max_candidates = max_candidates;
    params.m_backfill_queue_depth = depth;
    std::unique_ptr<Simulation> sim =
        use_custom ? std::make_unique<Simulation>(params, cost_from_job_order,
                                                  select_lowest_cost)
                   : std::make_unique<Simulation>(params);
    sim->get_trace().load_data(0);
    for (const auto &job : jobs) {
      sim->append_job(job.submit_time, job.nodes, kTestQueue, job.run_time);
    }
    sim->advance_to(std::numeric_limits<sim_time_t>::max());
    std::vector<sim_time_t> starts;
    for (job_no_t id = 0; id < jobs.size(); ++id) {
      starts.push_back(sim->get_trace().job_at(id).get_begin_time().first);
    }
    return starts;
  };

  const auto unlimited = run(0, 1, false);
  const auto limited = run(3, 1, false);
  // The depth must matter for this workload, or the comparison proves nothing.
  assert(unlimited != limited);
  assert(run(3, 1, true) == limited);
  assert(run(3, 64, true) == limited);
}

int write_custom_schedule(const char *input_path, const char *output_path,
                          num_nodes_t total_nodes) {
  Sim_Params params;
  params.m_infile = input_path;
  params.set_outfile(output_path);
  params.m_total_nodes = total_nodes;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_run_time_mode = RunTimeMode::LIMIT;
  params.m_backfill_policy = BackfillPolicy::EASY;
  params.m_num_max_candidates = 3;

  Simulation simulation(params, cost_from_job_order, select_lowest_cost);
  simulation.run();
  simulation.write_simulated_trace();
  return 0;
}

int main(int argc, char **argv) {
  if ((argc == 4 || argc == 5) && std::string(argv[1]) == "--write-schedule") {
    const auto total_nodes = argc == 5
                                 ? static_cast<num_nodes_t>(std::stoul(argv[4]))
                                 : num_nodes_t{100};
    return write_custom_schedule(argv[2], argv[3], total_nodes);
  }
  if (argc != 1) {
    std::cerr << "Usage: " << argv[0]
              << " [--write-schedule INPUT_CSV OUTPUT_CSV [TOTAL_NODES]]\n";
    return 2;
  }

  test_external_backfill_selection();
  test_selector_must_return_a_candidate();
  test_easypower_semiclamped_candidate_cost();
  test_easypower_load_admission_limits();
  test_arrival_scans_only_new_jobs();
  test_subclass_extension_hooks();
  test_current_utilization_api();
  test_batch_resource_area_accounting();
  test_warm_start_resource_area_accounting();
  test_matches_default_circular_easy();
  test_queue_depth_matches_fcfs_easy();
  std::cout << "Custom scheduler tests passed\n";
  return 0;
}
