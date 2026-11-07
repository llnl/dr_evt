/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace dr_evt;

namespace {

constexpr double kTolerance = 1e-4;

struct JobRow {
  double submit;
  double begin;
  double end;
  unsigned nodes;
};

struct ResourceRow {
  unsigned free;
  unsigned allocated;
};

struct RunResult {
  Simulation::Statistics stats;
  std::vector<JobRow> jobs;
  std::map<double, ResourceRow> settled_resources;
};

std::filesystem::path work_dir;
size_t run_number = 0;

void expect_close(double actual, double expected) {
  if (std::fabs(actual - expected) > kTolerance) {
    std::cerr << "expected " << expected << ", got " << actual << '\n';
    std::abort();
  }
}

void write_file(const std::filesystem::path &path, const std::string &text) {
  std::ofstream output(path);
  output << text;
  assert(output.good());
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

std::vector<JobRow> read_jobs(const std::filesystem::path &path) {
  std::ifstream input(path);
  assert(input.good());
  std::string line;
  assert(std::getline(input, line));
  std::vector<JobRow> jobs;
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    const auto fields = split_csv(line);
    assert(fields.size() >= 4);
    jobs.push_back({std::stod(fields[0]), std::stod(fields[1]),
                    std::stod(fields[2]),
                    static_cast<unsigned>(std::stoul(fields[3]))});
  }
  return jobs;
}

std::map<double, ResourceRow>
read_settled_resources(const std::filesystem::path &path,
                       double earliest = -1.0) {
  std::ifstream input(path);
  assert(input.good());
  std::string line;
  assert(std::getline(input, line));
  std::map<double, ResourceRow> rows;
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    const auto fields = split_csv(line);
    assert(fields.size() >= 3);
    const double time = std::stod(fields[0]);
    if (time + kTolerance >= earliest) {
      // Assignment deliberately keeps only the final, settled sample when
      // several arrivals/departures/capacity changes share one timestamp.
      rows[time] = {static_cast<unsigned>(std::stoul(fields[1])),
                    static_cast<unsigned>(std::stoul(fields[2]))};
    }
  }
  return rows;
}

RunResult
run_trace(const std::string &trace, double sim_start_time, unsigned total_nodes,
          PriorityPolicy priority = PriorityPolicy::FCFS,
          BackfillPolicy backfill = BackfillPolicy::EASY,
          QueueImplementation queue = QueueImplementation::CIRCULAR,
          const std::string &capacity = {},
          RunTimeMode run_time_mode = RunTimeMode::ACTUAL,
          DistributionType run_time_distribution = DistributionType::NORMAL,
          double run_time_scale = 1.0, double run_time_stddev = 0.0,
          double max_time = -1.0, double backfill_interval = 0.0,
          WarmStartWaitQueuePolicy warm_start_wait_queue =
              WarmStartWaitQueuePolicy::DROP,
          size_t reservation_depth = 0, size_t backfill_queue_depth = 0,
          double dispatch_interval = 0.0, size_t dispatch_queue_depth = 0,
          bool dispatch_on_events = false) {
  const std::string stem = "run_" + std::to_string(run_number++);
  const auto trace_path = work_dir / (stem + ".csv");
  const auto job_path = work_dir / (stem + ".jobs.csv");
  const auto resource_path = work_dir / (stem + ".resources.csv");
  write_file(trace_path, trace);

  Sim_Params params;
  params.m_infile = trace_path.string();
  params.m_total_nodes = total_nodes;
  params.m_sim_start_time = sim_start_time;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_run_time_mode = run_time_mode;
  params.m_run_time_distribution = run_time_distribution;
  params.m_run_time_scale = run_time_scale;
  params.m_run_time_stddev = run_time_stddev;
  if (max_time >= 0.0) {
    params.m_max_time = max_time;
    params.m_is_time_set = true;
  }
  params.m_priority_policy = priority;
  params.m_backfill_policy = backfill;
  params.m_queue_impl = queue;
  params.m_backfill_interval = backfill_interval;
  params.m_warm_start_wait_queue = warm_start_wait_queue;
  params.m_reservation_depth = reservation_depth;
  params.m_backfill_queue_depth = backfill_queue_depth;
  params.m_dispatch_interval = dispatch_interval;
  params.m_dispatch_queue_depth = dispatch_queue_depth;
  params.m_dispatch_on_events = dispatch_on_events;
  params.m_block_size = 4;
  params.m_job_flush_interval = 100000;
  params.m_resource_history_capacity = 100000;
  params.m_msec_output = true;
  params.set_outfile(job_path.string());
  params.set_resource_trace(resource_path.string());
  if (!capacity.empty()) {
    const auto capacity_path = work_dir / (stem + ".capacity.csv");
    write_file(capacity_path, capacity);
    params.m_capacity_schedule = capacity_path.string();
  }

  Simulation simulation(params);
  simulation.run();
  const auto stats = simulation.get_statistics();
  simulation.write_simulated_trace();
  simulation.write_resource_trace(resource_path.string());
  return {stats, read_jobs(job_path), read_settled_resources(resource_path)};
}

