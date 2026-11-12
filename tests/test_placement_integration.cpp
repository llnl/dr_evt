/******************************************************************************
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
/** @file test_placement_integration.cpp
 * @brief Real simulator placement, release ownership, restart, and option
 * checks.
 */
#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>
using namespace dr_evt;
namespace {
/** @brief Fail a behavior invariant.
 * @param[in] condition Required condition.
 * @param[in] message Failure context.
 * @throws std::runtime_error if condition is false. */
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
/** @brief Require rejection of unsupported configuration or invalid ownership.
 * @tparam Action Callable operation.
 * @param[in] action Operation expected to reject input.
 * @throws std::runtime_error if the operation succeeds. */
template <typename Action> void rejects(Action action) {
  try {
    action();
  } catch (const std::invalid_argument &) {
    return;
  }
  throw std::runtime_error("invalid input was accepted");
}
#if DR_EVT_LEGACY_QUEUE_INPUT
constexpr const char *queue = "pbatch";
#else
constexpr const char *queue = "1";
#endif
/** @brief Create common placement parameters.
 * @param[in] input Header-only simulation trace.
 * @param[in] features Compile-time strategy selection at construction.
 * @return Placement configuration with two nodes. */
Sim_Params params_for(const std::string &input, const std::string &features) {
  Sim_Params params;
  params.m_infile = input;
  params.m_total_nodes = 2;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_resource_model = "placement";
  params.m_placement_optimizations = features;
  params.m_run_time_mode = RunTimeMode::LIMIT;
  return params;
}
/** @brief Check real releases after job-record reclamation and exact restart.
 * @param[in] input Header-only input trace.
 * @param[in] output Temporary incremental trace path.
 * @param[in] features Construction-time strategy selector.
 * @throws std::runtime_error if identities, accounting, or restart differs. */
void delayed_release(const std::string &input, const std::string &output,
                     const std::string &features) {
  auto params = params_for(input, features);
  params.m_node_release_delay = 7;
  params.m_job_flush_interval = 1;
  params.set_outfile(output);
  Simulation source(params);
  const auto first = source.append_job(0, 2, queue, 5, 3);
  const auto second = source.append_job(1, 1, queue, 5, 5);
  source.advance_to(3);
  source.flush_completed_jobs();
  require(source.get_trace().num_reclaimed() == 1,
          "completed trace owner was not reclaimed");
  require(source.get_nodes_in_use() == 0 && source.get_available_nodes() == 0,
          "cleanup nodes became reusable at END");
  require(source.placement_state()->jobs.at(first).allocation.count() == 2,
          "cleanup lost exact allocation after reclamation");
  require(source.get_job_statuses({second})[0].state ==
              Simulation::Job_State::PENDING,
          "waiting job started during cleanup");
  source.advance_to(9);
  require(source.get_available_nodes() == 0,
          "cleanup was released before its event");
#if defined(DR_EVT_HAS_SER20)
  std::stringstream checkpoint(std::ios::in | std::ios::out | std::ios::binary);
  source.save_checkpoint(checkpoint);
  Simulation restored(params);
  checkpoint.seekg(0);
  restored.load_checkpoint(checkpoint);
  require(restored.placement_state()->occupied ==
              source.placement_state()->occupied,
          "checkpoint changed exact cleanup allocation");
  restored.advance_to(10);
  const auto actual = restored.get_job_statuses({second})[0];
  require(actual.start_time && *actual.start_time == 10 &&
              actual.node_ids == std::vector<num_nodes_t>{0},
          "restart did not reuse the exact released node");
  restored.advance_to(30);
  require(restored.placement_state()->jobs.empty() &&
              restored.placement_state()->occupied.count() == 0,
          "restart retained allocation after final RELEASE");
#else
  source.advance_to(10);
  const auto actual = source.get_job_statuses({second})[0];
  require(actual.start_time && *actual.start_time == 10 &&
              actual.node_ids == std::vector<num_nodes_t>{0},
          "release did not permit exact allocation");
#endif
}
/** @brief Check multibitmap selection, grouped release, diagnostics, and queue
 * depth.
 * @param[in] input Header-only simulation trace.
 * @param[in] features Construction-time optimization selector.
 * @throws std::runtime_error if an allocation or boundary is incorrect. */
void boundaries(const std::string &input, const std::string &features) {
  for (const auto &order : {"low", "high"}) {
    auto params = params_for(input, features);
    params.m_total_nodes = 65;
    params.m_placement_order = order;
    params.m_resource_model_diagnostics = true;
    params.m_backfill_policy = BackfillPolicy::HYBRID;
    params.m_reservation_depth = 2;
    params.m_backfill_queue_depth = 2;
    Simulation simulation(params);
    const auto full = simulation.append_job(0, 65, queue, 5);
    const auto head = simulation.append_job(1, 64, queue, 2);
    const auto tail = simulation.append_job(1, 1, queue, 2);
    simulation.advance_to(0);
    require(simulation.get_job_statuses({full})[0].node_ids.size() == 65,
            "multibitmap allocation omitted a valid node");
    simulation.advance_to(5);
    const auto statuses = simulation.get_job_statuses({head, tail});
    require(statuses[0].start_time == 5 && statuses[1].start_time == 5,
            "equal-time release and starts were not grouped");
    require(statuses[0].node_ids.size() == 64 &&
                statuses[1].node_ids.size() == 1,
            "queue depth lost selected allocations");
    require(statuses[1].node_ids[0] == (std::string(order) == "low" ? 64U : 0U),
            "compile-time order selected the wrong word-boundary node");
    simulation.advance_to(7);
    require(simulation.placement_state()->occupied.count() == 0 &&
                simulation.get_available_nodes() == 65,
            "completion did not restore all identities");
    const auto diagnostics = simulation.get_resource_diagnostics();
    require(diagnostics && diagnostics->calls[static_cast<size_t>(
                               ResourceOperation::Allocate)] == 3,
            "diagnostics lost committed START events");
  }
}
/** @brief Check unavailable head forecasts and aggregate approximations.
 * @param[in] input Header-only input trace.
 * @throws std::runtime_error if an expired forecast creates an invented start.
 */
void expired_forecast(const std::string &input) {
  auto params = params_for(input, "all");
  params.m_node_release_delay = 7;
  Simulation simulation(params);
  simulation.append_job(0, 2, queue, 3);
  auto waiting = simulation.append_job(1, 1, queue, 2);
  simulation.advance_to(4);
  require(simulation.get_fcfs_head_shadow_time() == -1 &&
              !simulation.get_job_statuses({waiting})[0].expected_start_time,
          "expired cleanup forecast invented a head reservation");
  const auto approximate = simulation.get_start_times_by_queue_drain(1);
  require(approximate.size() == 1,
          "placement lost the aggregate queue-drain approximation");
}
/** @brief Check transactional admission failures in the live allocation owner.
 * @throws std::runtime_error if invalid admission changes ownership or
 * occupancy. */
void invalid_ownership() {
  running_jobs_t owners;
  owners.enable_placement(65);
  auto allocation = owners.placement()->eligible.select(2);
  owners.commit(0, {0, 5, 2}, *allocation);
  const auto before = owners.placement()->occupied;
  rejects([&] { owners.commit(1, {0, 5, 2}, *allocation); });
  rejects([&] { owners.commit(1, {0, 5, 1}, *allocation); });
  rejects([&] { owners.commit(1, {0, 5, 1}, Bitmap(64, true)); });
  require(owners.size() == 1 && owners.placement()->occupied == before,
          "failed admission changed live allocation ownership");
  owners.erase(0);
  require(owners.empty() && owners.placement()->occupied.count() == 0,
          "owner release retained allocation membership");
}
/** @brief Check checkpoint continuation with a bounded examination budget.
 * @param[in] input Header-only input trace.
 * @param[in] model Resource model under test.
 * @throws std::runtime_error if restart changes the subsequent schedule. */
void restart_budget(const std::string &input, const std::string &model) {
#if defined(DR_EVT_HAS_SER20)
  auto params = params_for(input, "all");
  params.m_resource_model = model;
  params.m_backfill_policy = BackfillPolicy::HYBRID;
  params.m_reservation_depth = 2;
  params.m_backfill_queue_depth = 2;
  Simulation source(params);
  std::vector<job_no_t> jobs;
  jobs.push_back(source.append_job(0, 2, queue, 10));
  for (unsigned i = 0; i < 6; ++i)
    jobs.push_back(source.append_job(1, 1, queue, 3));
  source.advance_to(1);
  std::stringstream checkpoint(std::ios::in | std::ios::out | std::ios::binary);
  source.save_checkpoint(checkpoint);
  Simulation restored(params);
  checkpoint.seekg(0);
  restored.load_checkpoint(checkpoint);
  // A repeated advance at the saved time must preserve its examination budget.
  source.advance_to(1);
  restored.advance_to(1);
  for (double time : {10., 13., 16., 19., 22.}) {
    source.advance_to(time);
    restored.advance_to(time);
    const auto expected = source.get_job_statuses(jobs);
    const auto actual = restored.get_job_statuses(jobs);
    for (size_t i = 0; i < jobs.size(); ++i)
      require(expected[i].state == actual[i].state &&
                  expected[i].start_time == actual[i].start_time &&
                  expected[i].end_time == actual[i].end_time &&
                  expected[i].node_ids == actual[i].node_ids,
              "bounded checkpoint continuation changed scheduling");
  }
#else
  (void)input;
  (void)model;
#endif
}
/** @brief Regress capacity cleanup restoration after trace record reclamation.
 * @param[in] input Header-only input trace.
 * @param[in] output Temporary simulated trace path.
 * @throws std::runtime_error if cleanup ownership is lost. */
void capacity_cleanup(const std::string &input, const std::string &output) {
#if defined(DR_EVT_HAS_SER20)
  auto params = params_for(input, "all");
  params.m_resource_model = "capacity";
  params.m_node_release_delay = 7;
  params.m_job_flush_interval = 1;
  params.set_outfile(output);
  Simulation source(params);
  source.append_job(0, 2, queue, 5, 3);
  auto waiting = source.append_job(1, 1, queue, 5);
  source.advance_to(3);
  source.flush_completed_jobs();
  require(source.get_trace().num_reclaimed() == 1,
          "capacity trace record was not reclaimed");
  std::stringstream checkpoint(std::ios::in | std::ios::out | std::ios::binary);
  source.save_checkpoint(checkpoint);
  Simulation restored(params);
  checkpoint.seekg(0);
  restored.load_checkpoint(checkpoint);
  restored.advance_to(10);
  require(restored.get_job_statuses({waiting})[0].start_time == 10,
          "capacity cleanup checkpoint released resources incorrectly");
#else
  (void)input;
  (void)output;
#endif
}
/** @brief Require explicit rejection of unsupported model combinations.
 * @param[in] input Header-only simulation trace.
 * @throws std::runtime_error if invalid input is accepted. */
void invalid_options(const std::string &input) {
  auto params = params_for(input, "all");
  {
    Simulation simulation(params);
    const auto empty = simulation.append_job(0, 0, queue, 1);
    const auto oversized = simulation.append_job(0, 3, queue, 1);
    simulation.advance_to(0);
    const auto statuses = simulation.get_job_statuses({empty, oversized});
    require(statuses[0].state == Simulation::Job_State::REJECTED &&
                statuses[1].state == Simulation::Job_State::REJECTED,
            "empty or oversized placement was admitted");
  }
  params.m_resource_model = "unknown";
  rejects([&] { Simulation simulation(params); });
  params.m_resource_model = "placement";
  params.m_placement_order = "unknown";
  rejects([&] { Simulation simulation(params); });
  params.m_placement_order = "low";
  params.m_placement_optimizations = "unknown";
  rejects([&] { Simulation simulation(params); });
  params.m_placement_optimizations = "all";
  params.m_backfill_interval = 1;
  rejects([&] { Simulation simulation(params); });
  params.m_backfill_interval = 0;
  params.m_per_pass_accounting = true;
  rejects([&] { Simulation simulation(params); });
  params.m_per_pass_accounting = false;
  params.m_queue_impl = QueueImplementation::DEQUE;
  rejects([&] { Simulation simulation(params); });
}
} // namespace
/** @brief Run the actual simulation integration checks.
 * @return Zero on success, one on failed invariants. */
int main() {
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("dr-evt-placement-integration-" + std::to_string(getpid()));
  try {
    std::filesystem::create_directories(directory);
    const auto input = (directory / "empty.csv").string();
    {
      std::ofstream stream(input);
      stream << "job_submit_time,num_nodes,time_limit\n";
    }
    for (const auto &features :
         {"none", "fast", "batch", "scratch", "boolean", "all"}) {
      delayed_release(input,
                      (directory / (std::string(features) + ".csv")).string(),
                      features);
      boundaries(input, features);
    }
    invalid_ownership();
    expired_forecast(input);
    restart_budget(input, "placement");
    restart_budget(input, "capacity");
    capacity_cleanup(input, (directory / "capacity.csv").string());
    invalid_options(input);
    std::filesystem::remove_all(directory);
    std::cout << "Main simulator placement integration checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    std::filesystem::remove_all(directory);
    return 1;
  }
}
