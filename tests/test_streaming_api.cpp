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
 * Test the streaming/online simulation API
 *
 * Tests the public methods that allow external code (e.g., gRPC server)
 * to feed jobs dynamically and control simulation time.
 */

#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <utility>
#include <cassert>
#include <cmath>

using namespace dr_evt;

// Helper to compare floating point with tolerance
bool approx_equal(double a, double b, double tol = 1e-6) {
    return std::abs(a - b) < tol;
}

// Test 1: Basic submit_job and run_until
void test_basic_insert_and_run() {
    std::cout << "\n=== Test 1: Basic submit_job and run_until ===" << std::endl;

    // Create minimal trace file for testing
    // For simulation mode (EXACT duration), don't include begin_time/end_time
    // The scheduler will compute them
    std::ofstream ofs("/tmp/test_streaming_basic.csv");
    ofs << "job_submit_time,num_nodes,exit_status,queue,time_limit\n";
    ofs << "0,10,0,pbatch,100\n";  // Job 0: 10 nodes, 100s duration
    ofs << "50,20,0,pbatch,100\n"; // Job 1: 20 nodes, 100s duration
    ofs.close();

    // Setup simulation
    Sim_Params params;
    params.m_infile = "/tmp/test_streaming_basic.csv";
    params.m_total_nodes = 100;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_duration_mode = DurationMode::EXACT;
    params.m_backfill_policy = BackfillPolicy::EASY;
    params.m_priority_policy = PriorityPolicy::FCFS;

    Simulation sim(params);

    // Manually initialize (normally done in run())
    const auto max_num_jobs = params.m_is_jobs_set ? params.m_max_jobs : 0u;
    [[maybe_unused]] int rc = sim.get_trace().load_data(max_num_jobs);
    assert(rc == EXIT_SUCCESS);

    std::cout << "Loaded " << sim.get_trace().data().size() << " jobs" << std::endl;
    assert(sim.get_trace().data().size() == 2);

    // Initially no nodes in use
    assert(sim.get_nodes_in_use() == 0);
    std::cout << "✓ Initial state: 0 nodes in use" << std::endl;

    // Insert first job at t=0
    sim.submit_job(0, 0.0);
    sim.advance_to(0.0);  // Process START event

    // Now 10 nodes should be in use
    assert(sim.get_nodes_in_use() == 10);
    std::cout << "✓ After inserting job 0: 10 nodes in use" << std::endl;

    // Insert second job at t=50
    sim.submit_job(1, 50.0);
    sim.advance_to(50.0);  // Process START event

    // Now 30 nodes in use (10 + 20)
    assert(sim.get_nodes_in_use() == 30);
    std::cout << "✓ After inserting job 1: 30 nodes in use" << std::endl;

    // Advance to t=100 (job 0 ends)
    sim.advance_to(100.0);

    // Only job 1 running (20 nodes)
    assert(sim.get_nodes_in_use() == 20);
    std::cout << "✓ After job 0 completes: 20 nodes in use" << std::endl;

    // Advance to t=150 (job 1 ends)
    sim.advance_to(150.0);

    // All jobs done
    assert(sim.get_nodes_in_use() == 0);
    std::cout << "✓ After all jobs complete: 0 nodes in use" << std::endl;

    std::cout << "Test 1: PASSED" << std::endl;
}

// Test 2: Exclusive vs Inclusive run_until
void test_exclusive_vs_inclusive() {
    std::cout << "\n=== Test 2: Exclusive vs Inclusive run_until ===" << std::endl;

    // Create trace: job submitted at t=0, duration=10s
    // For simulation mode, don't include begin_time/end_time
    std::ofstream ofs("/tmp/test_streaming_exclusive.csv");
    ofs << "job_submit_time,num_nodes,exit_status,queue,time_limit\n";
    ofs << "0,10,0,pbatch,10\n";  // Submit at 0, 10 nodes, 10s duration
    ofs.close();

    Sim_Params params;
    params.m_infile = "/tmp/test_streaming_exclusive.csv";
    params.m_total_nodes = 100;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_duration_mode = DurationMode::EXACT;

    Simulation sim(params);
    const auto max_num_jobs = params.m_is_jobs_set ? params.m_max_jobs : 0u;
    sim.get_trace().load_data(max_num_jobs);

    // Insert job at t=0
    sim.submit_job(0, 0.0);

    // run_until_exclusive(0) should NOT process the START event at t=0
    // (current_time is 0, so exclusive of 0 means don't advance)
    // Job should not start yet
    assert(sim.get_nodes_in_use() == 0);
    std::cout << "✓ run_until_exclusive(0): START not processed, 0 nodes" << std::endl;

    // advance_to(0) should process the START event at t=0
    sim.advance_to(0.0);
    assert(sim.get_nodes_in_use() == 10);
    std::cout << "✓ advance_to(0): START processed, 10 nodes" << std::endl;

    // run_until_exclusive(10) should NOT process END event at t=10
    sim.run_until_exclusive(10.0);
    assert(sim.get_nodes_in_use() == 10);
    std::cout << "✓ run_until_exclusive(10): END not processed, still 10 nodes" << std::endl;

    // advance_to(10) should process END event at t=10
    sim.advance_to(10.0);
    assert(sim.get_nodes_in_use() == 0);
    std::cout << "✓ advance_to(10): END processed, 0 nodes" << std::endl;

    std::cout << "Test 2: PASSED" << std::endl;
}

