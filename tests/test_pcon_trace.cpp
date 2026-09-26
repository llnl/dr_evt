/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include "sim/sim.hpp"
#include "trace/trace.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>

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

bool expect_line(std::istream &input, const std::string &expected) {
  std::string actual;
  if (!std::getline(input, actual) || actual != expected) {
    std::cerr << "expected: " << expected << "\nactual:   " << actual << '\n';
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
    passed &= expect_line(
        jobs, "job_submit_time,begin_time,end_time,num_nodes,exit_status,q_id,"
              "time_limit");
    passed &= expect_line(jobs, "3,3,4,2,0,1,1");
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
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