void expect_job(const JobRow &job, double submit, double begin, double end,
                unsigned nodes) {
  expect_close(job.submit, submit);
  expect_close(job.begin, begin);
  expect_close(job.end, end);
  assert(job.nodes == nodes);
}

void test_boundary_classification_and_statistics() {
  const std::string trace =
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
      "0,0,5,2,0,5\n"     // completed before t
      "1,2,10,3,0,8\n"    // departure exactly at t
      "2,4,15,2,0,11\n"   // active seed, simultaneous departure
      "3,5,15,3,0,10\n"   // active seed, simultaneous departure
      "4,12,14,1,0,2\n"   // inherited waiter
      "9,10,11,1,0,1\n"   // inherited waiter beginning at t
      "10,10,14,4,0,4\n"  // boundary arrival: admitted
      "11,12,14,5,0,2\n"; // future arrival: rescheduled

  // Legacy compatibility leaves the two inherited rows out of the queue.
  const auto dropped = run_trace(trace, 10.0, 10);
  assert(dropped.jobs.size() == 2);

  const auto result = run_trace(
      trace, 10.0, 10, PriorityPolicy::FCFS, BackfillPolicy::EASY,
      QueueImplementation::CIRCULAR, {}, RunTimeMode::ACTUAL,
      DistributionType::NORMAL, 1.0, 0.0, -1.0, 0.0,
      WarmStartWaitQueuePolicy::INHERIT);
  assert(result.jobs.size() == 4);
  expect_job(result.jobs[0], 4.0, 10.0, 12.0, 1);
  expect_job(result.jobs[1], 9.0, 10.0, 11.0, 1);
  expect_job(result.jobs[2], 10.0, 11.0, 15.0, 4);
  expect_job(result.jobs[3], 11.0, 15.0, 17.0, 5);

  const auto &stats = result.stats;
  assert(stats.jobs_submitted == 4);
  assert(stats.jobs_completed == 4);
  assert(stats.jobs_running == 0);
  assert(stats.jobs_waiting == 0);
  expect_close(stats.current_time, 17.0);
  assert(stats.total_nodes == 10);
  assert(stats.nodes_in_use == 0);
  assert(stats.nodes_available == 10);
  expect_close(stats.resource_area, 54.0);
  expect_close(stats.utilization, 54.0 / 70.0);
  expect_close(stats.avg_wait_time, 3.0);
  expect_close(stats.avg_run_time, 2.25);
  expect_close(stats.avg_turnaround_time, 5.25);
  expect_close(stats.avg_bounded_slowdown, 1.0);
  expect_close(stats.makespan, 17.0);

  const std::map<double, unsigned> expected = {
      {10.0, 7}, {11.0, 10}, {12.0, 9}, {15.0, 5}, {17.0, 0}};
  assert(result.settled_resources.size() == expected.size());
  for (const auto &[time, allocated] : expected) {
    assert(result.settled_resources.at(time).allocated == allocated);
  }
}

void test_max_time_is_an_inclusive_event_boundary() {
  const std::string simulation_trace =
      "job_submit_time,num_nodes,time_limit,actual_run_time\n"
      "0,2,5,5\n"
      "5,2,10,10\n"
      "20,1,1,1\n";
  const auto simulation =
      run_trace(simulation_trace, 0.0, 2, PriorityPolicy::FCFS,
                BackfillPolicy::EASY, QueueImplementation::CIRCULAR, {},
                RunTimeMode::ACTUAL, DistributionType::NORMAL, 1.0, 0.0, 5.0);
  expect_close(simulation.stats.current_time, 5.0);
  assert(simulation.stats.jobs_completed == 1);
  assert(simulation.stats.jobs_running == 1);
  assert(simulation.stats.nodes_in_use == 2);
  assert(simulation.jobs.size() == 1);
  expect_job(simulation.jobs[0], 0.0, 0.0, 5.0, 2);
  assert(simulation.settled_resources.at(5.0).allocated == 2);

  const std::string replay_trace =
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
      "0,0,5,2,0,5\n"
      "4,5,10,2,0,5\n"
      "11,11,12,1,0,1\n";
  const auto replay =
      run_trace(replay_trace, 0.0, 4, PriorityPolicy::FCFS,
                BackfillPolicy::EASY, QueueImplementation::CIRCULAR, {},
                RunTimeMode::ACTUAL, DistributionType::NORMAL, 1.0, 0.0, 5.0);
  expect_close(replay.stats.current_time, 5.0);
  assert(replay.stats.jobs_completed == 1);
  assert(replay.stats.jobs_running == 1);
  assert(replay.stats.nodes_in_use == 2);
  assert(replay.jobs.size() == 1);
  expect_job(replay.jobs[0], 0.0, 0.0, 5.0, 2);

  const std::string warm_trace =
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
      "0,2,15,2,0,13\n"
      "10,20,24,2,0,4\n";
  const auto warm =
      run_trace(warm_trace, 10.0, 4, PriorityPolicy::FCFS, BackfillPolicy::EASY,
                QueueImplementation::CIRCULAR, {}, RunTimeMode::ACTUAL,
                DistributionType::NORMAL, 1.0, 0.0, 12.0);
  expect_close(warm.stats.current_time, 12.0);
  assert(warm.stats.jobs_completed == 0);
  assert(warm.stats.jobs_running == 2);
  assert(warm.stats.nodes_in_use == 4);
  assert(warm.jobs.empty());
}

