#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

using namespace dr_evt;

int main(int argc, char **argv) {
  const size_t count = std::strtoull(argv[1], nullptr, 10);
  const size_t repetitions = std::strtoull(argv[2], nullptr, 10);
  const std::string mode = argv[3];
  std::ofstream("empty.csv") << "job_submit_time,num_nodes,time_limit\n";
  Sim_Params params;
  params.m_infile = "empty.csv";
  params.set_outfile("unused.csv");
  params.m_total_nodes = 100;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_run_time_mode = RunTimeMode::LIMIT;
  params.m_backfill_policy = BackfillPolicy::HYBRID;
  params.m_reservation_depth = 32;
  const size_t coverage = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 64;
  params.m_backfill_queue_depth = coverage;
  if (coverage == 0) params.m_backfill_policy = BackfillPolicy::NONE;
  Simulation sim(params);
  std::vector<Simulation::Job_Append_Request> jobs;
  jobs.push_back({0, 99, "1", 1000});
  for (size_t i = 0; i < count; ++i) jobs.push_back({0, 100, "1", 10});
  auto ids = sim.append_jobs(jobs);
  ids.erase(ids.begin());
  sim.advance_to(0);
  const auto begin = std::chrono::steady_clock::now();
  size_t checksum = 0;
  for (size_t i = 0; i < repetitions; ++i) {
#ifdef HAS_PLANNED_STARTS
    if (mode == "cached") {
      const auto result = sim.get_estimated_job_starts();
      checksum += result.jobs.size();
    } else
#endif
    {
      checksum += sim.get_job_statuses(ids).size();
    }
  }
  const auto seconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - begin).count();
  std::cout << "{\"seconds\":" << seconds << ",\"checksum\":" << checksum
            << ",\"jobs\":" << count << ",\"repetitions\":" << repetitions << "}\n";
}
