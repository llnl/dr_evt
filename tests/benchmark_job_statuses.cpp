#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

using namespace dr_evt;

int main(int argc, char **argv) {
  const size_t jobs = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 10000;
  const size_t repetitions =
      argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1000;
  const size_t estimation_depth =
      argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 0;
  const size_t requested =
      argc > 4 ? std::strtoull(argv[4], nullptr, 10) : jobs;
  const char *trace = "/tmp/benchmark_job_statuses.csv";
  std::ofstream(trace) << "job_submit_time,num_nodes,time_limit\n";
  Sim_Params params;
  params.m_infile = trace;
  params.m_total_nodes = 100;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_run_time_mode = RunTimeMode::LIMIT;
  params.m_backfill_policy = BackfillPolicy::CONSERVATIVE;
  params.m_priority_policy = PriorityPolicy::FCFS;
  Simulation sim(params);
  sim.get_trace().load_data(0);
  const auto running = sim.append_job(0.0, 100,
#if DR_EVT_LEGACY_QUEUE_INPUT
                                      "pbatch",
#else
                                      "1",
#endif
                                      1000.0);
  sim.advance_to(0.0);
  std::vector<job_no_t> ids;
  ids.reserve(jobs);
  for (size_t i = 0; i < jobs; ++i)
    ids.push_back(sim.append_job(0.0, 1,
#if DR_EVT_LEGACY_QUEUE_INPUT
                                 "pbatch",
#else
                                 "1",
#endif
                                 10.0));
  ids.resize(std::min(requested, ids.size()));
  const auto begin = std::chrono::steady_clock::now();
  for (size_t i = 0; i < repetitions; ++i)
    (void)sim.get_job_statuses(ids, estimation_depth);
  const auto elapsed = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - begin).count();
  std::cout << "jobs=" << jobs << " requested=" << ids.size()
            << " depth=" << estimation_depth << " repetitions=" << repetitions
            << " seconds=" << elapsed << '\n';
  (void)running;
}
