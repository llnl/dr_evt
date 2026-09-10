/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/**
 * Test batch mode vs streaming mode equivalence
 *
 * Verifies that streaming API produces identical results to batch mode
 * by comparing job traces from both approaches.
 */

#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace dr_evt;

// Batch-mode parity exercises the protected scheduler helper directly to
// reproduce a preloaded trace's legacy submission path. Public streaming
// callers append and enqueue atomically instead.
class TestSimulation : public Simulation {
public:
  using Simulation::Simulation;
  using Simulation::submit_job;
};

struct JobResult {
  job_no_t job_idx;
  sim_time_t submit_time;
  sim_time_t start_time;
  sim_time_t end_time;
  num_nodes_t nodes;
};

struct ResourceState {
  sim_time_t time;
  num_nodes_t free_nodes;
  num_nodes_t allocated_nodes;
};

std::vector<JobResult> load_results(const std::string &filename) {
  std::vector<JobResult> results;
  std::ifstream ifs(filename);
  if (!ifs) {
    throw std::runtime_error("Failed to open: " + filename);
  }

  std::string line;
  std::getline(ifs, line); // Skip header

  while (std::getline(ifs, line)) {
    if (line.empty())
      continue;

    JobResult jr;
    // Parse CSV: job_submit_time,begin_time,end_time,num_nodes,exit_status,
    // queue-or-q_id,time_limit
    size_t pos = 0;
    size_t next_pos;

    // submit_time
    next_pos = line.find(',', pos);
    jr.submit_time = std::stod(line.substr(pos, next_pos - pos));
    pos = next_pos + 1;

    // begin_time
    next_pos = line.find(',', pos);
    jr.start_time = std::stod(line.substr(pos, next_pos - pos));
    pos = next_pos + 1;

    // end_time
    next_pos = line.find(',', pos);
    jr.end_time = std::stod(line.substr(pos, next_pos - pos));
    pos = next_pos + 1;

    // num_nodes
    next_pos = line.find(',', pos);
    jr.nodes = std::stoi(line.substr(pos, next_pos - pos));

    jr.job_idx = results.size(); // Implicit index
    results.push_back(jr);
  }

  return results;
}

std::vector<ResourceState> load_resource_trace(const std::string &filename) {
  std::vector<ResourceState> states;
  std::ifstream ifs(filename);
  if (!ifs) {
    throw std::runtime_error("Failed to open: " + filename);
  }

  std::string line;
  std::getline(ifs, line); // Skip header

  while (std::getline(ifs, line)) {
    if (line.empty())
      continue;

    ResourceState rs;
    // Parse CSV: time,free_nodes,allocated_nodes
    size_t pos = 0;
    size_t next_pos;

    // time
    next_pos = line.find(',', pos);
    rs.time = std::stod(line.substr(pos, next_pos - pos));
    pos = next_pos + 1;

    // free_nodes
    next_pos = line.find(',', pos);
    rs.free_nodes = std::stoi(line.substr(pos, next_pos - pos));
    pos = next_pos + 1;

    // allocated_nodes
    rs.allocated_nodes = std::stoi(line.substr(pos));

    states.push_back(rs);
  }

  return states;
}

bool compare_results(const std::vector<JobResult> &batch,
                     const std::vector<JobResult> &stream,
                     bool verbose = true) {
  if (batch.size() != stream.size()) {
    std::cout << "✗ Size mismatch: batch=" << batch.size()
              << " vs stream=" << stream.size() << std::endl;
    return false;
  }

  bool all_match = true;
  size_t mismatches = 0;
  const double TOLERANCE = 0.001; // 1ms tolerance for floating point

  for (size_t i = 0; i < batch.size(); i++) {
    bool match = true;

    if (std::abs(batch[i].start_time - stream[i].start_time) > TOLERANCE) {
      if (verbose && mismatches < 10) {
        std::cout << "  Job " << i
                  << ": start_time mismatch: " << batch[i].start_time << " vs "
                  << stream[i].start_time << std::endl;
      }
      match = false;
    }

    if (std::abs(batch[i].end_time - stream[i].end_time) > TOLERANCE) {
      if (verbose && mismatches < 10) {
        std::cout << "  Job " << i
                  << ": end_time mismatch: " << batch[i].end_time << " vs "
                  << stream[i].end_time << std::endl;
      }
      match = false;
    }

    if (batch[i].nodes != stream[i].nodes) {
      if (verbose && mismatches < 10) {
        std::cout << "  Job " << i << ": nodes mismatch: " << batch[i].nodes
                  << " vs " << stream[i].nodes << std::endl;
      }
      match = false;
    }

    if (!match) {
      mismatches++;
      all_match = false;
    }
  }

  if (verbose && mismatches > 0) {
    std::cout << "Total mismatches: " << mismatches << "/" << batch.size()
              << std::endl;
  }

  return all_match;
}

