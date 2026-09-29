/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include "sim/sim.hpp"
#include "trace/job_io.hpp"
#include "trace/trace.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

constexpr const char *input_path = "/tmp/dr_evt_pcon_input.csv";
constexpr const char *output_path = "/tmp/dr_evt_pcon_resources.csv";
constexpr const char *simulation_input_path =
    "/tmp/dr_evt_pcon_simulation_input.csv";
constexpr const char *simulation_output_path =
    "/tmp/dr_evt_pcon_simulation_output.csv";
constexpr const char *warm_input_path = "/tmp/dr_evt_pcon_warm_input.csv";
constexpr const char *warm_resource_path =
    "/tmp/dr_evt_pcon_warm_resources.csv";
constexpr const char *warm_job_path = "/tmp/dr_evt_pcon_warm_jobs.csv";
constexpr const char *filtered_input_path = "/tmp/dr_evt_pcon_filtered.csv";
constexpr const char *filtered_output_path =
    "/tmp/dr_evt_pcon_filtered_resources.csv";
constexpr const char *alignment_input_path =
    "/tmp/dr_evt_pcon_alignment_input.csv";
constexpr const char *queue_free_input_path =
    "/tmp/dr_evt_pcon_queue_free_input.csv";
constexpr const char *missing_pcon_input_path =
    "/tmp/dr_evt_pcon_missing_column.csv";
constexpr const char *admission_input_path =
    "/tmp/dr_evt_pcon_admission_input.csv";

bool expect_line(std::istream &input, const std::string &expected) {
  std::string actual;
  if (!std::getline(input, actual) || actual != expected) {
    std::cerr << "expected: " << expected << "\nactual:   " << actual << '\n';
    return false;
  }
  return true;
}

std::vector<dr_evt::Pcon_Job_Record>
load_pcon_records(const std::string &path, dr_evt::num_jobs_t max_count = 0) {
  dr_evt::Data_Columns columns("simple", "epoch", "+00:00");
  if (!columns.check_header(path)) {
    throw std::runtime_error("failed to validate Pcon test input: " + path);
  }
  std::vector<dr_evt::Pcon_Job_Record> records;
  if (dr_evt::Pcon_Trace_Policy::load_records(path, columns, records,
                                               max_count) != EXIT_SUCCESS) {
    throw std::runtime_error("failed to load Pcon test input: " + path);
  }
  return records;
}

bool expect_pcon(const dr_evt::Pcon_Job_Record &record, double avg,
                 double min, double max) {
  const auto &actual = record.pcon();
  if (actual.avgpcon != avg || actual.minpcon != min ||
      actual.maxpcon != max) {
    std::cerr << "expected Pcon " << avg << ',' << min << ',' << max
              << "; actual " << actual.avgpcon << ',' << actual.minpcon
              << ',' << actual.maxpcon << '\n';
    return false;
  }
  return true;
}

} // namespace