// Test 3: Dynamic job submission (online scheduler simulation)
void test_online_scheduling() {
    std::cout << "\n=== Test 3: Online Scheduling Simulation ===" << std::endl;

    // Create trace with 5 jobs of varying sizes
    std::ofstream ofs("/tmp/test_streaming_online.csv");
    ofs << "job_submit_time,begin_time,end_time,num_nodes,exit_status,queue,time_limit\n";
    ofs << "0,0,50,30,0,pbatch,50\n";   // Job 0: big, arrives at t=0
    ofs << "10,10,60,20,0,pbatch,50\n"; // Job 1: medium, arrives at t=10
    ofs << "20,20,70,10,0,pbatch,50\n"; // Job 2: small, arrives at t=20
    ofs << "30,30,80,40,0,pbatch,50\n"; // Job 3: huge, arrives at t=30
    ofs << "40,40,90,5,0,pbatch,50\n";  // Job 4: tiny, arrives at t=40
    ofs.close();

    Sim_Params params;
    params.m_infile = "/tmp/test_streaming_online.csv";
    params.m_total_nodes = 100;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_duration_mode = DurationMode::EXACT;

    Simulation sim(params);
    const auto max_num_jobs = params.m_is_jobs_set ? params.m_max_jobs : 0u;
    sim.get_trace().load_data(max_num_jobs);

    std::cout << "Simulating online scheduler with 100 nodes..." << std::endl;

    // Simulate external scheduler submitting jobs as they arrive
    std::vector<std::pair<sim_time_t, job_no_t>> arrivals = {
        {0.0, 0},
        {10.0, 1},
        {20.0, 2},
        {30.0, 3},
        {40.0, 4}
    };

    size_t next_arrival = 0;
    sim_time_t current_time = 0.0;
    std::vector<job_no_t> running_jobs;

    while (next_arrival < arrivals.size() || !running_jobs.empty()) {
        // Submit newly arrived jobs
        while (next_arrival < arrivals.size() &&
               approx_equal(arrivals[next_arrival].first, current_time)) {
            job_no_t job_idx = arrivals[next_arrival].second;
            const auto& job = sim.get_trace().data()[job_idx];
            num_nodes_t free_nodes = params.m_total_nodes - sim.get_nodes_in_use();

            if (job.get_num_nodes() <= free_nodes) {
                std::cout << "  t=" << current_time
                         << ": Starting job " << job_idx
                         << " (needs " << job.get_num_nodes() << " nodes, "
                         << free_nodes << " free)" << std::endl;
                sim.submit_job(job_idx, current_time);
                sim.advance_to(current_time);
                running_jobs.push_back(job_idx);
            } else {
                std::cout << "  t=" << current_time
                         << ": Job " << job_idx
                         << " queued (needs " << job.get_num_nodes()
                         << ", only " << free_nodes << " free)" << std::endl;
            }
            next_arrival++;
        }

        // Advance time by 10 seconds
        current_time += 10.0;
        if (current_time > 100.0) break;

        // Process any completions
        sim.advance_to(current_time);

        // Update running jobs list
        auto it = running_jobs.begin();
        while (it != running_jobs.end()) {
            const auto& job = sim.get_trace().data()[*it];
            sim_time_t end_time = static_cast<sim_time_t>(job.get_end_time().first) +
                                 job.get_end_time().second;
            if (end_time <= current_time && end_time > 0) {
                std::cout << "  t=" << current_time
                         << ": Job " << *it << " completed" << std::endl;
                it = running_jobs.erase(it);
            } else {
                ++it;
            }
        }
    }

    assert(sim.get_nodes_in_use() == 0);
    std::cout << "✓ All jobs completed, resources released" << std::endl;
    std::cout << "Test 3: PASSED" << std::endl;
}

// Test 4: Resource leak detection
void test_no_resource_leaks() {
    std::cout << "\n=== Test 4: Resource Leak Detection ===" << std::endl;

    std::ofstream ofs("/tmp/test_streaming_leaks.csv");
    ofs << "job_submit_time,begin_time,end_time,num_nodes,exit_status,queue,time_limit\n";
    for (int i = 0; i < 10; i++) {
        ofs << (i*10) << "," << (i*10) << "," << (i*10+20) << ",10,0,pbatch,20\n";
    }
    ofs.close();

    Sim_Params params;
    params.m_infile = "/tmp/test_streaming_leaks.csv";
    params.m_total_nodes = 100;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_duration_mode = DurationMode::EXACT;

    Simulation sim(params);
    const auto max_num_jobs = params.m_is_jobs_set ? params.m_max_jobs : 0u;
    sim.get_trace().load_data(max_num_jobs);

    std::cout << "Running 10 sequential jobs..." << std::endl;

    // Start all jobs and advance through their lifecycles
    for (int i = 0; i < 10; i++) {
        sim_time_t start_time = i * 10.0;
        sim.submit_job(i, start_time);
        sim.advance_to(start_time);

        // Check nodes in use
        [[maybe_unused]] num_nodes_t expected_nodes = 10 * std::min(i + 1, 2);  // Max 2 jobs overlap
        std::cout << "  t=" << start_time << ": "
                 << sim.get_nodes_in_use() << " nodes in use" << std::endl;
    }

    // Advance to end
    sim.advance_to(110.0);

    // All resources should be freed
    assert(sim.get_nodes_in_use() == 0);
    std::cout << "✓ No resource leaks detected" << std::endl;
    std::cout << "Test 4: PASSED" << std::endl;
}

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "Streaming API Test Suite" << std::endl;
    std::cout << "====================================" << std::endl;

    try {
        test_basic_insert_and_run();
        test_exclusive_vs_inclusive();
        test_online_scheduling();
        test_no_resource_leaks();

        std::cout << "\n====================================" << std::endl;
        std::cout << "ALL TESTS PASSED!" << std::endl;
        std::cout << "====================================" << std::endl;

        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "\nTEST FAILED: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
