/**
 * Test two-stream job feeding without MPI
 *
 * This simulates what the MPI job feeder does with 2 ranks:
 * - Stream 1: feeds jobs {0, 2, 4, 6, ...} (even indices)
 * - Stream 2: feeds jobs {1, 3, 5, 7, ...} (odd indices)
 *
 * Both streams coordinate to advance time safely, just like
 * MPI ranks would via MPI_Allreduce(MIN).
 *
 * Compare result against batch mode (all jobs at once).
 */

#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cassert>

using namespace dr_evt;

struct JobInfo {
    job_no_t idx;
    sim_time_t submit_time;
};

// Partition jobs round-robin (like MPI ranks would)
std::vector<JobInfo> get_stream_jobs(const Trace& trace, int stream_id, int num_streams) {
    std::vector<JobInfo> my_jobs;

    for (size_t i = stream_id; i < trace.data().size(); i += num_streams) {
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

// Find minimum next submit time across streams (simulates MPI_Allreduce MIN)
sim_time_t find_global_next_submit(
    const std::vector<std::vector<JobInfo>>& streams,
    const std::vector<size_t>& stream_positions) {

    sim_time_t global_min = std::numeric_limits<sim_time_t>::max();

    for (size_t s = 0; s < streams.size(); s++) {
        if (stream_positions[s] < streams[s].size()) {
            sim_time_t my_next = streams[s][stream_positions[s]].submit_time;
            global_min = std::min(global_min, my_next);
        }
    }

    return global_min;
}

// Test 1: Two-stream feeding with coordination
void test_two_stream_coordinated(const std::string& trace_file, int total_nodes) {
    std::cout << "====================================\n";
    std::cout << "Test: Two-Stream Coordinated Feeding\n";
    std::cout << "====================================\n\n";

    // Setup simulation
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
    params.m_outfile = "test_two_stream.out";

    Simulation sim(params);

    // Load trace
    const auto max_num_jobs = params.m_is_jobs_set ? params.m_max_jobs : 0u;
    int rc = sim.get_trace().load_data(max_num_jobs);
    assert(rc == EXIT_SUCCESS);

    std::stable_sort(sim.get_trace().data().begin(), sim.get_trace().data().end());

    size_t total_jobs = sim.get_trace().data().size();
    std::cout << "Total jobs: " << total_jobs << "\n";

    // Partition jobs into two streams (round-robin like MPI ranks)
    const int NUM_STREAMS = 2;
    std::vector<std::vector<JobInfo>> streams;
    for (int s = 0; s < NUM_STREAMS; s++) {
        streams.push_back(get_stream_jobs(sim.get_trace(), s, NUM_STREAMS));
        std::cout << "Stream " << s << ": " << streams[s].size() << " jobs\n";
    }
    std::cout << "\n";

    // Track position in each stream
    std::vector<size_t> stream_positions(NUM_STREAMS, 0);
    int total_submitted = 0;

    // Coordinated feeding loop (simulates MPI coordination)
    while (true) {
        // Find global next submit time (simulates MPI_Allreduce MIN)
        sim_time_t global_next = find_global_next_submit(streams, stream_positions);

        if (global_next == std::numeric_limits<sim_time_t>::max()) {
            break;  // No more jobs
        }

        // Each stream submits its jobs at this time
        for (int s = 0; s < NUM_STREAMS; s++) {
            while (stream_positions[s] < streams[s].size() &&
                   streams[s][stream_positions[s]].submit_time == global_next) {

                job_no_t job_idx = streams[s][stream_positions[s]].idx;
                sim.submit_job(job_idx, global_next);
                total_submitted++;
                stream_positions[s]++;
            }
        }

        // Advance time (safe because we coordinated via global_next)
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

    sim.write_simulated_trace();

    std::cout << "Jobs submitted: " << total_submitted << "\n";
    std::cout << "Final time: " << sim.get_current_time() << "\n";
    std::cout << "Nodes in use: " << sim.get_nodes_in_use() << "\n";
    std::cout << "Output written to: " << params.m_outfile << "\n\n";
}

// Test 2: Batch mode (all jobs at once)
void test_batch_mode(const std::string& trace_file, int total_nodes) {
    std::cout << "====================================\n";
    std::cout << "Test: Batch Mode (Baseline)\n";
    std::cout << "====================================\n\n";

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
    params.m_outfile = "test_batch.out";

    Simulation sim(params);
    sim.run();  // Batch mode

    std::cout << "Final time: " << sim.get_current_time() << "\n";
    std::cout << "Nodes in use: " << sim.get_nodes_in_use() << "\n";
    std::cout << "Output written to: " << params.m_outfile << "\n\n";
}

// Compare schedules from two files
bool compare_schedules(const std::string& file1, const std::string& file2) {
    std::ifstream f1(file1);
    std::ifstream f2(file2);

    if (!f1.is_open() || !f2.is_open()) {
        std::cerr << "ERROR: Could not open output files for comparison\n";
        return false;
    }

    std::vector<std::string> lines1, lines2;
    std::string line;

    while (std::getline(f1, line)) {
        if (!line.empty()) lines1.push_back(line);
    }
    while (std::getline(f2, line)) {
        if (!line.empty()) lines2.push_back(line);
    }

    if (lines1.size() != lines2.size()) {
        std::cerr << "ERROR: Different number of jobs\n";
        std::cerr << "  File 1: " << lines1.size() << " lines\n";
        std::cerr << "  File 2: " << lines2.size() << " lines\n";
        return false;
    }

    int mismatches = 0;
    for (size_t i = 0; i < lines1.size(); i++) {
        if (lines1[i] != lines2[i]) {
            if (mismatches < 5) {  // Show first 5 mismatches
                std::cerr << "Mismatch at line " << (i+1) << ":\n";
                std::cerr << "  Two-stream: " << lines1[i] << "\n";
                std::cerr << "  Batch:      " << lines2[i] << "\n";
            }
            mismatches++;
        }
    }

    if (mismatches > 0) {
        std::cerr << "ERROR: " << mismatches << " mismatches found\n";
        return false;
    }

    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " trace.csv [--total_nodes N]\n";
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

    std::cout << "======================================\n";
    std::cout << "Two-Stream Test (Manual Coordination)\n";
    std::cout << "======================================\n";
    std::cout << "Trace: " << trace_file << "\n";
    std::cout << "Nodes: " << total_nodes << "\n\n";

    try {
        // Run both tests
        test_two_stream_coordinated(trace_file, total_nodes);
        test_batch_mode(trace_file, total_nodes);

        // Compare results
        std::cout << "====================================\n";
        std::cout << "Comparing Schedules\n";
        std::cout << "====================================\n\n";

        if (compare_schedules("test_two_stream.out", "test_batch.out")) {
            std::cout << "✓ SUCCESS: Two-stream and batch produce identical schedules!\n\n";
            std::cout << "This validates:\n";
            std::cout << "  ✓ Round-robin job partitioning works correctly\n";
            std::cout << "  ✓ Coordinated time advancement (like MPI_Allreduce) works\n";
            std::cout << "  ✓ Streaming API produces same results as batch mode\n";
            std::cout << "  ✓ Multiple independent job sources can feed safely\n";
            return 0;
        } else {
            std::cout << "✗ FAIL: Schedules differ\n";
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