int main() {
  // One scalar timestamp plus three node counts (allocated, capacity,
  // releasing), padded to the timestamp's alignment: 24 bytes. Guards against
  // the sample growing, since the resource history buffers many of them.
  static_assert(sizeof(dr_evt::Standard_Resource_Sample) ==
                3 * sizeof(dr_evt::sim_time_t));
  static_assert(std::is_same_v<dr_evt::Trace::trace_data_t,
                               boost::circular_buffer<dr_evt::Job_Record>>);
  static_assert(sizeof(dr_evt::Trace::trace_data_t::value_type) ==
                sizeof(dr_evt::Job_Record));

  {
    std::ofstream input(input_path);
    input << "job_submit_time,begin_time,end_time,num_nodes,exit_status,q_id,"
             "time_limit,avgpcon,minpcon,maxpcon\n"
          << "0,1,4,2,0,1,3,1.5,2,3\n"
          << "0,2,3,1,0,1,1,0.5,1,1.5\n";
  }

  bool passed = true;
  try {
    dr_evt::PconTrace trace(input_path, "simple", "epoch", "+00:00");
    passed = trace.load_data() == EXIT_SUCCESS;
    if (trace.scheduler_power(0) != 1.5 ||
        trace.scheduler_maximum_power(0) != 3.0) {
      std::cerr << "Pcon scheduler power metadata regression\n";
      passed = false;
    }
    trace.run_job_trace(output_path, 4);

    std::ifstream output(output_path);
    passed &= expect_line(
        output, "time,free_nodes,allocated_nodes,avgpcon,minpcon,maxpcon");
    passed &= expect_line(output, "0,4,0,0.000000,0.000000,0.000000");
    passed &= expect_line(output, "1,2,2,1.500000,2.000000,3.000000");
    passed &= expect_line(output, "2,1,3,2.000000,3.000000,4.500000");
    passed &= expect_line(output, "3,2,2,1.500000,2.000000,3.000000");
    passed &= expect_line(output, "4,4,0,0.000000,0.000000,0.000000");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    passed = false;
  }

  // Admission limits drop complete records before job IDs are assigned.
  // Values exactly at either limit remain admissible, and later Pcon values
  // must remain aligned after both kinds of rejection.
  {
    std::ofstream input(admission_input_path);
#if DR_EVT_LEGACY_QUEUE_INPUT
    input << "job_submit_time,num_nodes,queue,time_limit,avgpcon,minpcon,"
             "maxpcon\n"
          << "0,4,pbatch,1,11,10,12\n"
          << "1,5,pbatch,1,2,1,3\n"
          << "2,1,pbatch,1,12.1,12,13\n"
          << "3,2,pbatch,1,3,2,4\n";
#else
    input << "job_submit_time,num_nodes,q_id,time_limit,avgpcon,minpcon,"
             "maxpcon\n"
          << "0,4,1,1,11,10,12\n"
          << "1,5,1,1,2,1,3\n"
          << "2,1,1,1,12.1,12,13\n"
          << "3,2,1,1,3,2,4\n";
#endif
  }
  try {
    dr_evt::Data_Columns columns("simple", "epoch", "+00:00");
    if (!columns.check_header(admission_input_path)) {
      throw std::runtime_error("failed to validate admission test input");
    }
    dr_evt::Trace_Admission_Limits limits;
    limits.maximum_nodes = 4;
    limits.maximum_job_power = 12.0;
    std::vector<dr_evt::Pcon_Job_Record> records;
    std::ostringstream diagnostics;
    auto *original_stderr = std::cerr.rdbuf(diagnostics.rdbuf());
    try {
      dr_evt::Pcon_Trace_Policy::load_records(admission_input_path, columns,
                                               records, 0, limits);
    } catch (...) {
      std::cerr.rdbuf(original_stderr);
      throw;
    }
    std::cerr.rdbuf(original_stderr);
    const std::string messages = diagnostics.str();
    if (records.size() != 2u || records[0].get_submit_time().first != 0 ||
        records[1].get_submit_time().first != 3 ||
        !expect_pcon(records[0], 11, 10, 12) ||
        !expect_pcon(records[1], 3, 2, 4) ||
        messages.find("Dropped trace row 2") == std::string::npos ||
        messages.find("maximum allowed nodes") == std::string::npos ||
        messages.find("Dropped trace row 3") == std::string::npos ||
        messages.find("maximum allowed power") == std::string::npos) {
      std::cerr << "trace admission filtering/diagnostics regression\n"
                << messages;
      passed = false;
    }

    dr_evt::Trace_Admission_Limits average_limits;
    average_limits.maximum_nodes = 4;
    average_limits.maximum_average_job_power = 10.0;
    std::vector<dr_evt::Pcon_Job_Record> average_records;
    dr_evt::Pcon_Trace_Policy::load_records(
        admission_input_path, columns, average_records, 0, average_limits);
    if (average_records.size() != 1u ||
        average_records[0].get_submit_time().first != 3 ||
        !expect_pcon(average_records[0], 3, 2, 4)) {
      std::cerr << "average-power admission filtering regression\n";
      passed = false;
    }

    dr_evt::PconTrace progressive(admission_input_path, "simple", "epoch",
                                  "+00:00");
    progressive.set_admission_limits(4, 12.0, false);
    const auto ids = progressive.load_next_file(0.0, admission_input_path);
    if (ids != std::vector<dr_evt::job_no_t>{0, 1} ||
        progressive.data().size() != 2u ||
        progressive.data()[0].pcon().maxpcon != 12.0 ||
        progressive.data()[1].pcon().avgpcon != 3.0) {
      std::cerr << "progressive trace admission filtering regression\n";
      passed = false;
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    passed = false;
  }

  // Rejected source rows must neither shift Pcon values onto adjacent jobs
  // nor stop max_count from retaining its source-row meaning.
  {
    std::ofstream input(alignment_input_path);
#if DR_EVT_LEGACY_QUEUE_INPUT
    input << "job_submit_time,num_nodes,queue,time_limit,duration,avgpcon,"
             "minpcon,maxpcon\n"
          << "0,1,pbatch,5,5,1,2,3\n"
          << "1,1,pbatch,5,6,901,902,903\n"
          << "2,1,pbatch,5,4,4,5,6\n"
          << "3,1,pbatch,5,7,904,905,906\n"
          << "4,1,pbatch,5,3,7,8,9\n";
#else
    input << "job_submit_time,num_nodes,q_id,time_limit,duration,avgpcon,"
             "minpcon,maxpcon\n"
          << "0,1,1,5,5,1,2,3\n"
          << "1,1,1,5,6,901,902,903\n"
          << "2,1,2,5,4,4,5,6\n"
          << "3,1,1,5,7,904,905,906\n"
          << "4,1,1,5,3,7,8,9\n";
#endif
  }

  try {
    const auto limited = load_pcon_records(alignment_input_path, 4);
    if (limited.size() != 2u || !expect_pcon(limited[0], 1, 2, 3) ||
        !expect_pcon(limited[1], 4, 5, 6)) {
      std::cerr << "Pcon max-count/alignment regression\n";
      passed = false;
    }

    const auto all = load_pcon_records(alignment_input_path);
    if (all.size() != 3u || !expect_pcon(all[0], 1, 2, 3) ||
        !expect_pcon(all[1], 4, 5, 6) ||
        !expect_pcon(all[2], 7, 8, 9)) {
      std::cerr << "Pcon multiple-rejection alignment regression\n";
      passed = false;
    }

    dr_evt::Data_Columns columns("simple", "epoch", "+00:00");
    if (!columns.check_header(alignment_input_path)) {
      throw std::runtime_error("failed to validate standard test input");
    }
    std::vector<dr_evt::Job_Record> standard;
    if (dr_evt::load(alignment_input_path, columns, standard) != EXIT_SUCCESS ||
        standard.size() != 3u || standard[0].get_submit_time().first != 0 ||
        standard[1].get_submit_time().first != 2 ||
        standard[2].get_submit_time().first != 4) {
      std::cerr << "standard trace loading changed during Pcon refactor\n";
      passed = false;
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    passed = false;
  }

  // Exercise the separate queue-free parsing path with the same immediate
  // Pcon attachment behavior.
  {
    std::ofstream input(queue_free_input_path);
    input << "job_submit_time,num_nodes,time_limit,duration,avgpcon,minpcon,"
             "maxpcon\n"
          << "0,1,5,6,999,999,999\n"
          << "1,1,5,4,10,11,12\n";
  }
  try {
    const auto records = load_pcon_records(queue_free_input_path);
    if (records.size() != 1u || !expect_pcon(records[0], 10, 11, 12)) {
      std::cerr << "queue-free Pcon alignment regression\n";
      passed = false;
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    passed = false;
  }

  // The Pcon-specific reader must reject a header that is valid for an
  // ordinary trace but omits one of the experimental columns.
  {
    std::ofstream input(missing_pcon_input_path);
    input << "job_submit_time,num_nodes,time_limit,avgpcon,minpcon\n"
          << "0,1,5,1,2\n";
  }
  try {
    (void)load_pcon_records(missing_pcon_input_path);
    std::cerr << "Pcon loader accepted a missing maxpcon column\n";
    passed = false;
  } catch (const std::invalid_argument &error) {
    if (std::string(error.what()).find("maxpcon") == std::string::npos) {
      std::cerr << "unexpected missing-column diagnostic: " << error.what()
                << '\n';
      passed = false;
    }
  } catch (const std::exception &error) {
    std::cerr << "unexpected missing-column exception: " << error.what()
              << '\n';
    passed = false;
  }

  {
    std::ofstream input(filtered_input_path);
    input << "job_submit_time,num_nodes,q_id,time_limit,duration,avgpcon,"
             "minpcon,maxpcon\n"
          << "0,1,1,5,6,999,999,999\n"
          << "0,1,1,5,5,2,3,4\n";
  }

  try {
    dr_evt::Sim_Params params;
    params.m_infile = filtered_input_path;
    params.m_trace_type = dr_evt::TraceType::PCON;
    params.m_total_nodes = 1;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_run_time_mode = dr_evt::RunTimeMode::ACTUAL;
    params.set_resource_trace(filtered_output_path);

    dr_evt::PconSimulation simulation(params);
    simulation.run();
    simulation.write_resource_trace(filtered_output_path);

    std::ifstream output(filtered_output_path);
    passed &= expect_line(
        output, "time,free_nodes,allocated_nodes,avgpcon,minpcon,maxpcon");
    passed &= expect_line(output, "0,1,0,0.000000,0.000000,0.000000");
    passed &= expect_line(output, "0,0,1,2.000000,3.000000,4.000000");
    passed &= expect_line(output, "5,1,0,0.000000,0.000000,0.000000");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    passed = false;
  }

  {
    std::ofstream input(warm_input_path);
    input << "job_submit_time,begin_time,end_time,num_nodes,exit_status,q_id,"
             "time_limit,avgpcon,minpcon,maxpcon\n"
          << "0,1,5,2,0,1,4,1.5,2,3\n"
          << "3,3,4,2,0,1,1,0.5,1,1.5\n";
  }

  try {
    dr_evt::Sim_Params params;
    params.m_infile = warm_input_path;
    params.m_trace_type = dr_evt::TraceType::PCON;
    params.m_total_nodes = 4;
    params.m_sim_start_time = 3;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_run_time_mode = dr_evt::RunTimeMode::ACTUAL;
    params.set_outfile(warm_job_path);

    dr_evt::PconSimulation simulation(params);
    simulation.run();
    const auto stats = simulation.get_statistics();
    if (stats.jobs_completed != 1 || stats.resource_area != 6.0 ||
        stats.utilization != 0.75) {
      std::cerr << "warm-start accounting mismatch: completed="
                << stats.jobs_completed << " area=" << stats.resource_area
                << " utilization=" << stats.utilization << '\n';
      passed = false;
    }
    simulation.write_simulated_trace();
    simulation.write_resource_trace(warm_resource_path);

    std::ifstream resources(warm_resource_path);
    passed &= expect_line(
        resources, "time,free_nodes,allocated_nodes,avgpcon,minpcon,maxpcon");
    passed &= expect_line(resources, "3,2,2,1.500000,2.000000,3.000000");
    passed &= expect_line(resources, "3,0,4,2.000000,3.000000,4.500000");
    passed &= expect_line(resources, "4,2,2,1.500000,2.000000,3.000000");
    passed &= expect_line(resources, "5,4,0,0.000000,0.000000,0.000000");

    std::ifstream jobs(warm_job_path);
#if DR_EVT_LEGACY_QUEUE_INPUT
    passed &= expect_line(
        jobs, "job_submit_time,begin_time,end_time,num_nodes,exit_status,"
              "time_limit");
    passed &= expect_line(jobs, "3,3,4,2,0,1");
#else
    passed &= expect_line(
        jobs, "job_submit_time,begin_time,end_time,num_nodes,exit_status,q_id,"
              "time_limit");
    passed &= expect_line(jobs, "3,3,4,2,0,1,1");
#endif
    std::string unexpected;
    if (std::getline(jobs, unexpected)) {
      std::cerr << "unexpected warm-start job line: " << unexpected << '\n';
      passed = false;
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    passed = false;
  }

  {
    std::ofstream input(simulation_input_path);
    input << "job_submit_time,num_nodes,q_id,time_limit,avgpcon,minpcon,"
             "maxpcon\n"
          << "0,2,1,3,1.5,2,3\n"
          << "0,1,1,1,0.5,1,1.5\n";
  }

  try {
    dr_evt::Sim_Params params;
    params.m_infile = simulation_input_path;
    params.m_trace_type = dr_evt::TraceType::PCON;
    params.m_total_nodes = 4;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_run_time_mode = dr_evt::RunTimeMode::LIMIT;
    params.set_resource_trace(simulation_output_path);

    dr_evt::PconSimulation simulation(params);
    simulation.run();
    simulation.write_resource_trace(simulation_output_path);

    std::ifstream output(simulation_output_path);
    passed &= expect_line(
        output, "time,free_nodes,allocated_nodes,avgpcon,minpcon,maxpcon");
    passed &= expect_line(output, "0,4,0,0.000000,0.000000,0.000000");
    passed &= expect_line(output, "0,2,2,1.500000,2.000000,3.000000");
    passed &= expect_line(output, "0,1,3,2.000000,3.000000,4.500000");
    passed &= expect_line(output, "1,2,2,1.500000,2.000000,3.000000");
    passed &= expect_line(output, "3,4,0,0.000000,0.000000,0.000000");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    passed = false;
  }

  std::remove(input_path);
  std::remove(output_path);
  std::remove(simulation_input_path);
  std::remove(simulation_output_path);
  std::remove(warm_input_path);
  std::remove(warm_resource_path);
  std::remove(warm_job_path);
  std::remove(filtered_input_path);
  std::remove(filtered_output_path);
  std::remove(alignment_input_path);
  std::remove(queue_free_input_path);
  std::remove(missing_pcon_input_path);
  std::remove(admission_input_path);
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