void test_no_live_history_and_empty_tail() {
  const std::string future =
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
      "0,1,4,3,0,3\n"
      "12,13,15,2,0,2\n";
  const auto with_future = run_trace(future, 10.0, 5);
  assert(with_future.jobs.size() == 1);
  expect_job(with_future.jobs[0], 12.0, 12.0, 14.0, 2);
  assert(with_future.settled_resources.at(10.0).allocated == 0);
  assert(with_future.stats.jobs_submitted == 1);
  assert(with_future.stats.jobs_completed == 1);
  expect_close(with_future.stats.resource_area, 4.0);

  const std::string finished =
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
      "0,1,3,2,0,2\n"
      "1,3,7,4,0,4\n";
  const auto empty = run_trace(finished, 20.0, 8);
  assert(empty.jobs.empty());
  assert(empty.stats.jobs_submitted == 0);
  assert(empty.stats.jobs_completed == 0);
  expect_close(empty.stats.current_time, 20.0);
  expect_close(empty.stats.resource_area, 0.0);
  expect_close(empty.stats.utilization, 0.0);
  assert(empty.settled_resources.size() == 1);
  assert(empty.settled_resources.at(20.0).allocated == 0);

  const std::string header_only =
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n";
  const auto no_records = run_trace(header_only, 7.0, 4);
  assert(no_records.jobs.empty());
  assert(no_records.stats.jobs_submitted == 0);
  assert(no_records.stats.jobs_completed == 0);
  assert(no_records.settled_resources.size() == 1);
  assert(no_records.settled_resources.at(7.0).allocated == 0);
}

void test_zero_start_preserves_full_replay() {
  const std::string trace =
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
      "0,2,5,2,0,3\n"
      "1,10,12,1,0,2\n";

  // Zero is the disabled sentinel for replay-based warm start. Replay therefore
  // preserves both historical schedules instead of rescheduling them from 0.
  const auto result = run_trace(trace, 0.0, 4);
  assert(result.jobs.size() == 2);
  expect_job(result.jobs[0], 0.0, 2.0, 5.0, 2);
  expect_job(result.jobs[1], 1.0, 10.0, 12.0, 1);
}

void test_staggered_and_simultaneous_historical_departures() {
  const std::string trace =
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
      "0,1,11,2,0,10\n"
      "1,2,13,3,0,11\n"
      "2,3,13,4,0,10\n"
      "10,10,15,1,0,5\n";
  const auto result = run_trace(trace, 10.0, 10);
  assert(result.jobs.size() == 1);
  expect_job(result.jobs[0], 10.0, 10.0, 15.0, 1);
  assert(result.settled_resources.at(10.0).allocated == 10);
  assert(result.settled_resources.at(11.0).allocated == 8);
  assert(result.settled_resources.at(13.0).allocated == 1);
  assert(result.settled_resources.at(15.0).allocated == 0);
  expect_close(result.stats.resource_area, 28.0);
  expect_close(result.stats.utilization, 0.56);
}

void test_capacity_boundaries_and_overcommit() {
  const std::string trace =
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
      "0,1,14,6,0,13\n"
      "10,10,13,2,0,3\n";
  const std::string capacity =
      "time,total_nodes\n"
      "0,10\n"   // before t
      "5,8\n"    // before t
      "10,4\n"   // exactly at t: inherited allocation now overcommits
      "12,10\n"; // after t: wakes the waiting boundary job

  const auto result =
      run_trace(trace, 10.0, 10, PriorityPolicy::FCFS, BackfillPolicy::EASY,
                QueueImplementation::CIRCULAR, capacity);
  assert(result.jobs.size() == 1);
  expect_job(result.jobs[0], 10.0, 12.0, 15.0, 2);
  assert(result.stats.jobs_submitted == 1);
  assert(result.stats.jobs_completed == 1);
  expect_close(result.stats.resource_area, 30.0);
  // Effective capacity is 6 while the reduction to 4 drains (t=10..12),
  // then 10 through completion: capacity area = 6*2 + 10*3 = 42.
  expect_close(result.stats.utilization, 30.0 / 42.0);

  const auto &at_t = result.settled_resources.at(10.0);
  assert(at_t.allocated == 6 && at_t.free == 0);
  const auto &after_t = result.settled_resources.at(12.0);
  assert(after_t.allocated == 8 && after_t.free == 2);
  assert(result.settled_resources.at(14.0).allocated == 2);
  assert(result.settled_resources.at(15.0).allocated == 0);
}

