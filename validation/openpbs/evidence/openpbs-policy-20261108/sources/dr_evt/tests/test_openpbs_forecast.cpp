/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file test_openpbs_forecast.cpp
 * @brief Verify forecast grouping, exact capacity intervals, and execution.
 */

#define DR_EVT_HAS_CONFIG 1
#include "sim/openpbs_forecast_timeline.hpp"
#include "sim/scheduler_fcfs_openpbs_circular.hpp"
#include "sim/sim.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace dr_evt;

static void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

int main() {
  // Two releases at distinct seconds belong to the same inclusive group.
  OpenPBS_Forecast_Timeline adjacent(
      Reservation_Timeline(std::map<sim_time_t, num_nodes_t>{
          {0, 1}, {10, 2}, {11, 3}, {100, 4}}));
  require(adjacent.earliest_fit(2, 2, 0) == 11,
          "adjacent releases must be checked after the entire group");
  require(adjacent.earliest_fit(1, 2, 0) == 0,
          "a current fit must not be delayed by future grouping");
  OpenPBS_Forecast_Timeline separated(
      Reservation_Timeline(std::map<sim_time_t, num_nodes_t>{
          {0, 1}, {10, 2}, {12, 3}, {100, 4}}));
  require(separated.earliest_fit(2, 2, 0) == 10,
          "a completion beyond T+1 must not enter the first group");
  OpenPBS_Forecast_Timeline lone(
      Reservation_Timeline(std::map<sim_time_t, num_nodes_t>{{0, 1}, {10, 2}}));
  require(lone.earliest_fit(2, 2, 0) == 10,
          "a lone release must not add a second to the predicted start");
  require(!lone.earliest_fit(3, 2, 0), "insufficient capacity must not fit");

  // Groups are anchored to the earliest unprocessed event, not chained by
  // proximity: {10,11} and {12,13}, rather than one group through 13.
  OpenPBS_Forecast_Timeline repeated(
      Reservation_Timeline(std::map<sim_time_t, num_nodes_t>{
          {0, 0}, {10, 1}, {11, 2}, {12, 3}, {13, 4}}));
  require(repeated.earliest_fit(2, 1, 0) == 11,
          "a group must end at its fixed T+1 endpoint");
  require(repeated.earliest_fit(3, 1, 0) == 13,
          "an unsuccessful group must advance to the next group");

  // Retain reservation start/end events even when a release cancels the
  // capacity subtraction and the underlying timeline removes that boundary.
  OpenPBS_Forecast_Timeline commitments(
      Reservation_Timeline(std::map<sim_time_t, num_nodes_t>{
          {0, 1}, {10, 2}, {11, 3}}));
  commitments.reserve(11, 3, 2);
  require(commitments.earliest_fit(1, 11.1, 0) == 13,
          "future reservations must be checked across the whole interval");
  require(commitments.earliest_fit(1, 11, 0) == 0,
          "a completion at a reservation start must fit half-open intervals");
  OpenPBS_Forecast_Timeline coalesced(
      Reservation_Timeline(std::map<sim_time_t, num_nodes_t>{
          {0, 0}, {10, 1}, {11, 2}, {12, 3}}));
  coalesced.reserve(11, 1, 10);
  require(coalesced.earliest_fit(1, 1, 0) == 11,
          "a canceled capacity boundary must retain its forecast event");

  for (auto policy : {BackfillPolicy::EASY, BackfillPolicy::HYBRID,
                      BackfillPolicy::CONSERVATIVE}) {
    auto scheduler = create_scheduler(3, 4, policy, PriorityPolicy::FCFS_OPENPBS,
                                      QueueImplementation::CIRCULAR, 128, 0,
                                      CircularOverflowPolicy::GROW, 1);
    require(dynamic_cast<FCFSOpenPBSCircularScheduler *>(scheduler.get()),
            "factory must select the separate OpenPBS scheduler");
    scheduler->insert_job(0, 0, 10, 1);
    scheduler->insert_job(1, 0, 11, 1);
    scheduler->insert_job(2, 0, 2, 2);
    scheduler->insert_job(3, 0, 11, 1);
    require(scheduler->schedule(3, {}, 0) == std::vector<job_no_t>({0, 1, 3}),
            "OpenPBS forecast must allow S at zero and leave H waiting");
  }
  bool rejected = false;
  try {
    (void)create_scheduler(3, 4, BackfillPolicy::HYBRID,
                           PriorityPolicy::FCFS_OPENPBS,
                           QueueImplementation::CIRCULAR, 128, 0,
                           CircularOverflowPolicy::GROW, 1, 0, 0, 1);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "periodic dispatch must not silently select OpenPBS mode");

  const auto directory = std::filesystem::temp_directory_path() /
      ("dr-evt-openpbs-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(directory);
  const auto input = directory / "empty.csv";
  std::ofstream(input) << "job_submit_time,num_nodes,time_limit\n";
  for (auto priority : {PriorityPolicy::FCFS, PriorityPolicy::FCFS_OPENPBS}) {
    Sim_Params params;
    params.m_infile = input.string();
    params.set_outfile((directory / "output.csv").string());
    params.m_total_nodes = 3;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_priority_policy = priority;
    params.m_backfill_policy = BackfillPolicy::HYBRID;
    params.m_reservation_depth = 1;
    params.m_per_pass_accounting = true;
    Simulation simulation(params);
#if DR_EVT_LEGACY_QUEUE_INPUT
    const char *queue = "pbatch";
#else
    const char *queue = "1";
#endif
    simulation.append_job(0, 1, queue, 10);
    simulation.append_job(0, 1, queue, 11);
    simulation.append_job(0, 2, queue, 2);
    simulation.append_job(0, 1, queue, 11);
    simulation.advance_to(0);
    const bool native = priority == PriorityPolicy::FCFS_OPENPBS;
    require(simulation.get_job_statuses({2})[0].expected_start_time ==
                (native ? 11 : 10),
            "status predictions must use the selected forecast rule");
#ifdef DR_EVT_HAS_SER20
    std::stringstream checkpoint(std::ios::in | std::ios::out | std::ios::binary);
    simulation.save_checkpoint(checkpoint);
#endif
    simulation.advance_to(30);
    const auto completed = simulation.get_job_statuses({0, 1, 2, 3});
    require(completed[0].start_time == 0 && completed[1].start_time == 0 &&
                completed[2].start_time == (native ? 11 : 10) &&
                completed[3].start_time == (native ? 0 : 11),
            "real event-driven starts must follow the chosen forecast policy");
#ifdef DR_EVT_HAS_SER20
    checkpoint.seekg(0);
    Simulation resumed(params);
    resumed.load_checkpoint(checkpoint);
    resumed.advance_to(30);
    const auto restored = resumed.get_job_statuses({0, 1, 2, 3});
    for (size_t i = 0; i < completed.size(); ++i) {
      require(restored[i].start_time == completed[i].start_time &&
                  restored[i].end_time == completed[i].end_time,
              "checkpoint restore must preserve the OpenPBS policy");
    }
#endif
  }
  std::filesystem::remove_all(directory);
  std::cout << "OpenPBS forecast grouping checks passed\n";
}
