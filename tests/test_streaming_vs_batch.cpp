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
 * Test: Compare streaming API results vs batch run()
 *
 * Verifies that using the streaming API (insert_job, run_until)
 * produces identical results to the standard batch run() method.
 */

#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"
#include <iostream>
#include <fstream>
#include <cassert>
#include <cmath>
#include <map>
#include <algorithm>

using namespace dr_evt;

// Helper to compare floating point with tolerance
bool approx_equal(double a, double b, double tol = 1e-6) {
    return std::abs(a - b) < tol;
}

// Create a test trace file
void create_test_trace(const std::string& filename) {
    std::ofstream ofs(filename);
    ofs << "job_submit_time,begin_time,end_time,num_nodes,exit_status,queue,time_limit\n";

    // 20 jobs with varying submit times and sizes
    ofs << "0,0,50,20,0,pbatch,50\n";
    ofs << "5,5,55,15,0,pbatch,50\n";
    ofs << "10,10,60,10,0,pbatch,50\n";
    ofs << "15,15,65,25,0,pbatch,50\n";
    ofs << "20,20,70,30,0,pbatch,50\n";
    ofs << "25,25,75,5,0,pbatch,50\n";
    ofs << "30,30,80,40,0,pbatch,50\n";
    ofs << "35,35,85,8,0,pbatch,50\n";
    ofs << "40,40,90,12,0,pbatch,50\n";
    ofs << "45,45,95,18,0,pbatch,50\n";
    ofs << "50,50,100,22,0,pbatch,50\n";
    ofs << "55,55,105,16,0,pbatch,50\n";
    ofs << "60,60,110,14,0,pbatch,50\n";
    ofs << "65,65,115,20,0,pbatch,50\n";
    ofs << "70,70,120,25,0,pbatch,50\n";
    ofs << "75,75,125,10,0,pbatch,50\n";
    ofs << "80,80,130,35,0,pbatch,50\n";
    ofs << "85,85,135,12,0,pbatch,50\n";
    ofs << "90,90,140,8,0,pbatch,50\n";
    ofs << "95,95,145,15,0,pbatch,50\n";

    ofs.close();
}

// Run simulation using standard batch run() method
std::map<job_no_t, std::pair<sim_time_t, sim_time_t>> run_batch_simulation(const std::string& trace_file) {
    std::cout << "\n=== Running BATCH simulation (standard run()) ===" << std::endl;

    Sim_Params params;
    params.m_infile = trace_file;
    params.set_outfile("batch_output.csv");
    params.m_total_nodes = 100;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_duration_mode = DurationMode::EXACT;
    params.m_backfill_policy = BackfillPolicy::EASY;
    params.m_priority_policy = PriorityPolicy::FCFS;
    params.m_seed = 42;  // Fixed seed for reproducibility

    Simulation sim(params);
    sim.run();

    // Extract results
    std::map<job_no_t, std::pair<sim_time_t, sim_time_t>> results;
    for (job_no_t idx = 0; idx < sim.get_trace().data().size(); idx++) {
        const auto& job = sim.get_trace().data()[idx];
        sim_time_t begin = static_cast<sim_time_t>(job.get_begin_time().first) +
                          job.get_begin_time().second;
        sim_time_t end = static_cast<sim_time_t>(job.get_end_time().first) +
                        job.get_end_time().second;
        results[idx] = {begin, end};
        std::cout << "  Job " << idx << ": begin=" << begin << ", end=" << end << std::endl;
    }

    sim.write_simulated_trace();

    return results;
}

