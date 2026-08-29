/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

/**
 * Test MPI-based streaming with multiple ranks feeding jobs
 *
 * Each MPI rank:
 * - Feeds a subset of jobs (round-robin partitioning)
 * - Coordinates time advancement via MPI_Allreduce
 * - Writes output to rank-specific file
 *
 * After all ranks complete, rank 0 merges and compares with batch mode.
 */

#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"
#include <mpi.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cassert>
#include <sstream>
#include <limits>

using namespace dr_evt;

struct JobInfo {
    job_no_t idx;
    sim_time_t submit_time;
};

// Get jobs for this rank (round-robin partitioning)
std::vector<JobInfo> get_rank_jobs(const Trace& trace, int rank, int size) {
    std::vector<JobInfo> my_jobs;

    for (size_t i = rank; i < trace.data().size(); i += size) {
        const auto& job = trace.data()[i];
        sim_time_t submit = static_cast<sim_time_t>(job.get_submit_time().first) +
                           job.get_submit_time().second;
        my_jobs.push_back({static_cast<job_no_t>(i), submit});
    }

    // Sort by submit time
    std::sort(my_jobs.begin(), my_jobs.end(),
              [](const JobInfo& a, const JobInfo& b) {
                  return a.submit_time < b.submit_time;
              });

    return my_jobs;
}

void run_mpi_streaming(const std::string& trace_file, int total_nodes,
                       int rank, int size) {
    // Setup simulation with rank-specific output file
    Sim_Params params;
    params.m_infile = trace_file;
    params.m_total_nodes = total_nodes;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_duration_mode = DurationMode::EXACT;
    params.m_backfill_policy = BackfillPolicy::EASY;
    params.m_priority_policy = PriorityPolicy::FCFS;
    params.m_seed = 42;
    params.m_verbose = false;

    // IMPORTANT: Each rank writes to its own file
    std::ostringstream oss;
    oss << "/tmp/mpi_stream_rank." << rank << ".csv";
    params.set_outfile(oss.str());

    Simulation sim(params);

    // Load trace
    const auto max_num_jobs = params.m_is_jobs_set ? params.m_max_jobs : 0u;
    int rc = sim.get_trace().load_data(max_num_jobs);
    assert(rc == EXIT_SUCCESS);

    std::stable_sort(sim.get_trace().data().begin(), sim.get_trace().data().end());

    // Get this rank's jobs
    auto my_jobs = get_rank_jobs(sim.get_trace(), rank, size);

    if (rank == 0) {
        std::cout << "Total jobs: " << sim.get_trace().data().size() << std::endl;
        std::cout << "MPI ranks: " << size << std::endl;
    }

    std::cout << "Rank " << rank << ": " << my_jobs.size() << " jobs" << std::endl;

    // Coordinated feeding loop with MPI
    size_t job_pos = 0;
    int total_submitted = 0;

    while (true) {
        // Find my next submit time
        double my_next = (job_pos < my_jobs.size()) ?
                        my_jobs[job_pos].submit_time :
                        std::numeric_limits<double>::max();

        // MPI_Allreduce to find global minimum
        double global_next;
        MPI_Allreduce(&my_next, &global_next, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);

        if (global_next == std::numeric_limits<double>::max()) {
            break;  // No more jobs
        }

        // Submit my jobs at this time
        while (job_pos < my_jobs.size() &&
               my_jobs[job_pos].submit_time == global_next) {
            sim.submit_job(my_jobs[job_pos].idx, global_next);
            total_submitted++;
            job_pos++;
        }

        // All ranks advance to the same time
        sim.advance_to(global_next);
    }

    // Final advance to complete all jobs
    sim_time_t max_time = 0.0;
    for (const auto& job : sim.get_trace().data()) {
        sim_time_t submit = static_cast<sim_time_t>(job.get_submit_time().first) +
                           job.get_submit_time().second;
        sim_time_t duration = job.get_limit_time();
        max_time = std::max(max_time, submit + duration * 2);
    }
    sim.advance_to(max_time);

    // Write output
    sim.write_simulated_trace();

    std::cout << "Rank " << rank << ": submitted " << total_submitted
              << " jobs, output: " << params.get_outfile() << std::endl;
}