void test_capacity_change_at_final_warm_departure() {
  const std::string trace =
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
      "0,1,15,4,0,14\n"   // final warmup departure
      "10,12,15,6,0,3\n"; // needs the capacity increase at t=15
  const std::string capacity =
      "time,total_nodes\n"
      "0,4\n"
      "15,8\n"; // simultaneous with the last warmup departure

  const auto result =
      run_trace(trace, 10.0, 8, PriorityPolicy::FCFS, BackfillPolicy::EASY,
                QueueImplementation::CIRCULAR, capacity);
  assert(result.jobs.size() == 1);
  expect_job(result.jobs[0], 10.0, 15.0, 18.0, 6);

  const auto &at_boundary = result.settled_resources.at(10.0);
  assert(at_boundary.allocated == 4 && at_boundary.free == 0);
  const auto &at_transition = result.settled_resources.at(15.0);
  assert(at_transition.allocated == 6 && at_transition.free == 2);
  const auto &at_completion = result.settled_resources.at(18.0);
  assert(at_completion.allocated == 0 && at_completion.free == 8);

  expect_close(result.stats.resource_area, 38.0);
  expect_close(result.stats.utilization, 38.0 / 44.0);
}

void test_fractional_boundary() {
  const std::string trace =
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
      "0,9.500,10.250,2,0,1\n" // ends exactly at t
      "1,9.750,10.750,3,0,1\n" // inherited occupancy
      "10.250,10.250,10.750,2,0,1\n"
      "10.500,10.500,11.250,3,0,1\n";
  const auto result = run_trace(trace, 10.25, 5);
  assert(result.jobs.size() == 2);
  expect_job(result.jobs[0], 10.25, 10.25, 10.75, 2);
  expect_job(result.jobs[1], 10.5, 10.75, 11.5, 3);
  assert(result.settled_resources.at(10.25).allocated == 5);
  assert(result.settled_resources.at(10.75).allocated == 3);
  assert(result.settled_resources.at(11.5).allocated == 0);
  expect_close(result.stats.resource_area, 4.75);
  expect_close(result.stats.avg_wait_time, 0.125);
  expect_close(result.stats.avg_turnaround_time, 0.75);
}

void test_runtime_mode_applies_only_to_simulated_jobs() {
  const std::string trace =
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
      "0,2,20,1,0,18\n"   // warmup job: fixed historical departure
      "10,12,14,1,0,7\n"; // simulated job: observed runtime 2, limit 7

  const auto actual =
      run_trace(trace, 10.0, 2, PriorityPolicy::FCFS, BackfillPolicy::EASY,
                QueueImplementation::CIRCULAR, {}, RunTimeMode::ACTUAL);
  assert(actual.jobs.size() == 1);
  expect_job(actual.jobs[0], 10.0, 10.0, 12.0, 1);

  const auto limit =
      run_trace(trace, 10.0, 2, PriorityPolicy::FCFS, BackfillPolicy::EASY,
                QueueImplementation::CIRCULAR, {}, RunTimeMode::LIMIT);
  assert(limit.jobs.size() == 1);
  expect_job(limit.jobs[0], 10.0, 10.0, 17.0, 1);

  const auto distribution =
      run_trace(trace, 10.0, 2, PriorityPolicy::FCFS, BackfillPolicy::EASY,
                QueueImplementation::CIRCULAR, {}, RunTimeMode::DISTRIBUTION,
                DistributionType::NORMAL, 0.5, 0.0);
  assert(distribution.jobs.size() == 1);
  expect_job(distribution.jobs[0], 10.0, 10.0, 13.5, 1);

  // Runtime mode affects only the simulated job. The warmup job retains its
  // recorded end time in all runs and remains allocated through t=20.
  assert(actual.settled_resources.at(20.0).allocated == 0);
  assert(limit.settled_resources.at(20.0).allocated == 0);
  assert(distribution.settled_resources.at(20.0).allocated == 0);
}

std::vector<JobRow> without_seed_jobs(const std::vector<JobRow> &jobs,
                                      size_t seed_count) {
  assert(jobs.size() >= seed_count);
  return {jobs.begin() + static_cast<std::ptrdiff_t>(seed_count), jobs.end()};
}