bool compare_resource_traces(const std::vector<ResourceState> &batch,
                             const std::vector<ResourceState> &stream,
                             bool verbose = true) {
  // Batch mode may have an initial (0, total_nodes, 0) entry that streaming
  // doesn't
  size_t batch_start = 0;
  size_t stream_start = 0;

  if (batch.size() == stream.size() + 1 && batch[0].time == 0 &&
      batch[0].allocated_nodes == 0) {
    batch_start = 1; // Skip initial state in batch
    if (verbose) {
      std::cout << "  (Skipping initial state entry in batch mode)"
                << std::endl;
    }
  } else if (batch.size() != stream.size()) {
    std::cout << "✗ Resource trace size mismatch: batch=" << batch.size()
              << " vs stream=" << stream.size() << std::endl;
    return false;
  }

  bool all_match = true;
  size_t mismatches = 0;
  const double TIME_TOLERANCE = 0.001;
  size_t entries_to_compare = batch.size() - batch_start;

  for (size_t i = 0; i < entries_to_compare; i++) {
    size_t batch_idx = batch_start + i;
    size_t stream_idx = stream_start + i;
    bool match = true;

    if (std::abs(batch[batch_idx].time - stream[stream_idx].time) >
        TIME_TOLERANCE) {
      if (verbose && mismatches < 10) {
        std::cout << "  Entry " << i
                  << ": time mismatch: " << batch[batch_idx].time << " vs "
                  << stream[stream_idx].time << std::endl;
      }
      match = false;
    }

    if (batch[batch_idx].free_nodes != stream[stream_idx].free_nodes) {
      if (verbose && mismatches < 10) {
        std::cout << "  Entry " << i << " (t=" << batch[batch_idx].time
                  << "): free_nodes mismatch: " << batch[batch_idx].free_nodes
                  << " vs " << stream[stream_idx].free_nodes << std::endl;
      }
      match = false;
    }

    if (batch[batch_idx].allocated_nodes !=
        stream[stream_idx].allocated_nodes) {
      if (verbose && mismatches < 10) {
        std::cout << "  Entry " << i << " (t=" << batch[batch_idx].time
                  << "): allocated_nodes mismatch: "
                  << batch[batch_idx].allocated_nodes << " vs "
                  << stream[stream_idx].allocated_nodes << std::endl;
      }
      match = false;
    }

    if (!match) {
      mismatches++;
      all_match = false;
    }
  }

  if (verbose && mismatches > 0) {
    std::cout << "Total resource trace mismatches: " << mismatches << "/"
              << entries_to_compare << std::endl;
  }

  return all_match;
}

void run_batch_mode(const std::string &input_file,
                    const std::string &output_file) {
  Sim_Params params;
  params.m_infile = input_file;
  params.m_total_nodes = 100;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_run_time_mode = RunTimeMode::LIMIT;
  params.m_backfill_policy = BackfillPolicy::EASY;
  params.m_priority_policy = PriorityPolicy::FCFS;
  params.set_outfile(output_file);

  Simulation sim(params);
  sim.run();
  sim.write_simulated_trace();

  // Write resource trace
  sim.write_resource_trace("/tmp/batch_resources.csv");
}

void run_streaming_mode(const std::string &input_file,
                        const std::string &output_file) {
  Sim_Params params;
  params.m_infile = input_file;
  params.m_total_nodes = 100;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_run_time_mode = RunTimeMode::LIMIT;
  params.m_backfill_policy = BackfillPolicy::EASY;
  params.m_priority_policy = PriorityPolicy::FCFS;
  params.set_outfile(output_file);

  TestSimulation sim(params);

  // Load trace
  const auto max_num_jobs = params.m_is_jobs_set ? params.m_max_jobs : 0u;
  [[maybe_unused]] int rc = sim.get_trace().load_data(max_num_jobs);
  assert(rc == EXIT_SUCCESS);

  size_t num_jobs = sim.get_trace().data().size();

  // Trace::load_data() sorts by submit_time, so data().back() is always
  // the last-arriving job - a rough estimate of the whole simulated
  // period's length.
  sim_time_t last_submit_time = 0.0;
  if (num_jobs > 0) {
    const auto &last_job = sim.get_trace().data().back();
    last_submit_time =
        static_cast<sim_time_t>(last_job.get_submit_time().first) +
        last_job.get_submit_time().second;
  }

  // Genuinely incremental streaming: divide the arrival period into 4
  // sub-periods; each iteration submits only the jobs arriving within
  // that sub-period, then advances to its end - jobs due later are
  // still queued to arrive on subsequent iterations, exactly like a
  // real streaming caller that doesn't know the whole future trace
  // upfront. A job doesn't need to finish within any one advance_to()
  // call.
  constexpr int NUM_PERIODS = 4;
  sim_time_t period = last_submit_time / NUM_PERIODS;
  size_t next_job_idx = 0;

  for (int k = 1; k <= NUM_PERIODS; ++k) {
    sim_time_t period_end = period * k;

    while (next_job_idx < num_jobs) {
      const auto &job = sim.get_trace().data()[next_job_idx];
      sim_time_t submit_time =
          static_cast<sim_time_t>(job.get_submit_time().first) +
          job.get_submit_time().second;
      if (submit_time > period_end) {
        break;
      }
      sim.submit_job(next_job_idx, submit_time);
      ++next_job_idx;
    }

    sim.advance_to(period_end);
  }

  // Submit anything left over (rounding at the last boundary), then
  // advance to infinity to fully drain whatever's still running or
  // queued - matching sim.run() (batch mode)'s own final call, and
  // guaranteeing a target time past every job's actual completion
  // without having to estimate one.
  while (next_job_idx < num_jobs) {
    const auto &job = sim.get_trace().data()[next_job_idx];
    sim_time_t submit_time =
        static_cast<sim_time_t>(job.get_submit_time().first) +
        job.get_submit_time().second;
    sim.submit_job(next_job_idx, submit_time);
    ++next_job_idx;
  }
  sim.advance_to(std::numeric_limits<sim_time_t>::max());

  // Write output
  sim.write_simulated_trace();

  // Write resource trace
  sim.write_resource_trace("/tmp/stream_resources.csv");
}

