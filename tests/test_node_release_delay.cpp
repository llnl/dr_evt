/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file test_node_release_delay.cpp
 * @brief Node release delay: sampling, limit-based planning, streaming
 * behavior, replay and warm-start boundaries, checkpoint continuation, and
 * rejection.
 */

#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"
#include "trace/node_release_delay.hpp"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace dr_evt;

namespace {

#if DR_EVT_LEGACY_QUEUE_INPUT
constexpr const char *test_queue = "pbatch";
#else
constexpr const char *test_queue = "1";
#endif

std::filesystem::path work_dir;
size_t run_number = 0;

void expect_close(double actual, double expected, double tolerance = 1e-6) {
  if (std::fabs(actual - expected) > tolerance) {
    std::cerr << "expected " << expected << ", got " << actual << '\n';
    std::abort();
  }
}

void write_file(const std::filesystem::path &path, const std::string &text) {
  std::ofstream output(path);
  output << text;
  assert(output.good());
}

std::vector<std::string> read_lines(const std::filesystem::path &path) {
  std::ifstream input(path);
  assert(input.good());
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

std::vector<std::string> split_csv(const std::string &line) {
  std::vector<std::string> fields;
  std::stringstream input(line);
  std::string field;
  while (std::getline(input, field, ',')) {
    fields.push_back(field);
  }
  return fields;
}

/// Last (settled) resource row per timestamp: free, allocated, releasing.
struct ResourceRow {
  unsigned free;
  unsigned allocated;
  unsigned releasing;
};
std::map<double, ResourceRow>
read_settled_resources(const std::filesystem::path &path) {
  const auto lines = read_lines(path);
  assert(!lines.empty());
  const bool has_releasing =
      lines.front().find("releasing_nodes") != std::string::npos;
  std::map<double, ResourceRow> rows;
  for (size_t i = 1; i < lines.size(); ++i) {
    const auto f = split_csv(lines[i]);
    assert(f.size() >= 3);
    rows[std::stod(f[0])] = {
        static_cast<unsigned>(std::stoul(f[1])),
        static_cast<unsigned>(std::stoul(f[2])),
        has_releasing ? static_cast<unsigned>(std::stoul(f[3])) : 0u};
  }
  return rows;
}

Sim_Params make_params(const std::string &infile, double delay,
                       ReleaseDelayModel model = ReleaseDelayModel::CONSTANT,
                       double spread = 0.0) {
  Sim_Params params;
  params.m_infile = infile;
  params.m_total_nodes = 100;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_run_time_mode = RunTimeMode::LIMIT;
  params.m_node_release_delay = delay;
  params.m_node_release_delay_model = model;
  params.m_node_release_delay_spread = spread;
  return params;
}

std::filesystem::path header_only_trace() {
  const auto path = work_dir / "empty.csv";
  write_file(path, "job_submit_time,num_nodes,time_limit\n");
  return path;
}

// ---------------------------------------------------------------------------

void test_model_sampling() {
  // Inactive by default and at a zero constant delay.
  Node_Release_Delay none;
  assert(!none.active());
  expect_close(none.sample(64), 0.0);
  Node_Release_Delay zero(0.0, ReleaseDelayModel::EXPONENTIAL, 0.0, 1);
  assert(!zero.active());

  // Constant: every sample equals the delay for every size.
  Node_Release_Delay constant(30.0, ReleaseDelayModel::CONSTANT, 0.0, 1);
  assert(constant.active());
  expect_close(constant.sample(1), 30.0);
  expect_close(constant.sample(100), 30.0);

  // Expected maxima used below: 30 * H(n) for exponential per-node times,
  // lo + (hi - lo) * n / (n + 1) for uniform ones.
  double harmonic8 = 0.0;
  for (unsigned n = 1; n <= 8; ++n) {
    harmonic8 += 1.0 / n;
  }
  const double expected_expo_8 = 30.0 * harmonic8;
  const double expected_uni_4 = 0.0 + 70.0 * 4.0 / 5.0;

  Node_Release_Delay uni(30.0, ReleaseDelayModel::UNIFORM, 40.0, 1);
  // A zero center with a positive spread still delays.
  assert(Node_Release_Delay(0.0, ReleaseDelayModel::UNIFORM, 5.0, 1).active());

  // Samples: nonnegative, within the uniform bounds, deterministic per seed,
  // different across seeds, and with a mean close to the expected maximum of
  // n draws.
  Node_Release_Delay a(30.0, ReleaseDelayModel::EXPONENTIAL, 0.0, 42);
  Node_Release_Delay b(30.0, ReleaseDelayModel::EXPONENTIAL, 0.0, 42);
  Node_Release_Delay c(30.0, ReleaseDelayModel::EXPONENTIAL, 0.0, 43);
  double sum_a = 0.0;
  bool differs_from_c = false;
  constexpr int draws = 40000;
  for (int i = 0; i < draws; ++i) {
    const double sa = a.sample(8);
    const double sb = b.sample(8);
    const double sc = c.sample(8);
    assert(sa >= 0.0 && std::isfinite(sa));
    assert(sa == sb);
    differs_from_c = differs_from_c || sa != sc;
    sum_a += sa;
  }
  assert(differs_from_c);
  // E[max of 8 Exp(30)] = 30 * H(8) ~ 81.5; the sample mean's standard
  // error is about 0.2 at this draw count.
  expect_close(sum_a / draws, expected_expo_8, 1.5);

  double sum_u = 0.0;
  for (int i = 0; i < draws; ++i) {
    const double s = uni.sample(4);
    assert(s >= 0.0 && s <= 70.0);
    sum_u += s;
  }
  expect_close(sum_u / draws, expected_uni_4, 1.0);

  // Validation.
  bool rejected = false;
  try {
    Node_Release_Delay bad(-1.0, ReleaseDelayModel::CONSTANT, 0.0, 1);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  std::cout << "  model sampling: ok\n";
}

void test_streaming_constant_delay() {
  // 100 nodes, 30 s delay. A: 70 nodes for 200 s at t=0. B: 45 nodes at
  // t=10 cannot fit. The scheduler plans at the time limit, as production
  // does, so B's reservation is A's limit, 200, although A's nodes will
  // only return at 230. C: 20 nodes for 50 s at t=20 backfills (ends at
  // 70 < 200; its nodes release at 100).
  auto params = make_params(header_only_trace().string(), 30.0);
  Simulation sim(params);
  sim.append_job(0.0, 70, test_queue, 200.0);
  sim.append_job(10.0, 45, test_queue, 300.0);
  sim.append_job(20.0, 20, test_queue, 50.0);

  sim.advance_to(75.0);
  assert(sim.get_nodes_in_use() == 70);
  assert(sim.get_nodes_releasing() == 20);
  assert(sim.get_available_nodes() == 10);
  assert(sim.get_statistics().jobs_running == 1);
  expect_close(sim.get_fcfs_head_shadow_time(), 200.0);
  {
    // C is in cleanup and contributes no future reservation release. A is
    // still running, so its time limit is the sole projected release.
    const auto window = sim.get_backfill_window();
    assert(window.releases.size() == 1);
    expect_close(window.releases.front().time, 200.0);
    assert(window.releases.front().nodes_released == 70);
  }

  sim.advance_to(100.0);
  assert(sim.get_nodes_releasing() == 0);
  assert(sim.get_available_nodes() == 30);

  sim.advance_to(210.0);
  assert(sim.get_nodes_in_use() == 0);
  assert(sim.get_nodes_releasing() == 70);
  assert(sim.get_statistics().jobs_running == 0);
  // Once the job has ended, cleanup capacity is unavailable but creates no
  // future reservation release.
  const auto window = sim.get_backfill_window();
  assert(window.releases.empty());

  sim.advance_to(std::numeric_limits<sim_time_t>::max());
  const auto statuses = sim.get_job_statuses({0, 1, 2});
  expect_close(*statuses[0].start_time, 0.0);
  expect_close(*statuses[1].start_time, 230.0);
  expect_close(*statuses[1].end_time, 530.0);
  expect_close(*statuses[2].start_time, 20.0);
  // The run ends at the final node release, after the last job end.
  expect_close(sim.get_current_time(), 560.0);
  assert(sim.get_nodes_releasing() == 0);
  assert(sim.get_available_nodes() == 100);

  // Cleanup time is not utilization: 70*200 + 45*300 + 20*50 node-seconds
  // over 100 nodes through the last completion at 530.
  const auto stats = sim.get_statistics();
  expect_close(stats.resource_area, 70.0 * 200 + 45.0 * 300 + 20.0 * 50);
  std::cout << "  streaming constant delay: ok\n";
}

void test_backfill_admitted_up_to_the_shadow() {
  // Planning ignores the delay, so a candidate is admitted on its projected
  // end alone. A: 70 nodes for 200 s at t=0; B: 45 nodes at t=10 waits with
  // its reservation at A's limit, 200. D: 20 nodes for 150 s at t=20 ends at
  // 170 (< 200) and is admitted although its nodes release only at 200, the
  // shadow itself. The head then slips to 230, when A's nodes return.
  auto params = make_params(header_only_trace().string(), 30.0);
  Simulation sim(params);
  sim.append_job(0.0, 70, test_queue, 200.0);
  sim.append_job(10.0, 45, test_queue, 300.0);
  sim.advance_to(10.0);
  expect_close(sim.get_fcfs_head_shadow_time(), 200.0);
  sim.append_job(20.0, 20, test_queue, 150.0);
  sim.advance_to(20.0);
  const auto d_status = sim.get_job_statuses({2}).front();
  assert(d_status.start_time.has_value());
  expect_close(*d_status.start_time, 20.0);

  sim.advance_to(200.0);
  // A ended at 200 and D's nodes release at 200; all 90 nodes are still
  // unavailable to B at this instant.
  assert(sim.get_nodes_in_use() == 0);
  assert(sim.get_nodes_releasing() == 70);
  assert(sim.get_available_nodes() == 30);
  sim.advance_to(std::numeric_limits<sim_time_t>::max());
  const auto b_status = sim.get_job_statuses({1}).front();
  expect_close(*b_status.start_time, 230.0);
  std::cout << "  backfill admitted up to the shadow: ok\n";
}

void test_exponential_delay_grows_with_size() {
  // Same workload under the exponential model: the schedule stays feasible,
  // every release follows its job's end by a positive delay, and planning
  // still projects the 70-node job's release at its limit.
  auto params = make_params(header_only_trace().string(), 30.0,
                            ReleaseDelayModel::EXPONENTIAL);
  Simulation sim(params);
  sim.append_job(0.0, 70, test_queue, 200.0);
  sim.append_job(10.0, 45, test_queue, 300.0);
  sim.advance_to(10.0);
  expect_close(sim.get_fcfs_head_shadow_time(), 200.0);
  sim.advance_to(200.0);
  assert(sim.get_nodes_releasing() == 70);
  assert(sim.get_nodes_in_use() == 0);
  // Cleanup nodes do not create a future reservation release. They become
  // schedulable only when the RELEASE event is processed.
  const auto window = sim.get_backfill_window();
  assert(window.releases.empty());
  sim.advance_to(std::numeric_limits<sim_time_t>::max());
  const auto statuses = sim.get_job_statuses({1});
  assert(*statuses[0].start_time > 200.0);
  std::cout << "  exponential delay: ok\n";
}

void test_cleanup_release_is_not_a_reservation_prediction() {
  // A ends at 200 then cleans up through 230. At 210 B (45 nodes) is
  // blocked by cleanup. Its shadow must remain absent rather than projecting
  // the sampled cleanup release at 230; RELEASE is the next schedulable
  // decision point.
  auto params = make_params(header_only_trace().string(), 30.0);
  Simulation sim(params);
  sim.append_job(0.0, 70, test_queue, 200.0);
  sim.advance_to(200.0);
  assert(sim.get_nodes_releasing() == 70);
  sim.append_job(210.0, 45, test_queue, 300.0);
  sim.advance_to(210.0);
  const auto before_release = sim.get_job_statuses({1});
  assert(!before_release[0].start_time.has_value());
  assert(sim.get_fcfs_head_shadow_time() == -1.0);
  sim.advance_to(230.0);
  const auto after_release = sim.get_job_statuses({1});
  expect_close(*after_release[0].start_time, 230.0);
  std::cout << "  cleanup release is not a reservation prediction: ok\n";
}

void test_replay_is_not_delayed() {
  const auto trace_path = work_dir / "replay.csv";
  write_file(trace_path,
             "job_submit_time,begin_time,end_time,num_nodes,exit_status,"
             "time_limit\n"
             "0,0,200,70,0,200\n"
             "10,200,500,45,0,300\n"
             "20,20,70,20,0,50\n");
  std::map<double, ResourceRow> without;
  std::map<double, ResourceRow> with;
  for (int pass = 0; pass < 2; ++pass) {
    const double delay = pass == 0 ? 0.0 : 30.0;
    auto params = make_params(trace_path.string(), delay);
    params.m_run_time_mode = RunTimeMode::ACTUAL;
    const auto jobs =
        work_dir / ("replay_jobs_" + std::to_string(pass) + ".csv");
    const auto res = work_dir / ("replay_res_" + std::to_string(pass) + ".csv");
    params.set_outfile(jobs.string());
    params.set_resource_trace(res.string());
    Simulation sim(params);
    sim.run();
    sim.write_simulated_trace();
    sim.write_resource_trace(res.string());
    assert(sim.get_nodes_releasing() == 0);
    const auto lines = read_lines(res);
    assert(lines.front() == "time,free_nodes,allocated_nodes");
    (pass == 0 ? without : with) = read_settled_resources(res);
  }
  assert(without.size() == with.size());
  for (const auto &[time, row] : without) {
    assert(with.count(time) == 1);
    assert(with[time].free == row.free);
    assert(with[time].allocated == row.allocated);
  }
  std::cout << "  replay ignores the delay: ok\n";
}

void test_warm_start_applies_from_boundary() {
  // Boundary at t=100. Job 0 ended at 95 (history: released at 95, no
  // delay). Job 1 is running at the boundary and ends at 150: its nodes are
  // delayed until 180, so job 2 (needs 60 of 100 nodes) starts at 180.
  const auto trace_path = work_dir / "warm.csv";
  write_file(trace_path,
             "job_submit_time,begin_time,end_time,num_nodes,exit_status,"
             "time_limit\n"
             "0,0,95,50,0,95\n"
             "1,1,150,50,0,149\n"
             "100,100,160,60,0,60\n");
  auto params = make_params(trace_path.string(), 30.0);
  params.m_run_time_mode = RunTimeMode::ACTUAL;
  params.m_sim_start_time = 100.0;
  const auto jobs = work_dir / "warm_jobs.csv";
  const auto res = work_dir / "warm_res.csv";
  params.set_outfile(jobs.string());
  params.set_resource_trace(res.string());
  Simulation sim(params);
  sim.run();
  sim.write_simulated_trace();
  sim.write_resource_trace(res.string());

  const auto job_lines = read_lines(jobs);
  assert(job_lines.size() == 2); // header + job 2
  const auto f = split_csv(job_lines[1]);
  expect_close(std::stod(f[1]), 180.0);
  expect_close(std::stod(f[2]), 240.0);

  const auto rows = read_settled_resources(res);
  assert(rows.at(100.0).allocated == 50 && rows.at(100.0).releasing == 0);
  assert(rows.at(150.0).allocated == 0 && rows.at(150.0).releasing == 50);
  assert(rows.at(180.0).allocated == 60 && rows.at(180.0).releasing == 0);
  std::cout << "  warm start from the boundary: ok\n";
}

#if defined(DR_EVT_HAS_SER20)
void test_checkpoint_across_pending_release() {
  auto params = make_params(header_only_trace().string(), 30.0,
                            ReleaseDelayModel::EXPONENTIAL);
  Simulation uninterrupted(params);
  uninterrupted.append_job(0.0, 70, test_queue, 200.0);
  uninterrupted.append_job(10.0, 45, test_queue, 300.0);
  uninterrupted.append_job(20.0, 20, test_queue, 50.0);
  // Job 0 has ended and its nodes are releasing; job 1 waits for them.
  uninterrupted.advance_to(205.0);
  assert(uninterrupted.get_nodes_releasing() == 70);
  const auto saved_window = uninterrupted.get_backfill_window();

  std::stringstream checkpoint(std::ios::in | std::ios::out | std::ios::binary);
  uninterrupted.save_checkpoint(checkpoint);
  uninterrupted.append_job(400.0, 10, test_queue, 100.0);
  uninterrupted.advance_to(std::numeric_limits<sim_time_t>::max());
  const auto expected = uninterrupted.get_job_statuses({0, 1, 2, 3});
  const auto expected_stats = uninterrupted.get_statistics();

  checkpoint.seekg(0);
  Simulation restarted(params);
  restarted.load_checkpoint(checkpoint);
  assert(restarted.get_nodes_releasing() == 70);
  assert(restarted.get_available_nodes() == 30);
  const auto restored_window = restarted.get_backfill_window();
  assert(restored_window.releases.size() == saved_window.releases.size());
  assert(restored_window.releases.empty());
  restarted.append_job(400.0, 10, test_queue, 100.0);
  restarted.advance_to(std::numeric_limits<sim_time_t>::max());
  const auto actual = restarted.get_job_statuses({0, 1, 2, 3});
  for (size_t i = 0; i < expected.size(); ++i) {
    assert(actual[i].state == expected[i].state);
    assert(actual[i].start_time == expected[i].start_time);
    assert(actual[i].end_time == expected[i].end_time);
  }
  const auto actual_stats = restarted.get_statistics();
  assert(actual_stats.current_time == expected_stats.current_time);
  assert(actual_stats.jobs_completed == expected_stats.jobs_completed);
  assert(restarted.get_nodes_releasing() == 0);

  // A different delay configuration is rejected on restore.
  checkpoint.clear();
  checkpoint.seekg(0);
  auto other = params;
  other.m_node_release_delay = 31.0;
  Simulation mismatched(other);
  bool rejected = false;
  try {
    mismatched.load_checkpoint(checkpoint);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  assert(rejected);
  std::cout << "  checkpoint across a pending release: ok\n";
}
#endif

void test_rejected_configuration() {
  bool rejected = false;
  try {
    Simulation sim(make_params(header_only_trace().string(), -5.0));
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  rejected = false;
  try {
    Simulation sim(make_params(header_only_trace().string(), 5.0,
                               ReleaseDelayModel::UNIFORM, -1.0));
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  std::cout << "  rejected configuration: ok\n";
}

} // namespace

int main() {
  work_dir = std::filesystem::temp_directory_path() /
             ("dr_evt_node_release_delay_" + std::to_string(::getpid()));
  std::filesystem::create_directories(work_dir);
  (void)run_number;
  std::cout << "Node release delay tests\n";
  test_model_sampling();
  test_streaming_constant_delay();
  test_backfill_admitted_up_to_the_shadow();
  test_exponential_delay_grows_with_size();
  test_cleanup_release_is_not_a_reservation_prediction();
  test_replay_is_not_delayed();
  test_warm_start_applies_from_boundary();
#if defined(DR_EVT_HAS_SER20)
  test_checkpoint_across_pending_release();
#endif
  test_rejected_configuration();
  std::filesystem::remove_all(work_dir);
  std::cout << "All node release delay tests passed\n";
  return EXIT_SUCCESS;
}