// Run simulation using streaming API (submit_job + advance_to)
std::map<job_no_t, std::pair<sim_time_t, sim_time_t>> run_streaming_simulation(const std::string& trace_file) {
    std::cout << "\n=== Running STREAMING simulation (submit_job + advance_to) ===" << std::endl;

    Sim_Params params;
    params.m_infile = trace_file;
    params.set_outfile("streaming_output.csv");
    params.m_total_nodes = 100;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_duration_mode = DurationMode::EXACT;
    params.m_backfill_policy = BackfillPolicy::EASY;
    params.m_priority_policy = PriorityPolicy::FCFS;
    params.m_seed = 42;  // Same seed as batch

    Simulation sim(params);

    // Manually load data (normally done in run())
    const auto max_num_jobs = params.m_is_jobs_set ? params.m_max_jobs : 0u;
    int rc = sim.get_trace().load_data(max_num_jobs);
    assert(rc == EXIT_SUCCESS);
    std::stable_sort(sim.get_trace().data().begin(), sim.get_trace().data().end());

    std::cout << "Using streaming API (NEW DESIGN):" << std::endl;
    std::cout << "  - submit_job(): External code feeds jobs" << std::endl;
    std::cout << "  - advance_to(): Internal Scheduler makes decisions" << std::endl;

    // Submit all jobs upfront (batch pattern)
    std::cout << "\nSubmitting all " << sim.get_trace().data().size() << " jobs..." << std::endl;
    for (num_jobs_t i = 0; i < sim.get_trace().data().size(); ++i) {
        const auto& job = sim.get_trace().data()[i];
        sim_time_t submit_time = static_cast<sim_time_t>(job.get_submit_time().first) +
                                 job.get_submit_time().second;
        sim.submit_job(i, submit_time);
    }

    // Find max time to advance to
    sim_time_t max_time = 0.0;
    for (const auto& job : sim.get_trace().data()) {
        sim_time_t submit = static_cast<sim_time_t>(job.get_submit_time().first) +
                           job.get_submit_time().second;
        sim_time_t duration = job.get_limit_time();
        max_time = std::max(max_time, submit + duration * 2);
    }

    std::cout << "Advancing to time " << max_time << " (scheduler makes all decisions)..." << std::endl;
    sim.advance_to(max_time);

    std::cout << "Final time: " << sim.get_current_time() << std::endl;
    std::cout << "Nodes in use: " << sim.get_nodes_in_use() << std::endl;

    // Extract results
    std::map<job_no_t, std::pair<sim_time_t, sim_time_t>> results;
    for (job_no_t idx = 0; idx < sim.get_trace().data().size(); idx++) {
        const auto& job = sim.get_trace().data()[idx];
        sim_time_t begin = static_cast<sim_time_t>(job.get_begin_time().first) +
                          job.get_begin_time().second;
        sim_time_t end = static_cast<sim_time_t>(job.get_end_time().first) +
                        job.get_end_time().second;
        results[idx] = {begin, end};
        std::cout << "  Job " << idx << ": begin=" << begin << ", end=" << end << std::endl;
    }

    sim.write_simulated_trace();

    return results;
}

// Compare results
bool compare_results(const std::map<job_no_t, std::pair<sim_time_t, sim_time_t>>& batch,
                    const std::map<job_no_t, std::pair<sim_time_t, sim_time_t>>& streaming) {
    std::cout << "\n=== Comparing Results ===" << std::endl;

    if (batch.size() != streaming.size()) {
        std::cerr << "ERROR: Different number of jobs!" << std::endl;
        std::cerr << "  Batch: " << batch.size() << std::endl;
        std::cerr << "  Streaming: " << streaming.size() << std::endl;
        return false;
    }

    bool all_match = true;
    for (const auto& [job_idx, batch_times] : batch) {
        auto it = streaming.find(job_idx);
        if (it == streaming.end()) {
            std::cerr << "ERROR: Job " << job_idx << " not found in streaming results!" << std::endl;
            all_match = false;
            continue;
        }

        const auto& [batch_begin, batch_end] = batch_times;
        const auto& [stream_begin, stream_end] = it->second;

        bool begin_match = approx_equal(batch_begin, stream_begin);
        bool end_match = approx_equal(batch_end, stream_end);

        if (!begin_match || !end_match) {
            std::cerr << "ERROR: Job " << job_idx << " times don't match!" << std::endl;
            std::cerr << "  Batch:     begin=" << batch_begin << ", end=" << batch_end << std::endl;
            std::cerr << "  Streaming: begin=" << stream_begin << ", end=" << stream_end << std::endl;
            all_match = false;
        } else {
            std::cout << "✓ Job " << job_idx << ": times match (begin=" << batch_begin
                     << ", end=" << batch_end << ")" << std::endl;
        }
    }

    return all_match;
}

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "Streaming vs Batch Comparison Test" << std::endl;
    std::cout << "====================================" << std::endl;
    std::cout << "\nPurpose: Verify that the streaming API" << std::endl;
    std::cout << "(submit_job + advance_to) produces" << std::endl;
    std::cout << "IDENTICAL results to batch run()" << std::endl;
    std::cout << "\nNote: Both use the same Scheduler," << std::endl;
    std::cout << "so they SHOULD match exactly!" << std::endl;

    const std::string trace_file = "test_comparison_trace.csv";

    try {
        // Create test trace
        std::cout << "\nCreating test trace with 20 jobs..." << std::endl;
        create_test_trace(trace_file);

        // Run both simulations
        auto batch_results = run_batch_simulation(trace_file);
        auto streaming_results = run_streaming_simulation(trace_file);

        // Compare
        bool match = compare_results(batch_results, streaming_results);

        if (match) {
            std::cout << "\n====================================" << std::endl;
            std::cout << "✓✓✓ TEST PASSED ✓✓✓" << std::endl;
            std::cout << "Streaming API produces IDENTICAL" << std::endl;
            std::cout << "results to batch run() method!" << std::endl;
            std::cout << "====================================" << std::endl;
            return EXIT_SUCCESS;
        } else {
            std::cout << "\n====================================" << std::endl;
            std::cout << "✗✗✗ TEST FAILED ✗✗✗" << std::endl;
            std::cout << "Streaming results differ from batch!" << std::endl;
            std::cout << "====================================" << std::endl;
            return EXIT_FAILURE;
        }

    } catch (const std::exception& e) {
        std::cerr << "\nTEST FAILED: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
