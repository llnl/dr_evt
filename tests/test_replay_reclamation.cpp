/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/**
 * Regression tests for replay job-store reclamation. These use the standard
 * Trace policy used by the standalone tracer; power-field behavior remains in
 * test_pcon_trace.cpp.
 */

#include "trace/trace.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace {

constexpr const char *basic_input = "/tmp/dr_evt_reclaim_basic.csv";
constexpr const char *resource_output = "/tmp/dr_evt_reclaim_resources.csv";
constexpr const char *explicit_output = "/tmp/dr_evt_reclaim_explicit.csv";
constexpr const char *periodic_output = "/tmp/dr_evt_reclaim_periodic.csv";
constexpr const char *default_output = "/tmp/dr_evt_reclaim_default.csv";
constexpr const char *final_output = "/tmp/dr_evt_reclaim_final.csv";
constexpr const char *same_time_input = "/tmp/dr_evt_reclaim_same_time.csv";
constexpr const char *same_time_output =
    "/tmp/dr_evt_reclaim_same_time_out.csv";
constexpr const char *out_of_order_input =
    "/tmp/dr_evt_reclaim_out_of_order.csv";
constexpr const char *out_of_order_output =
    "/tmp/dr_evt_reclaim_out_of_order_out.csv";

size_t count_lines(const char *path) {
  std::ifstream input(path);
  std::string line;
  size_t lines = 0;
  while (std::getline(input, line)) {
    ++lines;
  }
  return lines;
}

// Expose only replay event insertion so each test can stop at the precise
// committed-time boundary it needs. Production run_job_trace() performs these
// same operations while walking the loaded records.
class ReplayTestTrace : public dr_evt::Trace {
public:
  using dr_evt::Trace::Trace;

  void enqueue_replay_job(dr_evt::job_no_t job_no) {
    const auto &job = job_at(job_no);
    m_ctx.m_evtq.emplace(job_no, job.get_begin_time(), true);
    m_ctx.m_evtq.emplace(job_no, job.get_end_time(), false);
    m_replay_jobs_enqueued = static_cast<size_t>(job_no) + 1;
  }
};

void write_basic_input() {
  std::ofstream input(basic_input);
  input << "job_submit_time,begin_time,end_time,num_nodes,exit_status,q_id,"
           "time_limit\n"
        << "0,1,4,2,0,1,3\n"
        << "10,11,12,1,0,1,1\n";
}

void remove_outputs() {
  std::remove(basic_input);
  std::remove(resource_output);
  std::remove(explicit_output);
  std::remove(periodic_output);
  std::remove(default_output);
  std::remove(final_output);
  std::remove(same_time_input);
  std::remove(same_time_output);
  std::remove(out_of_order_input);
  std::remove(out_of_order_output);
}

} // namespace

