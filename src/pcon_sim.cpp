/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include "params/sim_params.hpp"
#include "sim/sim.hpp"
#include "utils/timer.hpp"
#include <cstdlib>
#include <iostream>

/** Run the command-line simulator with inline Pcon accounting enabled. */
int main(int argc, char **argv) {
  int rc = EXIT_SUCCESS;
  dr_evt::Sim_Params cfg;
  cfg.getopt(argc, argv);
  cfg.print();

  const double start = dr_evt::get_time();
  try {
    dr_evt::PconSimulation simulation(cfg);
    simulation.run();
    simulation.write_simulated_trace();

    std::string resource_file = cfg.get_resource_trace();
    if (resource_file.empty()) {
      resource_file = cfg.get_outfile();
      if (!resource_file.empty()) {
        const size_t extension = resource_file.rfind(".csv");
        if (extension != std::string::npos) {
          resource_file = resource_file.substr(0, extension) + "_resources.csv";
        } else {
          resource_file += "_resources.csv";
        }
      }
    }
    if (!resource_file.empty()) {
      simulation.write_resource_trace(resource_file);
    }

    std::cout << std::endl;
    simulation.print_stats(std::cout);
  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << std::endl;
    rc = EXIT_FAILURE;
  }

  std::cout << "\nWall clock time to run simulation: "
            << dr_evt::get_time() - start << " (sec)" << std::endl;
  return rc;
}