void compare_equivalent(const std::string &warm_trace,
                        const std::string &reference_trace,
                        double sim_start_time, unsigned total_nodes,
                        size_t seed_count, PriorityPolicy priority,
                        BackfillPolicy backfill, QueueImplementation queue,
                        double backfill_interval = 0.0,
                        size_t backfill_queue_depth = 0) {
  const auto warm =
      run_trace(warm_trace, sim_start_time, total_nodes, priority, backfill,
                queue, {}, RunTimeMode::ACTUAL, DistributionType::NORMAL, 1.0,
                0.0, -1.0, backfill_interval, WarmStartWaitQueuePolicy::DROP, 0,
                backfill_queue_depth);
  const auto reference =
      run_trace(reference_trace, 0.0, total_nodes, priority, backfill, queue,
                {}, RunTimeMode::ACTUAL, DistributionType::NORMAL, 1.0, 0.0,
                -1.0, backfill_interval, WarmStartWaitQueuePolicy::DROP, 0,
                backfill_queue_depth);
  const auto reference_jobs = without_seed_jobs(reference.jobs, seed_count);
  assert(warm.jobs.size() == reference_jobs.size());
  for (size_t i = 0; i < warm.jobs.size(); ++i) {
    expect_close(warm.jobs[i].submit, reference_jobs[i].submit);
    expect_close(warm.jobs[i].begin, reference_jobs[i].begin);
    expect_close(warm.jobs[i].end, reference_jobs[i].end);
    assert(warm.jobs[i].nodes == reference_jobs[i].nodes);
  }

  const auto reference_after_t = [&]() {
    std::map<double, ResourceRow> rows;
    for (const auto &[time, row] : reference.settled_resources) {
      if (time + kTolerance >= sim_start_time) {
        rows[time] = row;
      }
    }
    return rows;
  }();
  assert(warm.settled_resources.size() == reference_after_t.size());
  for (const auto &[time, row] : warm.settled_resources) {
    const auto &reference_row = reference_after_t.at(time);
    assert(row.free == reference_row.free);
    assert(row.allocated == reference_row.allocated);
  }
  expect_close(warm.stats.resource_area, reference.stats.resource_area);
  const double reference_from_boundary =
      reference.stats.resource_area /
      (static_cast<double>(total_nodes) *
       (reference.stats.makespan - sim_start_time));
  expect_close(warm.stats.utilization, reference_from_boundary);
}

void test_every_policy_and_queue() {
  const std::string warm =
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
      "0,1,4,1,0,10\n"
      "1,2,15,4,0,10\n"
      "2,6,12,3,0,10\n"
      "3,11,14,2,0,10\n"
      "10,10,14,3,0,10\n"
      "10,11,13,4,0,10\n"
      "11,12,15,2,0,10\n"
      "12,13,14,5,0,10\n";
  const std::string reference =
      "job_submit_time,num_nodes,time_limit,actual_run_time\n"
      "10,4,10,5\n"
      "10,3,10,2\n"
      "10,3,10,4\n"
      "10,4,10,2\n"
      "11,2,10,3\n"
      "12,5,10,1\n";

  const std::vector<PriorityPolicy> priorities = {
      PriorityPolicy::FCFS, PriorityPolicy::FCFS_ALT,
      PriorityPolicy::FCFS_CONSERVATIVE, PriorityPolicy::SJF,
      PriorityPolicy::LJF};
  const std::vector<BackfillPolicy> backfills = {
      BackfillPolicy::NONE, BackfillPolicy::EASY, BackfillPolicy::CONSERVATIVE};
  const std::vector<QueueImplementation> queues = {
      QueueImplementation::CIRCULAR, QueueImplementation::DEQUE,
      QueueImplementation::MULTIMAP, QueueImplementation::BLOCK};

  for (const auto priority : priorities) {
    const auto supported_queue = priority == PriorityPolicy::FCFS_ALT
                                     ? QueueImplementation::MULTIMAP
                                     : QueueImplementation::CIRCULAR;
    for (const auto backfill : backfills) {
      compare_equivalent(warm, reference, 10.0, 10, 2, priority, backfill,
                         supported_queue);
    }
  }
  for (const auto queue : queues) {
    for (const auto backfill : backfills) {
      compare_equivalent(warm, reference, 10.0, 10, 2, PriorityPolicy::FCFS,
                         backfill, queue);
    }
  }
  for (const auto queue :
       {QueueImplementation::CIRCULAR, QueueImplementation::DEQUE}) {
    for (const auto backfill : backfills) {
      compare_equivalent(warm, reference, 10.0, 10, 2,
                         PriorityPolicy::FCFS_CONSERVATIVE, backfill, queue);
    }
  }
}