int main() {
  bool passed = true;
  write_basic_input();

  // Standalone replay retains records for its reporting consumers. Explicit
  // flushes reclaim only the completed front prefix and repeated calls do not
  // duplicate output.
  try {
    dr_evt::Trace trace(basic_input, "simple", "epoch", "+00:00");
    trace.set_job_store_capacity(2);
    trace.set_job_flush_interval(3);
    passed &= trace.load_data() == EXIT_SUCCESS;
    trace.run_job_trace(resource_output, 4);
    passed &= trace.data().size() == 2;
    passed &= trace.num_reclaimed() == 0;

    trace.start_simulated_trace(explicit_output);
    trace.flush_completed_jobs(4.0);
    passed &= trace.data().size() == 1;
    passed &= trace.num_reclaimed() == 1;
    trace.flush_completed_jobs(4.0);
    passed &= trace.data().size() == 1;
    passed &= trace.num_reclaimed() == 1;
    trace.flush_completed_jobs(12.0);
    passed &= trace.data().empty();
    passed &= trace.num_reclaimed() == 2;
    trace.write_simulated_trace(explicit_output);
    passed &= count_lines(explicit_output) == 3;
  } catch (const std::exception &error) {
    std::cerr << "explicit replay flush: " << error.what() << '\n';
    passed = false;
  }

  // An interval flush between replay submissions may reclaim an earlier job,
  // but must not reclaim a later record before its events have been enqueued.
  try {
    dr_evt::Trace trace(basic_input, "simple", "epoch", "+00:00");
    trace.set_job_store_capacity(2);
    trace.set_job_flush_interval(1);
    passed &= trace.load_data() == EXIT_SUCCESS;
    trace.start_simulated_trace(periodic_output);
    trace.run_job_trace({}, 4);
    passed &= trace.data().empty();
    passed &= trace.num_reclaimed() == 2;
    trace.write_simulated_trace(periodic_output);
    passed &= count_lines(periodic_output) == 3;
  } catch (const std::exception &error) {
    std::cerr << "periodic replay flush: " << error.what() << '\n';
    passed = false;
  }

  // With interval zero, the effective interval follows the resolved
  // two-record capacity: one departure does not flush; the second does.
  try {
    ReplayTestTrace trace(basic_input, "simple", "epoch", "+00:00");
    trace.set_job_store_capacity(2);
    passed &= trace.load_data() == EXIT_SUCCESS;
    trace.start_simulated_trace(default_output);
    trace.enqueue_replay_job(0);
    trace.enqueue_replay_job(1);
    trace.run_until_inclusive(4.0);
    passed &= trace.data().size() == 2;
    passed &= trace.num_reclaimed() == 0;
    trace.run_until_inclusive(12.0);
    passed &= trace.data().empty();
    passed &= trace.num_reclaimed() == 2;
    trace.write_simulated_trace(default_output);
    passed &= count_lines(default_output) == 3;
  } catch (const std::exception &error) {
    std::cerr << "default replay flush interval: " << error.what() << '\n';
    passed = false;
  }

  // Final output writes every resident record when there was no earlier
  // incremental output or reclamation.
  try {
    dr_evt::Trace trace(basic_input, "simple", "epoch", "+00:00");
    trace.set_job_store_capacity(2);
    passed &= trace.load_data() == EXIT_SUCCESS;
    trace.run_job_trace({}, 4);
    trace.write_simulated_trace(final_output);
    passed &= trace.data().size() == 2;
    passed &= trace.num_reclaimed() == 0;
    passed &= count_lines(final_output) == 3;
  } catch (const std::exception &error) {
    std::cerr << "final replay output: " << error.what() << '\n';
    passed = false;
  }

  {
    std::ofstream input(same_time_input);
    input << "job_submit_time,begin_time,end_time,num_nodes,exit_status,q_id,"
             "time_limit\n"
          << "0,1,4,2,0,1,3\n"
          << "0,2,4,1,0,1,2\n";
  }

  // Reclamation cannot occur between departures at the same timestamp.
  try {
    ReplayTestTrace trace(same_time_input, "simple", "epoch", "+00:00");
    trace.set_job_store_capacity(2);
    trace.set_job_flush_interval(1);
    passed &= trace.load_data() == EXIT_SUCCESS;
    trace.start_simulated_trace(same_time_output);
    trace.enqueue_replay_job(0);
    trace.enqueue_replay_job(1);
    passed &= trace.process_single_event(); // arrival at t=1
    passed &= trace.process_single_event(); // arrival at t=2
    passed &= trace.process_single_event(); // first departure at t=4
    passed &= trace.num_reclaimed() == 0;
    passed &= trace.data().size() == 2;
    passed &= trace.process_single_event(); // second departure at t=4
    passed &= trace.num_reclaimed() == 2;
    passed &= trace.data().empty();
    trace.write_simulated_trace(same_time_output);
    passed &= count_lines(same_time_output) == 3;
  } catch (const std::exception &error) {
    std::cerr << "same-time replay departures: " << error.what() << '\n';
    passed = false;
  }

  {
    std::ofstream input(out_of_order_input);
    input << "job_submit_time,begin_time,end_time,num_nodes,exit_status,q_id,"
             "time_limit\n"
          << "0,1,10,2,0,1,9\n"
          << "0,2,4,1,0,1,2\n";
  }

  // A completed later record remains resident behind an unfinished front
  // record; after the front completes, both form one reclaimable prefix.
  try {
    ReplayTestTrace trace(out_of_order_input, "simple", "epoch", "+00:00");
    trace.set_job_store_capacity(2);
    trace.set_job_flush_interval(1);
    passed &= trace.load_data() == EXIT_SUCCESS;
    trace.start_simulated_trace(out_of_order_output);
    trace.enqueue_replay_job(0);
    trace.enqueue_replay_job(1);
    trace.run_until_inclusive(4.0);
    passed &= trace.num_reclaimed() == 0;
    passed &= trace.data().size() == 2;
    trace.run_until_inclusive(10.0);
    passed &= trace.num_reclaimed() == 2;
    passed &= trace.data().empty();
    trace.write_simulated_trace(out_of_order_output);
    passed &= count_lines(out_of_order_output) == 3;
  } catch (const std::exception &error) {
    std::cerr << "out-of-order replay completion: " << error.what() << '\n';
    passed = false;
  }

  remove_outputs();
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
