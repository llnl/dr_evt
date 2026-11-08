/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#define DR_EVT_HAS_CONFIG 1
#include "sim/scheduler_fcfs_pass_hybrid_circular.hpp"
#include "sim/sim.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace dr_evt;

static void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

int main() {
  // Immediate starts consume Q, and a fresh pass receives a fresh Q at the
  // same timestamp. Selected starts remain committed across those passes.
  FCFSPassHybridCircularScheduler immediate(4, 4, BackfillPolicy::HYBRID, 0,
                                            CircularOverflowPolicy::GROW, 1, 1);
  for (job_no_t id = 0; id < 4; ++id) {
    immediate.insert_job(id, 0, 10, 1);
  }
  require(immediate.schedule(4, {}, 0) == std::vector<job_no_t>({0, 1, 2, 3}),
          "immediate starts lost across passes");
  require(immediate.last_pass_count() == 4, "immediate starts must consume Q");
  require(!immediate.last_retry_stopped(), "useful passes must not be guarded");

  // One running job and two waiting jobs, Q=R=1: reservations consume Q/R,
  // stay pending, and cause another pass. Recreating the same plan stops it.
  FCFSPassHybridCircularScheduler retry(1, 2, BackfillPolicy::HYBRID, 0,
                                        CircularOverflowPolicy::GROW, 1, 1);
  retry.insert_job(1, 0, 5, 1);
  retry.insert_job(2, 0, 5, 1);
  running_jobs_t running{{0, {0, 10, 1}}};
  require(retry.schedule(0, running, 0).empty(),
          "busy resources cannot start jobs");
  require(retry.last_pass_count() == 2 && retry.last_retry_stopped(),
          "unchanged reservation-only retries must terminate");
  require(retry.active_job_count() == 2, "reservations must stay pending");
  require(retry.schedule(1, {}, 10) == std::vector<job_no_t>({1}),
          "guard must allow starts after the next completion");

  // R=1 does not stop examinations. An allocation-only failure consumes the
  // remaining Q slot, while a larger Q reaches a later short backfill job.
  for (size_t q : {size_t{2}, size_t{3}}) {
    FCFSPassHybridCircularScheduler scheduler(
        4, 3, BackfillPolicy::HYBRID, 0, CircularOverflowPolicy::GROW, 1, q);
    scheduler.insert_job(1, 0, 5, 4);
    scheduler.insert_job(2, 0, 20, 1);
    scheduler.insert_job(3, 0, 2, 1);
    auto starts = scheduler.schedule(2, {{0, {0, 10, 2}}}, 0);
    require(starts ==
                (q == 2 ? std::vector<job_no_t>{} : std::vector<job_no_t>{3}),
            "allocation-only failures must consume Q after R is reached");
  }

  // Backfilling is interleaved with reservations, before R is exhausted.
  FCFSPassHybridCircularScheduler interleaved(
      4, 4, BackfillPolicy::HYBRID, 0, CircularOverflowPolicy::GROW, 2, 4);
  interleaved.insert_job(1, 0, 10, 3);
  interleaved.insert_job(2, 0, 2, 1);
  interleaved.insert_job(3, 0, 5, 4);
  interleaved.insert_job(4, 0, 30, 1);
  require(interleaved.schedule(2, {{0, {0, 10, 2}}}, 0) ==
              std::vector<job_no_t>{2},
          "backfill must be attempted before creating all R reservations");

  // A permanently insufficient capacity outlook cannot reserve job 1.
  // It consumes no Q and does not prevent job 2 from using the only slot.
  FCFSPassHybridCircularScheduler blocked(4, 2, BackfillPolicy::HYBRID, 0,
                                          CircularOverflowPolicy::GROW, 1, 1);
  blocked.insert_job(1, 0, 5, 2);
  blocked.insert_job(2, 0, 5, 1);
  require(blocked.schedule(1, {}, 0) == std::vector<job_no_t>{2},
          "unreservable jobs must not consume Q");
  require(blocked.schedule(4, {}, 5) == std::vector<job_no_t>{1},
          "capacity changes must reconsider blocked jobs");

  // EASY and conservative use the same accounting with different R values.
  for (auto policy : {BackfillPolicy::EASY, BackfillPolicy::CONSERVATIVE}) {
    auto scheduler = create_scheduler(
        1, 2, policy, PriorityPolicy::FCFS, QueueImplementation::CIRCULAR, 128,
        0, CircularOverflowPolicy::GROW, 0, 1, 0, 0, 0, false, true);
    require(dynamic_cast<FCFSPassHybridCircularScheduler *>(scheduler.get()) !=
                nullptr,
            "factory must select the dedicated subclass");
    scheduler->insert_job(1, 0, 5, 1);
    scheduler->insert_job(2, 0, 5, 1);
    require(scheduler->schedule(0, running, 0).empty(),
            "reservation must not start jobs");
  }
  // Unsupported trigger combinations fail at construction.
  bool rejected = false;
  try {
    (void)create_scheduler(4, 0, BackfillPolicy::EASY, PriorityPolicy::FCFS,
                           QueueImplementation::CIRCULAR, 128, 0,
                           CircularOverflowPolicy::GROW, 0, 1, 0, 1, 0, false,
                           true);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected,
          "periodic dispatch must not select event-driven pass accounting");

  // Genuine streaming can advance past a guarded reservation-only retry.
  // Checkpoint restore preserves the policy and the resulting starts.
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("dr-evt-pass-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(directory);
  const auto trace = directory / "empty.csv";
  {
    std::ofstream output(trace);
    output << "job_submit_time,num_nodes,time_limit\n";
  }
  Sim_Params params;
  params.m_infile = trace.string();
  params.set_outfile((directory / "out.csv").string());
  params.m_total_nodes = 1;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_backfill_policy = BackfillPolicy::HYBRID;
  params.m_reservation_depth = 1;
  params.m_backfill_queue_depth = 1;
  params.m_per_pass_accounting = true;
  Simulation simulation(params);
#if DR_EVT_LEGACY_QUEUE_INPUT
  const char *queue = "pbatch";
#else
  const char *queue = "1";
#endif
  simulation.append_job(0, 1, queue, 10);
  simulation.append_job(0, 1, queue, 5);
  simulation.append_job(0, 1, queue, 6);
  simulation.advance_to(0);
#ifdef DR_EVT_HAS_SER20
  std::stringstream checkpoint(std::ios::in | std::ios::out | std::ios::binary);
  simulation.save_checkpoint(checkpoint);
#endif
  simulation.advance_to(21);
  const auto expected = simulation.get_job_statuses({0, 1, 2});
  require(expected[0].start_time == 0 && expected[1].start_time == 10 &&
              expected[2].start_time == 15,
          "streaming must advance after the retry guard");
#ifdef DR_EVT_HAS_SER20
  checkpoint.seekg(0);
  Simulation resumed(params);
  resumed.load_checkpoint(checkpoint);
  resumed.advance_to(21);
  const auto restored = resumed.get_job_statuses({0, 1, 2});
  for (size_t i = 0; i < restored.size(); ++i) {
    require(restored[i].start_time == expected[i].start_time &&
                restored[i].end_time == expected[i].end_time,
            "checkpoint must preserve placement-pass scheduling");
  }
#endif
  std::filesystem::remove_all(directory);
  std::cout << "placement-pass accounting checks passed\n";
}