// --backfill_queue_depth after a warm start. A historical job holds 8 of 10
// nodes until t=20. At the boundary H (10 nodes) is blocked, A (4 nodes) does
// not fit, and B (2 nodes, 5 s) can backfill. A depth of 2 stops the scan at
// A, so B waits; unlimited, B starts at the boundary. Each warm run must
// match the cold run seeded with the boundary state.
void test_queue_depth_after_warm_start() {
  const std::string warm =
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
      "0,0,20,8,0,20\n"
      "10,20,25,10,0,5\n"
      "10,25,55,4,0,30\n"
      "10,10,15,2,0,5\n";
  const std::string reference =
      "job_submit_time,num_nodes,time_limit,actual_run_time\n"
      "10,8,10,10\n"
      "10,10,5,5\n"
      "10,4,30,30\n"
      "10,2,5,5\n";

  struct Case {
    PriorityPolicy priority;
    QueueImplementation queue;
  };
  const Case cases[] = {
      {PriorityPolicy::FCFS, QueueImplementation::CIRCULAR},
      {PriorityPolicy::FCFS, QueueImplementation::DEQUE},
      {PriorityPolicy::FCFS, QueueImplementation::BLOCK},
      {PriorityPolicy::FCFS_ALT, QueueImplementation::MULTIMAP},
      {PriorityPolicy::FCFS_CONSERVATIVE, QueueImplementation::CIRCULAR},
      {PriorityPolicy::FCFS_CONSERVATIVE, QueueImplementation::DEQUE},
  };
  // SJF and LJF are not listed: in the cold reference the seed that stands
  // for the running historical job competes in duration order and does not
  // start first, so the seeded cold run is not equivalent for them.
  for (const auto &c : cases) {
    for (const size_t depth : {size_t{0}, size_t{2}}) {
      compare_equivalent(warm, reference, 10.0, 10, 1, c.priority,
                         BackfillPolicy::EASY, c.queue, 0.0, depth);
    }
  }

  // The depth must change this workload, or the comparisons prove little.
  const auto run = [&](size_t depth) {
    return run_trace(warm, 10.0, 10, PriorityPolicy::FCFS,
                     BackfillPolicy::EASY, QueueImplementation::CIRCULAR, {},
                     RunTimeMode::ACTUAL, DistributionType::NORMAL, 1.0, 0.0,
                     -1.0, 0.0, WarmStartWaitQueuePolicy::DROP, 0, depth);
  };
  const auto unlimited = run(0);
  const auto limited = run(2);
  assert(unlimited.jobs.size() == 3 && limited.jobs.size() == 3);
  expect_close(unlimited.jobs[2].begin, 10.0);
  assert(limited.jobs[2].begin > 10.0);
}

// Periodic backfill under warm start. Ticks are anchored at the boundary
// (first tick at t + interval), so a candidate arriving between ticks waits
// for the next one while FCFS dispatch stays event-driven, and the
// warm-start run matches the cold reference whenever the reference's ticks
// (multiples of the interval from time 0) coincide with the boundary grid.
void test_periodic_backfill() {
  // History: A (90 of 100 nodes) runs 2..40. Post-boundary: a head H that
  // can never fit beside A, and a candidate C behind it, both at t=15; a
  // second candidate D at t=23. Recorded begin/end of H, C, and D are
  // placeholders that the counterfactual run replaces.
  const std::string warm =
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
      "2,2,40,90,0,50\n"
      "15,40,41,100,0,1\n"
      "15,15,16,10,0,1\n"
      "23,23,24,10,0,1\n";
  const auto result =
      run_trace(warm, 10.0, 100, PriorityPolicy::FCFS, BackfillPolicy::EASY,
                QueueImplementation::CIRCULAR, {}, RunTimeMode::ACTUAL,
                DistributionType::NORMAL, 1.0, 0.0, -1.0, 10.0);
  // The output holds the post-boundary jobs only; A's historical end at 40
  // is visible through H, which can start only once A's nodes are freed.
  assert(result.jobs.size() == 3);
  expect_job(result.jobs[0], 15.0, 40.0, 41.0, 100); // head waits for A
  expect_job(result.jobs[1], 15.0, 20.0, 21.0, 10);  // tick at 20, not 15
  expect_job(result.jobs[2], 23.0, 30.0, 31.0, 10);  // tick at 30, not 23
  expect_close(result.stats.makespan, 41.0);
  assert(result.stats.nodes_in_use == 0);

  // Equivalence with the cold reference: t=10 is on the 2 s and 5 s grids.
  const std::string equivalent_warm =
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
      "0,1,4,1,0,10\n"
      "1,2,15,4,0,10\n"
      "2,6,12,3,0,10\n"
      "3,11,14,2,0,10\n"
      "10,10,14,3,0,10\n"
      "10,11,13,4,0,10\n"
      "11,12,15,2,0,10\n"
      "12,13,14,5,0,10\n";
  const std::string reference =
      "job_submit_time,num_nodes,time_limit,actual_run_time\n"
      "10,4,10,5\n"
      "10,3,10,2\n"
      "10,3,10,4\n"
      "10,4,10,2\n"
      "11,2,10,3\n"
      "12,5,10,1\n";
  for (const auto backfill :
       {BackfillPolicy::EASY, BackfillPolicy::CONSERVATIVE}) {
    for (const double interval : {2.0, 5.0}) {
      compare_equivalent(equivalent_warm, reference, 10.0, 10, 2,
                         PriorityPolicy::FCFS, backfill,
                         QueueImplementation::CIRCULAR, interval);
    }
  }
}

