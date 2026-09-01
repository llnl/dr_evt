/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include <iostream>
#include <cstdlib>
#include "params/sim_params.hpp"
#include "utils/timer.hpp"
#include "sim/sim.hpp"


int main(int argc, char** argv)
{
    int rc = EXIT_SUCCESS;
    dr_evt::Sim_Params cfg;
    cfg.getopt(argc, argv);

    // Print configuration
    cfg.print();

    double t_start = dr_evt::get_time();

    try {
        // Create and run simulation
        dr_evt::Simulation sim(cfg);
        sim.run();

        // Write simulated trace
        sim.write_simulated_trace();

        // Write resource trace (if --resource_trace specified or default location)
        std::string resource_file = cfg.get_resource_trace();
        if (resource_file.empty()) {
            // Default: derive from output file
            resource_file = cfg.get_outfile();
            if (!resource_file.empty()) {
                // Replace .csv with _resources.csv
                size_t pos = resource_file.rfind(".csv");
                if (pos != std::string::npos) {
                    resource_file = resource_file.substr(0, pos) + "_resources.csv";
                } else {
                    resource_file += "_resources.csv";
                }
            }
        }
        if (!resource_file.empty()) {
            sim.write_resource_trace(resource_file);
        }

        // Print statistics
        std::cout << std::endl;
        sim.print_stats(std::cout);
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        rc = EXIT_FAILURE;
    }

    std::cout << "\nWall clock time to run simulation: "
              << dr_evt::get_time() - t_start << " (sec)" << std::endl;

    return rc;
}
