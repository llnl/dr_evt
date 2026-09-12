/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/**
 * Test progressive/multi-file loading (--infile_list): a sequence of
 * separate, pre-sorted trace files loaded one at a time as the
 * simulation reaches each, instead of one big file loaded whole - so
 * --job_store_capacity can actually bound memory (single-file mode
 * always grows to fit the whole trace regardless of this setting; see
 * docs/dev/OUTPUT_TRACE_BUFFERS.md).
 *
 * Covers Trace::load_next_file() (the per-file loader) directly, and
 * Simulation::run_progressive() (the driving loop: load a file, submit
 * its jobs one at a time, advance_to() its last submit_time, repeat)
 * via the public Simulation::run() entry point, same as a real
 * --infile_list CLI invocation would use. Also covers the
 * --check_memory_pressure option (Trace::check_memory_pressure()),
 * which ensure_batch_capacity() - shared by load_next_file() and
 * append_jobs() - runs before growing the job store for a new batch.
 */

#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace dr_evt;

namespace {

const char *PART1 = "tests/test_traces/progressive/part1.csv";
const char *PART2 = "tests/test_traces/progressive/part2.csv";
const char *PART3 = "tests/test_traces/progressive/part3.csv";
const char *COMBINED = "tests/test_traces/progressive/combined.csv";

Sim_Params make_progressive_params(const std::vector<std::string> &files) {
  Sim_Params params;
  params.m_infile_list_parsed = files;
  params.m_infile_list = "unused-marker"; // only checked for emptiness by run()
  params.m_infile = files.front(); // what Trace's own constructor validates
  params.m_total_nodes = 100;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_run_time_mode = RunTimeMode::LIMIT;
  return params;
}

[[maybe_unused]] std::string slurp(const std::string &path) {
  std::ifstream ifs(path);
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

} // anonymous namespace

// Test 1: progressive loading across 3 files must produce exactly the
// same schedule as the same 6 jobs in one combined file - splitting the
// input shouldn't change the answer.
void test_progressive_matches_single_file() {
  std::cout << "\n=== Test 1: progressive matches single-file schedule ==="
            << std::endl;

  std::string progressive_stats;
  std::string combined_stats;
  {
    auto params = make_progressive_params({PART1, PART2, PART3});
    params.set_outfile("/tmp/test_progressive_out.csv");
    Simulation sim(params);
    sim.run();
    sim.write_simulated_trace();
    assert(sim.get_trace().completed_count() == 6);
    std::ostringstream stats;
    sim.print_stats(stats);
    progressive_stats = stats.str();
  }
  {
    Sim_Params params;
    params.m_infile = COMBINED;
    params.m_total_nodes = 100;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_run_time_mode = RunTimeMode::LIMIT;
    params.set_outfile("/tmp/test_progressive_combined_out.csv");
    Simulation sim(params);
    sim.run();
    sim.write_simulated_trace();
    assert(sim.get_trace().completed_count() == 6);
    std::ostringstream stats;
    sim.print_stats(stats);
    combined_stats = stats.str();
  }

  assert(slurp("/tmp/test_progressive_out.csv") ==
         slurp("/tmp/test_progressive_combined_out.csv"));
  if (progressive_stats != combined_stats) {
    std::cerr << "Progressive and single-file statistics differ:\n"
              << "--- progressive ---\n"
              << progressive_stats << "--- single file ---\n"
              << combined_stats;
    throw std::runtime_error(
        "input partitioning changed simulation statistics");
  }

  // Repeat the statistics comparison with a queued workload. The ordinary
  // progressive fixture never has a waiting job, so an extra zero-valued
  // sample at each file boundary would otherwise go undetected.
  constexpr const char *queue_part1 =
      "/tmp/test_progressive_queue_stats_part1.csv";
  constexpr const char *queue_part2 =
      "/tmp/test_progressive_queue_stats_part2.csv";
  constexpr const char *queue_combined =
      "/tmp/test_progressive_queue_stats_combined.csv";
  {
    std::ofstream out(queue_part1);
    out << "job_submit_time,num_nodes,time_limit\n"
        << "0,100,10\n"
        << "1,100,10\n";
  }
  {
    std::ofstream out(queue_part2);
    out << "job_submit_time,num_nodes,time_limit\n" << "2,100,10\n";
  }
  {
    std::ofstream out(queue_combined);
    out << "job_submit_time,num_nodes,time_limit\n"
        << "0,100,10\n"
        << "1,100,10\n"
        << "2,100,10\n";
  }

  std::string queued_progressive_stats;
  std::string queued_combined_stats;
  {
    auto params = make_progressive_params({queue_part1, queue_part2});
    params.set_outfile("/tmp/test_progressive_queue_stats_out.csv");
    Simulation sim(params);
    sim.run();
    std::ostringstream stats;
    sim.print_stats(stats);
    queued_progressive_stats = stats.str();
  }
  {
    Sim_Params params;
    params.m_infile = queue_combined;
    params.m_total_nodes = 100;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_run_time_mode = RunTimeMode::LIMIT;
    params.set_outfile("/tmp/test_combined_queue_stats_out.csv");
    Simulation sim(params);
    sim.run();
    std::ostringstream stats;
    sim.print_stats(stats);
    queued_combined_stats = stats.str();
  }
  if (queued_progressive_stats != queued_combined_stats) {
    std::cerr << "Queued progressive and single-file statistics differ:\n"
              << "--- progressive ---\n"
              << queued_progressive_stats << "--- single file ---\n"
              << queued_combined_stats;
    throw std::runtime_error(
        "input partitioning changed queued simulation statistics");
  }
  if (queued_progressive_stats.find("Average queue length: 0.333333 jobs") ==
          std::string::npos ||
      queued_progressive_stats.find("Peak queue length: 2 jobs") ==
          std::string::npos) {
    std::cerr << "Unexpected per-arrival queue statistics:\n"
              << queued_progressive_stats;
    throw std::runtime_error("queue statistics use the wrong sampling rule");
  }
  std::remove(queue_part1);
  std::remove(queue_part2);
  std::remove(queue_combined);

  std::cout << "  PASSED" << std::endl;
}

// Test 2: the actual point of this feature - with a small
// --job_store_capacity and job durations short enough that earlier
// jobs finish before later files load, progressive mode's peak
// capacity must stay well below what single-file mode reaches for the
// same jobs (single-file mode always grows to fit the whole trace, 8
// for 6 jobs - confirmed here as the honest baseline, not assumed).
void test_progressive_bounds_memory() {
  std::cout
      << "\n=== Test 2: progressive mode actually bounds job-store memory ==="
      << std::endl;

  {
    auto params = make_progressive_params({PART1, PART2, PART3});
    params.set_outfile("/tmp/test_progressive_bounded_out.csv");
    params.m_job_store_capacity = 2;
    params.m_job_store_overflow = CircularOverflowPolicy::GROW;

    Simulation sim(params);
    sim.run();
    sim.write_simulated_trace();

    assert(sim.get_trace().completed_count() == 6);
    assert(sim.get_trace().num_reclaimed() > 0);
    assert(sim.get_trace().data().capacity() <= 4);
  }
  {
    Sim_Params params;
    params.m_infile = COMBINED;
    params.m_total_nodes = 100;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_run_time_mode = RunTimeMode::LIMIT;
    params.set_outfile("/tmp/test_progressive_singlefile_bounded_out.csv");
    params.m_job_store_capacity = 2;
    params.m_job_store_overflow = CircularOverflowPolicy::GROW;

    Simulation sim(params);
    sim.run();
    sim.write_simulated_trace();

    assert(sim.get_trace().completed_count() == 6);
    assert(sim.get_trace().num_reclaimed() ==
           0); // never reclaims - everything submitted upfront
    assert(sim.get_trace().data().capacity() == 8);
  }

  std::cout << "  PASSED" << std::endl;
}

// Test 3: --job_store_overflow=abort must throw cleanly (not crash)
// when a file's jobs can't fit even after reclaiming everything
// currently reclaimable - here, forced by requesting a capacity too
// small for the very first file while nothing has run yet.
void test_progressive_abort_overflow() {
  std::cout << "\n=== Test 3: progressive mode's abort overflow policy ==="
            << std::endl;

  auto params = make_progressive_params({PART1, PART2});
  params.m_job_store_capacity = 1; // part1.csv alone has 2 jobs
  params.m_job_store_overflow = CircularOverflowPolicy::ABORT;

  Simulation sim(params);
  [[maybe_unused]] bool threw = false;
  try {
    sim.run();
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  std::cout << "  PASSED" << std::endl;
}

// Test 4: an empty file (header only, zero jobs) in the middle of the
// list must be skipped gracefully, not treated as an error - the
// remaining files' jobs still get processed correctly.
void test_progressive_empty_file_in_list() {
  std::cout
      << "\n=== Test 4: an empty file in the list is skipped gracefully ==="
      << std::endl;

  std::ofstream empty("/tmp/test_progressive_empty.csv");
  empty << "job_submit_time,num_nodes,time_limit\n";
  empty.close();

  auto params = make_progressive_params(
      {PART1, "/tmp/test_progressive_empty.csv", PART2});
  params.set_outfile("/tmp/test_progressive_empty_out.csv");

  Simulation sim(params);
  sim.run();
  sim.write_simulated_trace();

  assert(sim.get_trace().completed_count() == 4); // part1 (2) + part2 (2)

  std::cout << "  PASSED" << std::endl;
}

// Test 5: cross-file continuity violation (a later file's earliest
// submit_time before the previous file's latest) must throw, via the
// full Simulation::run() path - not just Trace::load_next_file()
// directly (already covered when this was designed/verified, but not
// as a permanent test until now).
void test_progressive_rejects_out_of_order_files() {
  std::cout << "\n=== Test 5: progressive mode rejects out-of-order files ==="
            << std::endl;

  // part2.csv's earliest submit_time (10) is before part3.csv's -
  // reversing the order here makes part3 (earliest=30) load first,
  // then part2 (earliest=10) violates continuity against it.
  auto params = make_progressive_params({PART3, PART2});

  Simulation sim(params);
  [[maybe_unused]] bool threw = false;
  try {
    sim.run();
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  std::cout << "  PASSED" << std::endl;
}

// Test 6: REPLAY-format input (begin_time/end_time already present)
// must be rejected outright for --infile_list - there's no scheduling
// decision for progressive loading to plug into for replay at all.
void test_progressive_rejects_replay_format() {
  std::cout << "\n=== Test 6: progressive mode rejects REPLAY-format input ==="
            << std::endl;

  std::ofstream replay_file("/tmp/test_progressive_replay.csv");
  replay_file << "job_submit_time,begin_time,end_time,num_nodes,time_limit\n";
  replay_file << "0,0,3,10,3\n";
  replay_file.close();

  Sim_Params params;
  params.m_infile_list_parsed = {"/tmp/test_progressive_replay.csv"};
  params.m_infile_list = "unused-marker";
  params.m_infile = params.m_infile_list_parsed.front();
  params.m_total_nodes = 100;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";

  Simulation sim(params);
  [[maybe_unused]] bool threw = false;
  try {
    sim.run();
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  std::cout << "  PASSED" << std::endl;
}

// Test 7: exit_status belongs to the generated-output schema.  Replay input
// accepts it for round-tripping a simulator output, but it must not affect the
// replayed job or the newly generated output; input without it is equivalent.
void test_input_exit_status_is_ignored() {
  std::cout << "\n=== Test 7: input exit_status is ignored ===" << std::endl;

  const char *with_status = "/tmp/test_exit_status_present.csv";
  const char *without_status = "/tmp/test_exit_status_absent.csv";
#if DR_EVT_LEGACY_QUEUE_INPUT
  const char *queue_column = "queue";
  const char *queue_value = "pbatch";
  const char *expected_row = "0,0,10,10,0,pbatch,10\n";
#else
  const char *queue_column = "q_id";
  const char *queue_value = "1";
  const char *expected_row = "0,0,10,10,0,1,10\n";
#endif
  {
    std::ofstream file(with_status);
    file << "job_submit_time,begin_time,end_time,num_nodes,exit_status,"
         << queue_column << ",time_limit\n";
    file << "0,0,10,10,73," << queue_value << ",10\n";
  }
  {
    std::ofstream file(without_status);
    file << "job_submit_time,begin_time,end_time,num_nodes," << queue_column
         << ",time_limit\n";
    file << "0,0,10,10," << queue_value << ",10\n";
  }

  auto replay = [](const char *input, const char *output) {
    Sim_Params params;
    params.m_infile = input;
    params.m_total_nodes = 100;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.set_outfile(output);
    Simulation sim(params);
    sim.run();
    sim.write_simulated_trace();
  };

  replay(with_status, "/tmp/test_exit_status_present_out.csv");
  replay(without_status, "/tmp/test_exit_status_absent_out.csv");
  assert(slurp("/tmp/test_exit_status_present_out.csv") ==
         slurp("/tmp/test_exit_status_absent_out.csv"));
  assert(slurp("/tmp/test_exit_status_present_out.csv").find(expected_row) !=
         std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

// Test 9: --check_memory_pressure refuses to load the next file when
// doing so would push projected job-store usage past the configured
// fraction of actual available memory - forced deterministically here
// via the DR_EVT_TEST_AVAILABLE_MEMORY_BYTES test seam (see
// get_available_memory_bytes()'s doc comment in system_memory.hpp),
// not by relying on the test machine actually being low on memory.
void test_memory_pressure_refuses_when_forced_low() {
  std::cout << "\n=== Test 8: --check_memory_pressure refuses under forced low "
               "memory ==="
            << std::endl;

  setenv("DR_EVT_TEST_AVAILABLE_MEMORY_BYTES", "10", 1);

  auto params = make_progressive_params({PART1, PART2});
  params.m_memory_pressure_fraction = 0.8;
  Simulation sim(params);

  [[maybe_unused]] bool threw = false;
  try {
    sim.run();
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  unsetenv("DR_EVT_TEST_AVAILABLE_MEMORY_BYTES");
  std::cout << "  PASSED" << std::endl;
}

// Test 10: the same forced-low-memory condition as Test 9 must NOT
// affect a run that never enables --check_memory_pressure - the check
// is opt-in, not a background limit.
void test_memory_pressure_disabled_by_default() {
  std::cout << "\n=== Test 9: --check_memory_pressure is off unless explicitly "
               "enabled ==="
            << std::endl;

  setenv("DR_EVT_TEST_AVAILABLE_MEMORY_BYTES", "10", 1);

  auto params = make_progressive_params({PART1, PART2, PART3});
  params.set_outfile("/tmp/test_progressive_mempress_disabled_out.csv");
  Simulation sim(params);
  // Deliberately not setting params.m_memory_pressure_fraction here.
  sim.run();
  sim.write_simulated_trace();

  assert(sim.get_trace().completed_count() == 6);

  unsetenv("DR_EVT_TEST_AVAILABLE_MEMORY_BYTES");
  std::cout << "  PASSED" << std::endl;
}

// Test 11: with --check_memory_pressure enabled but no artificially low
// memory forced (the real, actual available memory on the test
// machine), a normal progressive-loading run must still succeed - not
// a false positive against real, plentiful memory.
void test_memory_pressure_no_false_positive() {
  std::cout << "\n=== Test 10: --check_memory_pressure doesn't false-positive "
               "under real memory ==="
            << std::endl;

  auto params = make_progressive_params({PART1, PART2, PART3});
  params.set_outfile("/tmp/test_progressive_mempress_no_fp_out.csv");
  params.m_memory_pressure_fraction = 0.8;
  Simulation sim(params);
  sim.run();
  sim.write_simulated_trace();

  assert(sim.get_trace().completed_count() == 6);

  std::cout << "  PASSED" << std::endl;
}

// Test 12: the fraction itself must actually be what's compared
// against, not a fixed threshold - under the exact same forced
// available-memory condition, a tight fraction must refuse while a
// loose fraction succeeds. 512000 bytes / sizeof(Job_Record) (80)
// straddles the job store's own default 4096-record capacity floor
// (resolve_job_store_capacity() floors there regardless of how few
// jobs are actually loaded): 0.5 * 512000 / 80 = 3200 (< 4096, must
// refuse), 0.9 * 512000 / 80 = 5760 (> 4096, must succeed).
void test_memory_pressure_fraction_is_configurable() {
  std::cout
      << "\n=== Test 11: --check_memory_pressure's fraction is configurable ==="
      << std::endl;

  setenv("DR_EVT_TEST_AVAILABLE_MEMORY_BYTES", "512000", 1);

  {
    auto params = make_progressive_params({PART1, PART2, PART3});
    params.m_memory_pressure_fraction = 0.5;
    Simulation sim(params);
    [[maybe_unused]] bool threw = false;
    try {
      sim.run();
    } catch (const std::runtime_error &) {
      threw = true;
    }
    assert(threw);
  }
  {
    auto params = make_progressive_params({PART1, PART2, PART3});
    params.set_outfile("/tmp/test_progressive_mempress_configurable_out.csv");
    params.m_memory_pressure_fraction = 0.9;
    Simulation sim(params);
    sim.run();
    sim.write_simulated_trace();
    assert(sim.get_trace().completed_count() == 6);
  }

  unsetenv("DR_EVT_TEST_AVAILABLE_MEMORY_BYTES");
  std::cout << "  PASSED" << std::endl;
}

int main() {
  std::cout << "====================================" << std::endl;
  std::cout << "Progressive Loading (--infile_list) Test Suite" << std::endl;
  std::cout << "====================================" << std::endl;

  try {
    test_progressive_matches_single_file();
    test_progressive_bounds_memory();
    test_progressive_abort_overflow();
    test_progressive_empty_file_in_list();
    test_progressive_rejects_out_of_order_files();
    test_progressive_rejects_replay_format();
    test_input_exit_status_is_ignored();
    test_memory_pressure_refuses_when_forced_low();
    test_memory_pressure_disabled_by_default();
    test_memory_pressure_no_false_positive();
    test_memory_pressure_fraction_is_configurable();

    std::cout << "\n====================================" << std::endl;
    std::cout << "ALL PROGRESSIVE LOADING TESTS PASSED" << std::endl;
    std::cout << "====================================" << std::endl;
    return EXIT_SUCCESS;
  } catch (const std::exception &e) {
    std::cerr << "\nTEST FAILED: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
}