void test_fixed_periodic_warm_start() {
  for (const double departure : {16.0, 20.0}) {
    std::ostringstream trace;
    trace << "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
          << "0,0," << departure << ",3,0,20\n"
          << "10,30,50,2,0,20\n";
    const auto result = run_trace(
        trace.str(), 10.0, 4, PriorityPolicy::FCFS, BackfillPolicy::HYBRID,
        QueueImplementation::CIRCULAR, {}, RunTimeMode::ACTUAL,
        DistributionType::NORMAL, 1.0, 0.0, -1.0, 0.0,
        WarmStartWaitQueuePolicy::DROP, 1, 0, 5.0, 2);
    assert(result.jobs.size() == 1);
    expect_job(result.jobs[0], 10.0, 20.0, 40.0, 2);
    assert(result.stats.jobs_completed == 1);
    assert(result.stats.nodes_in_use == 0);
  }
}

void test_fixed_periodic_arrival_warm_start() {
  const std::string trace =
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
      "0,0,16,3,0,20\n"
      "10,30,50,2,0,20\n";
  const auto result = run_trace(
      trace, 10, 4, PriorityPolicy::FCFS, BackfillPolicy::HYBRID,
      QueueImplementation::CIRCULAR, {}, RunTimeMode::ACTUAL,
      DistributionType::NORMAL, 1, 0, -1, 0,
      WarmStartWaitQueuePolicy::DROP, 1, 0, 5, 2, true);
  assert(result.jobs.size() == 1);
  // Ordinary seed completion at 16 waits for the next aligned tick.
  expect_job(result.jobs[0], 10, 20, 40, 2);

  const auto notified = run_trace(
      "job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit\n"
      "0,0,16,3,0,20\n"
      "17,30,50,2,0,20\n",
      10, 4, PriorityPolicy::FCFS, BackfillPolicy::HYBRID,
      QueueImplementation::CIRCULAR, {}, RunTimeMode::ACTUAL,
      DistributionType::NORMAL, 1, 0, -1, 0,
      WarmStartWaitQueuePolicy::DROP, 1, 0, 5, 2, true);
  assert(notified.jobs.size() == 1);
  expect_job(notified.jobs[0], 17, 17, 37, 2);
}

void test_seeded_random_differential() {
  constexpr double t = 20.5;
  for (unsigned seed : {7u, 29u, 101u, 4099u}) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<unsigned> nodes(1, 4);
    std::uniform_int_distribution<unsigned> duration(1, 7);
    std::uniform_int_distribution<unsigned> offset(0, 5);

    std::ostringstream warm;
    warm << "job_submit_time,begin_time,end_time,num_nodes,exit_status,"
            "time_limit\n";
    // Finished history and inherited waiters deliberately have no reference
    // counterpart. They should be observationally absent after the boundary.
    warm << "0,1,4," << nodes(rng) << ",0,3\n";

    std::ostringstream reference;
    reference << "job_submit_time,num_nodes,time_limit,actual_run_time\n";
    unsigned active_nodes = 0;
    constexpr size_t active_count = 3;
    for (size_t i = 0; i < active_count; ++i) {
      const unsigned n = nodes(rng);
      const unsigned remaining = 2 + offset(rng);
      const double begin = 10.0 + static_cast<double>(i);
      const double end = t + remaining;
      active_nodes += n;
      warm << i + 1 << ',' << begin << ',' << end << ',' << n << ",0," << 10
           << "\n";
      // Historical departures are known to the warm-start planner. Give
      // the cold seed the same projected release, not a new ten-second limit.
      reference << t << ',' << n << ',' << remaining << ',' << remaining
                << "\n";
    }
    warm << "4,21,24,1,0,3\n";
    warm << "5,20.5,22,1,0,2\n";

    constexpr size_t workload_count = 9;
    for (size_t i = 0; i < workload_count; ++i) {
      const double submit = t + 0.5 * offset(rng);
      const unsigned n = nodes(rng);
      const unsigned d = duration(rng);
      // begin/end are observed history only; warm start must retain duration
      // while replacing both with the counterfactual scheduler's result.
      warm << submit << ',' << submit + 3 << ',' << submit + 3 + d << ',' << n
           << ",0,10\n";
      reference << submit << ',' << n << ",10," << d << "\n";
    }
    const unsigned total_nodes = std::max(12u, active_nodes);
    compare_equivalent(warm.str(), reference.str(), t, total_nodes,
                       active_count, PriorityPolicy::FCFS, BackfillPolicy::EASY,
                       QueueImplementation::CIRCULAR);
  }
}

/**
 * A simulation's own output is sufficient to reconstruct the deterministic
 * scheduler-visible state at a boundary: active jobs become replay seeds and
 * jobs that have not yet started become inherited waiters. This is deliberately
 * a no-checkpoint test: it proves the replay warm-start path, not checkpoint
 * restoration. Random runtime/release models and a non-aligned periodic tick
 * are intentionally outside this equivalence contract.
 */
