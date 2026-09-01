/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/**
 * MPI Job Feeder - External job submission simulation
 *
 * Each MPI rank acts as an independent job feeder (e.g., representing
 * different users, clusters, or submission sources). Ranks coordinate
 * to feed jobs to a shared simulation while respecting the streaming
 * API precondition: no job submitted with submit_time < current_time.
 *
 * Usage:
 *   mpirun -np 4 mpi_job_feeder trace.csv
 *
 * Each rank:
 *   1. Loads its partition of jobs
 *   2. Submits jobs using submit_job()
 *   3. Coordinates with other ranks to advance time safely
 *   4. Reports its statistics
 */

#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"
#include <mpi.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>

using namespace dr_evt;

// Partition jobs by rank (round-robin)
std::vector<job_no_t> get_rank_jobs(size_t total_jobs, int rank, int size) {
    std::vector<job_no_t> my_jobs;
    for (size_t i = rank; i < total_jobs; i += size) {
        my_jobs.push_back(static_cast<job_no_t>(i));
    }
    return my_jobs;
}

int main(int argc, char** argv) {
    // Initialize MPI
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc < 2) {
        if (rank == 0) {
            std::cerr << "Usage: " << argv[0] << " trace.csv [--total_nodes N]" << std::endl;
            std::cerr << "\nEach MPI rank acts as an independent job feeder.\n";
            std::cerr << "Jobs are partitioned round-robin across ranks.\n";
        }
        MPI_Finalize();
        return 1;
    }

    // Parse command-line arguments
    std::string trace_file = argv[1];
    int total_nodes = 1000;  // Default

    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "--total_nodes" && i + 1 < argc) {
            total_nodes = std::atoi(argv[i + 1]);
            i++;
        }
    }

    try {
        // Setup simulation (all ranks create same simulation)
        Sim_Params params;
        params.m_infile = trace_file;
        params.m_total_nodes = total_nodes;
        params.m_trace_format = "simple";
        params.m_timestamp_format = "epoch";
        params.m_duration_mode = DurationMode::EXACT;
        params.m_backfill_policy = BackfillPolicy::EASY;
        params.m_priority_policy = PriorityPolicy::FCFS;
        params.m_seed = 42;
        params.m_verbose = (rank == 0);  // Only rank 0 prints progress

        if (rank == 0) {
            std::cout << "====================================\n";
            std::cout << "MPI Job Feeder\n";
            std::cout << "====================================\n";
            std::cout << "MPI ranks: " << size << "\n";
            std::cout << "Trace: " << trace_file << "\n";
            std::cout << "Nodes: " << total_nodes << "\n";
            std::cout << "\n";
        }

        // Create simulation
        Simulation sim(params);

        // Load trace data
        const auto max_num_jobs = params.m_is_jobs_set ? params.m_max_jobs : 0u;
        int rc = sim.get_trace().load_data(max_num_jobs);
        if (rc != EXIT_SUCCESS) {
            if (rank == 0) {
                std::cerr << "ERROR: Failed to load trace\n";
            }
            MPI_Finalize();
            return 1;
        }

        std::stable_sort(sim.get_trace().data().begin(), sim.get_trace().data().end());

        size_t total_jobs = sim.get_trace().data().size();

        // Partition jobs by rank (round-robin)
        std::vector<job_no_t> my_jobs = get_rank_jobs(total_jobs, rank, size);

        if (rank == 0) {
            std::cout << "Total jobs: " << total_jobs << "\n";
            std::cout << "Jobs per rank: ~" << (total_jobs + size - 1) / size << "\n\n";
        }

        // Build list of jobs with their submit times for this rank
        struct JobInfo {
            job_no_t idx;
            sim_time_t submit_time;
        };
        std::vector<JobInfo> my_job_info;

        for (job_no_t job_idx : my_jobs) {
            const auto& job = sim.get_trace().data()[job_idx];
            sim_time_t submit = static_cast<sim_time_t>(job.get_submit_time().first) +
                               job.get_submit_time().second;
            my_job_info.push_back({job_idx, submit});
        }

        // Sort by submit time
        std::sort(my_job_info.begin(), my_job_info.end(),
                  [](const JobInfo& a, const JobInfo& b) {
                      return a.submit_time < b.submit_time;
                  });

        // Simulation loop with MPI coordination
        sim_time_t current_time = 0.0;
        size_t next_job_idx = 0;  // Index into my_job_info
        int jobs_submitted = 0;

        if (rank == 0) {
            std::cout << "Starting distributed job submission...\n\n";
        }

        while (true) {
            // Find my next job to submit
            sim_time_t my_next_submit = std::numeric_limits<sim_time_t>::max();
            if (next_job_idx < my_job_info.size()) {
                my_next_submit = my_job_info[next_job_idx].submit_time;
            }

            // Find global next submit time across all ranks
            sim_time_t global_next_submit;
            MPI_Allreduce(&my_next_submit, &global_next_submit, 1, MPI_DOUBLE,
                         MPI_MIN, MPI_COMM_WORLD);

            // If no more jobs from any rank, we're done
            if (global_next_submit == std::numeric_limits<sim_time_t>::max()) {
                break;
            }

            // Submit all jobs at this time point from all ranks
            // (Multiple ranks might have jobs at the same time)
            while (next_job_idx < my_job_info.size() &&
                   my_job_info[next_job_idx].submit_time == global_next_submit) {

                job_no_t job_idx = my_job_info[next_job_idx].idx;
                sim.submit_job(job_idx, global_next_submit);
                jobs_submitted++;

                if (params.m_verbose && jobs_submitted <= 5) {
                    std::cout << "[Rank " << rank << "] Submitted job " << job_idx
                             << " at t=" << global_next_submit << "\n";
                }

                next_job_idx++;
            }

            // Advance time to next submit point
            // This is SAFE because we know (via MPI_Allreduce) that no rank
            // will submit jobs before global_next_submit
            if (global_next_submit > current_time) {
                sim.advance_to(global_next_submit);
                current_time = global_next_submit;
            }

            // Barrier to keep ranks synchronized
            MPI_Barrier(MPI_COMM_WORLD);
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

        // Gather statistics from all ranks
        int total_submitted = 0;
        MPI_Reduce(&jobs_submitted, &total_submitted, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

        // Report results
        if (rank == 0) {
            std::cout << "\n====================================\n";
            std::cout << "Distributed Submission Complete\n";
            std::cout << "====================================\n";
            std::cout << "Total jobs submitted: " << total_submitted << "\n";
            std::cout << "Jobs completed: " << sim.get_trace().data().size() << "\n";
            std::cout << "Final time: " << sim.get_current_time() << "\n";
            std::cout << "Nodes in use: " << sim.get_nodes_in_use() << "\n";
            std::cout << "\n";

            // Write output
            sim.write_simulated_trace();
            std::cout << "\nResults written to output files.\n";
        } else {
            // Other ranks print their statistics
            std::cout << "[Rank " << rank << "] Jobs submitted: " << jobs_submitted << "\n";
        }

        MPI_Barrier(MPI_COMM_WORLD);

        if (rank == 0) {
            std::cout << "\nSUCCESS: Distributed job feeding completed!\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "[Rank " << rank << "] ERROR: " << e.what() << "\n";
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }

    MPI_Finalize();
    return 0;
}