void run_batch_reference(const std::string& trace_file, int total_nodes) {
    Sim_Params params;
    params.m_infile = trace_file;
    params.m_total_nodes = total_nodes;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_duration_mode = DurationMode::EXACT;
    params.m_backfill_policy = BackfillPolicy::EASY;
    params.m_priority_policy = PriorityPolicy::FCFS;
    params.m_seed = 42;
    params.m_verbose = false;
    params.set_outfile("/tmp/mpi_batch.csv");

    Simulation sim(params);
    sim.run();

    std::cout << "Batch mode output: " << params.get_outfile() << std::endl;
}

bool compare_files(const std::string& file1, const std::string& file2) {
    std::ifstream f1(file1);
    std::ifstream f2(file2);

    if (!f1.is_open() || !f2.is_open()) {
        return false;
    }

    std::string line1, line2;
    while (std::getline(f1, line1) && std::getline(f2, line2)) {
        if (line1 != line2) {
            return false;
        }
    }

    // Check both reached EOF
    return f1.eof() && f2.eof();
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc < 2) {
        if (rank == 0) {
            std::cerr << "Usage: mpirun -np N " << argv[0]
                      << " trace.csv [--total_nodes M]" << std::endl;
        }
        MPI_Finalize();
        return 1;
    }

    std::string trace_file = argv[1];
    int total_nodes = 100;

    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "--total_nodes" && i + 1 < argc) {
            total_nodes = std::atoi(argv[i + 1]);
            i++;
        }
    }

    if (rank == 0) {
        std::cout << "===========================================" << std::endl;
        std::cout << "MPI Streaming Test" << std::endl;
        std::cout << "===========================================" << std::endl;
        std::cout << "Trace: " << trace_file << std::endl;
        std::cout << "Nodes: " << total_nodes << std::endl;
        std::cout << std::endl;
    }

    try {
        // All ranks run streaming mode
        run_mpi_streaming(trace_file, total_nodes, rank, size);

        // Barrier to ensure all ranks finish
        MPI_Barrier(MPI_COMM_WORLD);

        // Rank 0 runs batch mode and compares all outputs
        if (rank == 0) {
            std::cout << "\nValidating MPI streaming results..." << std::endl;

            // Step 1: Check that all ranks produced identical output
            std::cout << "\n1. Checking that all ranks agree..." << std::endl;
            bool all_ranks_agree = true;

            for (int r = 1; r < size; r++) {
                std::ostringstream rank_file;
                rank_file << "/tmp/mpi_stream_rank." << r << ".csv";

                if (!compare_files("/tmp/mpi_stream_rank.0.csv", rank_file.str())) {
                    std::cout << "  ✗ Rank 0 and rank " << r << " outputs differ!" << std::endl;
                    all_ranks_agree = false;
                }
            }

            if (all_ranks_agree) {
                std::cout << "  ✓ All " << size << " ranks produced identical output" << std::endl;
            }

            // Step 2: Compare against batch mode
            std::cout << "\n2. Comparing MPI streaming vs batch mode..." << std::endl;
            run_batch_reference(trace_file, total_nodes);

            bool matches_batch = compare_files("/tmp/mpi_stream_rank.0.csv",
                                              "/tmp/mpi_batch.csv");

            if (matches_batch) {
                std::cout << "  ✓ MPI streaming matches batch mode" << std::endl;
            } else {
                std::cout << "  ✗ MPI streaming differs from batch mode!" << std::endl;
            }

            // Final verdict
            bool success = all_ranks_agree && matches_batch;

            std::cout << "\n===========================================" << std::endl;
            if (success) {
                std::cout << "✓✓ SUCCESS: All validations passed!" << std::endl;
                std::cout << "\nValidated:" << std::endl;
                std::cout << "  ✓ All " << size << " ranks agree (deterministic)" << std::endl;
                std::cout << "  ✓ MPI coordination (MPI_Allreduce)" << std::endl;
                std::cout << "  ✓ Round-robin job partitioning" << std::endl;
                std::cout << "  ✓ Rank-specific output files" << std::endl;
                std::cout << "  ✓ MPI streaming matches batch mode" << std::endl;
            } else {
                std::cout << "✗✗ FAIL: Validation failed!" << std::endl;
                if (!all_ranks_agree) {
                    std::cout << "  ✗ Ranks produced different outputs (non-deterministic!)" << std::endl;
                }
                if (!matches_batch) {
                    std::cout << "  ✗ MPI streaming differs from batch mode" << std::endl;
                }
            }
            std::cout << "===========================================" << std::endl;

            MPI_Finalize();
            return success ? 0 : 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "Rank " << rank << " ERROR: " << e.what() << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }

    MPI_Finalize();
    return 0;
}
