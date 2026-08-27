/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
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