void test_self_consistent_replay_warm_start() {
  constexpr double boundary = 10.0;
  const std::string input =
      "job_submit_time,num_nodes,time_limit,actual_run_time\n"
      "0,4,30,30\n"  // seed still running at the boundary
      "1,4,6,6\n"   // inherited FCFS head
      "2,1,3,3\n"   // inherited candidate
      "3,2,5,5\n";  // inherited candidate
  const std::vector<unsigned> time_limits = {30, 6, 3, 5};

  const auto check = [&](PriorityPolicy priority, BackfillPolicy backfill,
                         size_t reservation_depth = 0) {
    const auto cold = run_trace(input, 0.0, 4, priority, backfill,
                                QueueImplementation::CIRCULAR, {},
                                RunTimeMode::ACTUAL, DistributionType::NORMAL,
                                1.0, 0.0, -1.0, 0.0,
                                WarmStartWaitQueuePolicy::INHERIT,
                                reservation_depth);
    assert(cold.jobs.size() == time_limits.size());

    std::ostringstream replay;
    replay << "job_submit_time,begin_time,end_time,num_nodes,exit_status,"
              "time_limit\n";
    for (size_t i = 0; i < cold.jobs.size(); ++i) {
      const auto &job = cold.jobs[i];
      replay << job.submit << ',' << job.begin << ',' << job.end << ','
             << job.nodes << ",0," << time_limits[i] << '\n';
    }

    const auto warm = run_trace(replay.str(), boundary, 4, priority, backfill,
                                QueueImplementation::CIRCULAR, {},
                                RunTimeMode::ACTUAL, DistributionType::NORMAL,
                                1.0, 0.0, -1.0, 0.0,
                                WarmStartWaitQueuePolicy::INHERIT,
                                reservation_depth);
    std::vector<JobRow> expected;
    for (const auto &job : cold.jobs) {
      if (job.begin + kTolerance >= boundary) {
        expected.push_back(job);
      }
    }
    assert(warm.jobs.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
      expect_job(warm.jobs[i], expected[i].submit, expected[i].begin,
                 expected[i].end, expected[i].nodes);
    }
  };

  check(PriorityPolicy::FCFS, BackfillPolicy::EASY);
  check(PriorityPolicy::FCFS_CONSERVATIVE, BackfillPolicy::CONSERVATIVE);
  check(PriorityPolicy::FCFS_CONSERVATIVE, BackfillPolicy::HYBRID, 3);
  check(PriorityPolicy::SJF, BackfillPolicy::EASY);
}

void test_rejected_modes() {
  const auto simulation_path = work_dir / "non_replay.csv";
  write_file(simulation_path, "job_submit_time,num_nodes,time_limit\n0,1,1\n");
  Sim_Params non_replay;
  non_replay.m_infile = simulation_path.string();
  non_replay.m_total_nodes = 2;
  non_replay.m_sim_start_time = 1.0;
  non_replay.m_trace_format = "simple";
  non_replay.m_timestamp_format = "epoch";
  bool rejected = false;
  try {
    Simulation simulation(non_replay);
    simulation.run();
  } catch (const std::runtime_error &e) {
    rejected = std::string(e.what()).find("replay-format") != std::string::npos;
  }
  assert(rejected);

  const auto replay_path = work_dir / "replay_for_list.csv";
  write_file(replay_path,
             "job_submit_time,begin_time,end_time,num_nodes,exit_status,"
             "time_limit\n0,0,1,1,0,1\n");
  Sim_Params progressive;
  progressive.m_infile = replay_path.string();
  progressive.m_infile_list = (work_dir / "traces.list").string();
  progressive.m_sim_start_time = 1.0;
  progressive.m_total_nodes = 2;
  progressive.m_trace_format = "simple";
  progressive.m_timestamp_format = "epoch";
  rejected = false;
  try {
    Simulation simulation(progressive);
    simulation.run();
  } catch (const std::runtime_error &e) {
    rejected = std::string(e.what()).find("infile_list") != std::string::npos;
  }
  assert(rejected);
}

} // namespace

int main() {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  work_dir = std::filesystem::temp_directory_path() /
             ("dr_evt_warm_start_" + std::to_string(suffix));
  std::filesystem::create_directory(work_dir);
  try {
    test_boundary_classification_and_statistics();
    test_max_time_is_an_inclusive_event_boundary();
    test_no_live_history_and_empty_tail();
    test_zero_start_preserves_full_replay();
    test_staggered_and_simultaneous_historical_departures();
    test_capacity_boundaries_and_overcommit();
    test_capacity_change_at_final_warm_departure();
    test_fractional_boundary();
    test_runtime_mode_applies_only_to_simulated_jobs();
    test_every_policy_and_queue();
  test_queue_depth_after_warm_start();
    test_periodic_backfill();
    test_seeded_random_differential();
    test_fixed_periodic_warm_start();
    test_fixed_periodic_arrival_warm_start();
    test_self_consistent_replay_warm_start();
    test_rejected_modes();
  } catch (...) {
    std::filesystem::remove_all(work_dir);
    throw;
  }
  std::filesystem::remove_all(work_dir);
  std::cout << "Warm-start tests passed\n";
  return EXIT_SUCCESS;
}
