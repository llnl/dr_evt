#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"
#include <cstdlib>
#include <iostream>

int main(int argc, char **argv) {
  dr_evt::Sim_Params params;
  params.m_infile = argv[1];
  params.m_total_nodes = 795;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_run_time_mode = dr_evt::RunTimeMode::LIMIT;
  params.m_backfill_policy = std::string(argv[3]) == "easy"
                                ? dr_evt::BackfillPolicy::EASY
                                : dr_evt::BackfillPolicy::HYBRID;
  params.m_reservation_depth = 32;
  params.m_backfill_queue_depth = 64;
  params.set_outfile("unused.csv");
  const size_t repetitions = std::strtoull(argv[2], nullptr, 10);
  size_t started = 0;
  for (size_t i = 0; i < repetitions; ++i) {
    dr_evt::Simulation sim(params);
    sim.run();
    started += sim.get_statistics().jobs_started;
  }
  std::cout << "jobs_started=" << started << '\n';
}