int main(int argc, char **argv) {
  std::string input_file = "tests/test_traces/scale/huge_2000jobs.csv";

  if (argc > 1) {
    input_file = argv[1];
  }

  std::cout << "==========================================" << std::endl;
  std::cout << "Batch vs Streaming Equivalence Test" << std::endl;
  std::cout << "==========================================" << std::endl;
  std::cout << "Input: " << input_file << std::endl;

  // Check file exists
  std::ifstream test_file(input_file);
  if (!test_file) {
    std::cerr << "Error: Input file not found: " << input_file << std::endl;
    return 1;
  }
  test_file.close();

  // Count jobs
  std::ifstream ifs(input_file);
  size_t job_count = 0;
  std::string line;
  std::getline(ifs, line); // Skip header
  while (std::getline(ifs, line)) {
    if (!line.empty())
      job_count++;
  }
  ifs.close();

  std::cout << "Jobs: " << job_count << std::endl;
  std::cout << std::endl;

  // Run batch mode
  std::cout << "Running BATCH mode..." << std::endl;
  auto start_batch = std::chrono::steady_clock::now();
  run_batch_mode(input_file, "/tmp/batch_output.csv");
  auto end_batch = std::chrono::steady_clock::now();
  auto batch_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_batch - start_batch)
                        .count();
  std::cout << "  ✓ Completed in " << batch_time << "ms" << std::endl;

  // Run streaming mode
  std::cout << "Running STREAMING mode..." << std::endl;
  auto start_stream = std::chrono::steady_clock::now();
  run_streaming_mode(input_file, "/tmp/stream_output.csv");
  auto end_stream = std::chrono::steady_clock::now();
  auto stream_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                         end_stream - start_stream)
                         .count();
  std::cout << "  ✓ Completed in " << stream_time << "ms" << std::endl;

  std::cout << std::endl;

  // Compare job traces
  std::cout << "Comparing job traces..." << std::endl;
  auto batch_results = load_results("/tmp/batch_output.csv");
  auto stream_results = load_results("/tmp/stream_output.csv");

  std::cout << "  Batch jobs: " << batch_results.size() << std::endl;
  std::cout << "  Stream jobs: " << stream_results.size() << std::endl;
  std::cout << std::endl;

  bool jobs_match = compare_results(batch_results, stream_results, true);

  std::cout << std::endl;

  // Compare resource traces
  std::cout << "Comparing resource traces..." << std::endl;
  auto batch_resources = load_resource_trace("/tmp/batch_resources.csv");
  auto stream_resources = load_resource_trace("/tmp/stream_resources.csv");

  std::cout << "  Batch resource entries: " << batch_resources.size()
            << std::endl;
  std::cout << "  Stream resource entries: " << stream_resources.size()
            << std::endl;
  std::cout << std::endl;

  bool resources_match =
      compare_resource_traces(batch_resources, stream_resources, true);

  bool match = jobs_match && resources_match;

  std::cout << std::endl;
  std::cout << "==========================================" << std::endl;
  if (match) {
    std::cout << "✓✓ PASS: Results are IDENTICAL!" << std::endl;
    std::cout << std::endl;
    std::cout << "Validated:" << std::endl;
    std::cout << "  ✓ Job traces match (scheduling decisions)" << std::endl;
    std::cout << "  ✓ Resource traces match (resource accounting)" << std::endl;
    std::cout << std::endl;
    std::cout << "Performance:" << std::endl;
    std::cout << "  Batch time: " << batch_time << "ms" << std::endl;
    std::cout << "  Stream time: " << stream_time << "ms" << std::endl;
    double overhead = 100.0 * (stream_time - batch_time) / batch_time;
    std::cout << "  Overhead: " << std::fixed << std::setprecision(1)
              << overhead << "%" << std::endl;
  } else {
    std::cout << "✗✗ FAIL: Results DIFFER!" << std::endl;
    if (!jobs_match) {
      std::cout << "  ✗ Job traces differ (scheduling mismatch)" << std::endl;
    }
    if (!resources_match) {
      std::cout << "  ✗ Resource traces differ (accounting error)" << std::endl;
    }
  }
  std::cout << "==========================================" << std::endl;

  return match ? 0 : 1;
}
