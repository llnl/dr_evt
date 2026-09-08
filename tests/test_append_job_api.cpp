/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/**
 * Test the streaming/online simulation API: Trace::append_job()/
 * Simulation::append_job() (the real streaming insertion point, for a
 * job the trace has never seen before), Trace::append_jobs()/
 * Simulation::append_jobs() (the batch counterpart - several such jobs
 * in one call), together with submit_job()/advance_to() (which neither
 * append_job() nor append_jobs() replace - a job still has to be
 * submitted to the scheduler after being appended, same as before).
 *
 * This file used to be two: this one covered only append_job()'s own
 * insertion behavior (reclaim-before-grow, capacity, validation), while
 * a separate test_streaming_api.cpp covered submit_job()/advance_to()'s
 * general correctness by loading a small, hand-written CSV via
 * load_data() first. That split turned out to be unnecessary - none of
 * test_streaming_api.cpp's tests actually depended on jobs coming from a
 * preloaded file (each one's submit times were hand-written to match
 * the CSV exactly, never diverging from it), so the exact same coverage
 * is achievable via append_job() instead, with no file and no gRPC
 * needed. Consolidated here to remove that redundancy.
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
#include <stdexcept>
#include <limits>
#include <algorithm>
#include <cstdlib>

using namespace dr_evt;

// Helper to compare floating point with tolerance
bool approx_equal(double a, double b, double tol = 1e-6) {
    return std::abs(a - b) < tol;
}

// Every test below needs a Simulation constructed against *some*
// existing, valid-header CSV - Trace's constructor validates the header
// regardless of whether load_data() is ever called. None of these tests
// want the server/simulation to actually know about any job in advance
// (that's the whole point - each job only becomes known via
// append_job()), so this file is always header-only, zero data rows.
const char* EMPTY_TRACE_PATH = "/tmp/test_append_job_api_empty.csv";

void write_empty_trace_fixture() {
    std::ofstream ofs(EMPTY_TRACE_PATH);
    ofs << "job_submit_time,num_nodes,queue,time_limit\n";
}

Sim_Params make_params() {
    Sim_Params params;
    params.m_infile = EMPTY_TRACE_PATH;
    params.m_total_nodes = 100;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_run_time_mode = RunTimeMode::LIMIT;
    return params;
}

// Test 1: genuinely new jobs, no prior trace-file knowledge at all -
// the case the whole feature exists for.
void test_append_with_empty_trace() {
    std::cout << "\n=== Test 1: append_job() with zero jobs preloaded ===" << std::endl;

    Sim_Params params = make_params();
    params.set_outfile("/tmp/test_append_empty_out.csv");

    Simulation sim(params);
    [[maybe_unused]] int rc = sim.get_trace().load_data(0);
    assert(rc == 0);
    assert(sim.get_trace().data().size() == 0);

    job_no_t j0 = sim.append_job(0.0, 10, "pbatch", 100);
    assert(j0 == 0);
    job_no_t j1 = sim.append_job(5.0, 20, "pbatch", 200);
    assert(j1 == 1);
    assert(sim.get_trace().data().size() == 2);

    sim.advance_to(1000.0);

    [[maybe_unused]] const auto& job0 = sim.get_trace().job_at(0);
    [[maybe_unused]] const auto& job1 = sim.get_trace().job_at(1);
    assert(job0.is_scheduled());
    assert(job1.is_scheduled());
    assert(job0.get_end_time().first == 100);   // 0 + limit_time 100
    assert(job1.get_end_time().first == 205);   // 5 + limit_time 200

    sim.write_simulated_trace();
    assert(sim.get_trace().completed_count() == 2);

    std::cout << "  PASSED" << std::endl;
}

// Test 2: reclaim-before-grow at the actual insertion point (append_job(),
// not load_data() - see OUT_TRACE_STREAMING.md's "Reclaim at the point
// of need" section for why load_data() itself never needs this).
// Forces capacity=1 so a second append can only succeed by reclaiming
// the first (already-finished) job's slot, not by growing.
void test_append_reclaims_before_growing() {
    std::cout << "\n=== Test 2: append_job() reclaims before growing ===" << std::endl;

    Sim_Params params = make_params();
    params.set_outfile("/tmp/test_append_reclaim_out.csv");
    params.m_job_store_capacity = 1;
    params.m_job_store_overflow = CircularOverflowPolicy::GROW;

    Simulation sim(params);
    sim.get_trace().set_job_store_capacity(1);
    sim.get_trace().set_job_store_overflow(CircularOverflowPolicy::GROW);
    sim.get_trace().load_data(0);

    [[maybe_unused]] job_no_t j0 = sim.append_job(0.0, 10, "pbatch", 50);
    sim.advance_to(60.0);  // job0 finishes at t=50 - its slot is now reclaimable

    assert(sim.get_trace().data().capacity() == 1);

    [[maybe_unused]] job_no_t j1 = sim.append_job(60.0, 10, "pbatch", 50);
    assert(j1 == 1);
    // The point of this test: capacity must still be 1 (reclaimed
    // job0's slot) - if this were 2, append_job() grew instead of
    // reclaiming, which is the wrong order.
    assert(sim.get_trace().data().capacity() == 1);
    assert(sim.get_trace().num_reclaimed() == 1);

    sim.advance_to(200.0);

    sim.write_simulated_trace();
    assert(sim.get_trace().completed_count() == 2);

    std::cout << "  PASSED" << std::endl;
}

// Test 3: append_job() honors the same "submit_time >= current_time"
// precondition submit_job() already enforces.
void test_append_rejects_past_submit_time() {
    std::cout << "\n=== Test 3: append_job() rejects submit_time < current_time ===" << std::endl;

    Simulation sim(make_params());
    sim.get_trace().load_data(0);

    [[maybe_unused]] job_no_t j0 = sim.append_job(0.0, 10, "pbatch", 50);
    sim.advance_to(100.0);

    [[maybe_unused]] bool threw = false;
    try {
        sim.append_job(50.0, 10, "pbatch", 50);  // 50 < current_time (100)
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);

    std::cout << "  PASSED" << std::endl;
}

// Test 4: basic append_jobs() batch call - several genuinely new jobs
// in one call, run to completion.
void test_append_jobs_batch() {
    std::cout << "\n=== Test 4: append_jobs() batch append ===" << std::endl;

    Sim_Params params = make_params();
    params.set_outfile("/tmp/test_append_jobs_batch_out.csv");

    Simulation sim(params);
    sim.get_trace().load_data(0);

    std::vector<Simulation::Job_Append_Request> reqs = {
        {0.0, 10, "pbatch", 100},
        {5.0, 20, "pbatch", 200},
        {10.0, 15, "pbatch", 150},
    };
    auto job_nos = sim.append_jobs(reqs);
    assert(job_nos.size() == 3);
    assert(job_nos[0] == 0 && job_nos[1] == 1 && job_nos[2] == 2);

    sim.advance_to(1000.0);
    sim.write_simulated_trace();
    assert(sim.get_trace().completed_count() == 3);

    std::cout << "  PASSED" << std::endl;
}

// Test 5: append_jobs() rejects a batch that isn't sorted by
// submit_time - and, being all-or-nothing on input validation, leaves
// m_data completely untouched (not partially appended up to the bad
// request).
void test_append_jobs_rejects_unsorted_batch() {
    std::cout << "\n=== Test 5: append_jobs() rejects unsorted batch ===" << std::endl;

    Simulation sim(make_params());
    sim.get_trace().load_data(0);

    std::vector<Simulation::Job_Append_Request> bad_reqs = {
        {10.0, 10, "pbatch", 100},
        {5.0, 20, "pbatch", 200},  // out of order
    };
    [[maybe_unused]] bool threw = false;
    try {
        sim.append_jobs(bad_reqs);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
    assert(sim.get_trace().data().size() == 0);

    std::cout << "  PASSED" << std::endl;
}

// Test 6: append_jobs() rejects a batch containing any submit_time <
// current_time - and, same all-or-nothing input validation as test 5,
// leaves m_data untouched (the one job already appended earlier stays,
// nothing from the rejected batch is added).
void test_append_jobs_rejects_past_submit_time() {
    std::cout << "\n=== Test 6: append_jobs() rejects a batch with a past submit_time ===" << std::endl;

    Simulation sim(make_params());
    sim.get_trace().load_data(0);

    [[maybe_unused]] job_no_t j0 = sim.append_job(0.0, 10, "pbatch", 50);
    sim.advance_to(100.0);

    std::vector<Simulation::Job_Append_Request> past_reqs = {
        {150.0, 10, "pbatch", 50},
        {50.0, 10, "pbatch", 50},  // 50 < current_time (100)
    };
    [[maybe_unused]] bool threw = false;
    try {
        sim.append_jobs(past_reqs);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
    assert(sim.get_trace().data().size() == 1);  // only j0 - batch rejected entirely

    std::cout << "  PASSED" << std::endl;
}

// Test 7: reclaim-before-grow (established for the single-job path in
// test 2) holds the same way within a batch call - each request in the
// batch goes through the same per-job check-full()-reclaim-grow order,
// not a single capacity computation for the whole batch upfront.
void test_append_jobs_reclaims_before_growing() {
    std::cout << "\n=== Test 7: append_jobs() reclaims before growing, within a batch ===" << std::endl;

    Sim_Params params = make_params();
    params.set_outfile("/tmp/test_append_jobs_reclaim_out.csv");
    params.m_job_store_capacity = 1;
    params.m_job_store_overflow = CircularOverflowPolicy::GROW;

    Simulation sim(params);
    sim.get_trace().set_job_store_capacity(1);
    sim.get_trace().set_job_store_overflow(CircularOverflowPolicy::GROW);
    sim.get_trace().load_data(0);

    [[maybe_unused]] job_no_t f0 = sim.append_job(0.0, 10, "pbatch", 50);
    sim.advance_to(60.0);  // f0 finishes at t=50 - its slot is now reclaimable
    assert(sim.get_trace().data().capacity() == 1);

    // First request in the batch reclaims f0's slot (capacity stays 1);
    // the second can't be accommodated by reclaiming again (nothing
    // else is finished yet), so it grows the buffer - same order as a
    // lone append_job() call would follow, just executed twice in a
    // row within one append_jobs() call.
    std::vector<Simulation::Job_Append_Request> batch = {
        {60.0, 10, "pbatch", 50},
        {65.0, 10, "pbatch", 50},
    };
    auto job_nos = sim.append_jobs(batch);
    assert(job_nos.size() == 2);
    assert(sim.get_trace().num_reclaimed() == 1);
    assert(sim.get_trace().data().capacity() == 2);

    std::cout << "  PASSED" << std::endl;
}

// Test 8: append_jobs() with --job_store_overflow=abort is fully
// atomic on capacity exhaustion too, not just input validation (tests
// 5-6) - capacity for the whole batch is resolved before anything is
// appended (see Trace::append_jobs()'s doc comment), so a batch that
// can't fit even after reclaiming leaves m_data completely untouched,
// not partially filled up to wherever the old buffer ran out.
void test_append_jobs_abort_is_atomic() {
    std::cout << "\n=== Test 8: append_jobs() abort-on-exhaustion leaves m_data untouched ===" << std::endl;

    Sim_Params params = make_params();
    params.m_job_store_capacity = 1;
    params.m_job_store_overflow = CircularOverflowPolicy::ABORT;

    Simulation sim(params);
    sim.get_trace().set_job_store_capacity(1);
    sim.get_trace().set_job_store_overflow(CircularOverflowPolicy::ABORT);
    sim.get_trace().load_data(0);

    [[maybe_unused]] job_no_t f0 = sim.append_job(0.0, 10, "pbatch", 50);
    // Still running (hasn't reached end_time=50 yet) - not reclaimable.
    assert(sim.get_trace().data().size() == 1);

    std::vector<Simulation::Job_Append_Request> batch = {
        {10.0, 10, "pbatch", 50},
        {20.0, 10, "pbatch", 50},
    };
    [[maybe_unused]] bool threw = false;
    try {
        sim.append_jobs(batch);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
    // f0 alone, untouched - nothing from the rejected batch was added.
    assert(sim.get_trace().data().size() == 1);
    assert(sim.get_trace().data().capacity() == 1);

    std::cout << "  PASSED" << std::endl;
}

// Test 9: append_jobs() over an empty request vector is a valid no-op -
// returns an empty vector, doesn't touch m_data or throw.
void test_append_jobs_empty_batch() {
    std::cout << "\n=== Test 9: append_jobs() with an empty request vector ===" << std::endl;

    Simulation sim(make_params());
    sim.get_trace().load_data(0);

    std::vector<Simulation::Job_Append_Request> empty_reqs;
    auto job_nos = sim.append_jobs(empty_reqs);
    assert(job_nos.empty());
    assert(sim.get_trace().data().size() == 0);

    std::cout << "  PASSED" << std::endl;
}

// Test 10: basic append_job()+submit_job()+advance_to() sequencing -
// two jobs, checking nodes-in-use at each stage of their overlapping
// lifecycle.
void test_basic_append_and_run() {
    std::cout << "\n=== Test 10: Basic append_job() and run_until ===" << std::endl;

    Simulation sim(make_params());
    sim.get_trace().load_data(0);

    assert(sim.get_nodes_in_use() == 0);
    std::cout << "  Initial state: 0 nodes in use" << std::endl;

    [[maybe_unused]] job_no_t j0 = sim.append_job(0.0, 10, "pbatch", 100);
    sim.advance_to(0.0);
    assert(sim.get_nodes_in_use() == 10);
    std::cout << "  After job 0 appended+submitted: 10 nodes in use" << std::endl;

    [[maybe_unused]] job_no_t j1 = sim.append_job(50.0, 20, "pbatch", 100);
    sim.advance_to(50.0);
    assert(sim.get_nodes_in_use() == 30);
    std::cout << "  After job 1 appended+submitted: 30 nodes in use" << std::endl;

    sim.advance_to(100.0);  // job 0 ends
    assert(sim.get_nodes_in_use() == 20);
    std::cout << "  After job 0 completes: 20 nodes in use" << std::endl;

    sim.advance_to(150.0);  // job 1 ends
    assert(sim.get_nodes_in_use() == 0);
    std::cout << "  After all jobs complete: 0 nodes in use" << std::endl;

    std::cout << "  PASSED" << std::endl;
}

// Test 11: exclusive vs inclusive advance - run_until_exclusive() must
// not process an event exactly at its target time, advance_to() must.
void test_exclusive_vs_inclusive() {
    std::cout << "\n=== Test 11: Exclusive vs Inclusive run_until ===" << std::endl;

    Simulation sim(make_params());
    sim.get_trace().load_data(0);

    [[maybe_unused]] job_no_t j0 = sim.append_job(0.0, 10, "pbatch", 10);

    // run_until_exclusive(0) must not process the START event at t=0.
    assert(sim.get_nodes_in_use() == 0);
    std::cout << "  run_until_exclusive(0): START not processed, 0 nodes" << std::endl;

    sim.advance_to(0.0);
    assert(sim.get_nodes_in_use() == 10);
    std::cout << "  advance_to(0): START processed, 10 nodes" << std::endl;

    sim.run_until_exclusive(10.0);
    assert(sim.get_nodes_in_use() == 10);
    std::cout << "  run_until_exclusive(10): END not processed, still 10 nodes" << std::endl;

    sim.advance_to(10.0);
    assert(sim.get_nodes_in_use() == 0);
    std::cout << "  advance_to(10): END processed, 0 nodes" << std::endl;

    std::cout << "  PASSED" << std::endl;
}

// Test 12: online scheduling loop - jobs genuinely appended as they
// "arrive" (rather than all known upfront), a free-node check before
// each submission, and periodic polling to detect completions. This is
// the shape a real streaming caller (e.g. the gRPC server, driven one
// arrival at a time) actually takes.
void test_online_scheduling() {
    std::cout << "\n=== Test 12: Online Scheduling Simulation ===" << std::endl;

    Simulation sim(make_params());
    sim.get_trace().load_data(0);

    std::cout << "  Simulating online scheduler with 100 nodes..." << std::endl;

    // Each job's own (arrival_time, num_nodes) - appended one at a time
    // as the loop reaches its arrival_time, not known to the simulation
    // in advance.
    struct Arrival { sim_time_t time; num_nodes_t num_nodes; };
    std::vector<Arrival> arrivals = {
        {0.0, 30}, {10.0, 20}, {20.0, 10}, {30.0, 40}, {40.0, 5}
    };

    size_t next_arrival = 0;
    sim_time_t current_time = 0.0;
    std::vector<job_no_t> running_jobs;

    while (next_arrival < arrivals.size() || !running_jobs.empty()) {
        while (next_arrival < arrivals.size() &&
               approx_equal(arrivals[next_arrival].time, current_time)) {
            const auto& arr = arrivals[next_arrival];
            num_nodes_t free_nodes = 100 - sim.get_nodes_in_use();

            if (arr.num_nodes <= free_nodes) {
                job_no_t job_idx = sim.append_job(current_time, arr.num_nodes, "pbatch", 50);
                sim.advance_to(current_time);
                running_jobs.push_back(job_idx);
                std::cout << "    t=" << current_time << ": started job " << job_idx
                          << " (" << arr.num_nodes << " nodes, " << free_nodes << " free)"
                          << std::endl;
            } else {
                std::cout << "    t=" << current_time << ": arrival needing "
                          << arr.num_nodes << " nodes queued (" << free_nodes
                          << " free)" << std::endl;
            }
            next_arrival++;
        }

        current_time += 10.0;
        if (current_time > 100.0) break;

        sim.advance_to(current_time);

        auto it = running_jobs.begin();
        while (it != running_jobs.end()) {
            const auto& job = sim.get_trace().job_at(*it);
            sim_time_t end_time = static_cast<sim_time_t>(job.get_end_time().first) +
                                 job.get_end_time().second;
            if (end_time <= current_time && end_time > 0) {
                std::cout << "    t=" << current_time << ": job " << *it << " completed"
                          << std::endl;
                it = running_jobs.erase(it);
            } else {
                ++it;
            }
        }
    }

    assert(sim.get_nodes_in_use() == 0);
    std::cout << "  All jobs completed, resources released" << std::endl;
    std::cout << "  PASSED" << std::endl;
}

// Test 13: resource leak detection across many sequential append+submit
// cycles.
void test_no_resource_leaks() {
    std::cout << "\n=== Test 13: Resource Leak Detection ===" << std::endl;

    Simulation sim(make_params());
    sim.get_trace().load_data(0);

    std::cout << "  Running 10 sequential jobs..." << std::endl;

    for (int i = 0; i < 10; i++) {
        sim_time_t start_time = i * 10.0;
        [[maybe_unused]] job_no_t job_idx = sim.append_job(start_time, 10, "pbatch", 20);
        sim.advance_to(start_time);
        std::cout << "    t=" << start_time << ": "
                  << sim.get_nodes_in_use() << " nodes in use" << std::endl;
    }

    sim.advance_to(110.0);
    assert(sim.get_nodes_in_use() == 0);
    std::cout << "  No resource leaks detected" << std::endl;
    std::cout << "  PASSED" << std::endl;
}

// Test 14: advance_to() idle-gap postcondition (m_current_time ==
// target_time even when nothing is left to process before target_time)
// - exactly the situation a real streaming caller sits in while waiting
// for the next arrival. Two jobs appended+submitted up front finish
// early, then a long idle gap before a third job genuinely arrives
// later (appended only once the loop reaches it, not known beforehand).
void test_advance_to_idle_gap() {
    std::cout << "\n=== Test 14: advance_to() Idle Gap Postcondition ===" << std::endl;

    Simulation sim(make_params());
    sim.get_trace().load_data(0);

    [[maybe_unused]] job_no_t j0 = sim.append_job(0.0, 30, "pbatch", 50);
    [[maybe_unused]] job_no_t j1 = sim.append_job(10.0, 30, "pbatch", 50);

    // Repeatedly advance in fixed 100-unit steps, like a caller polling
    // at a regular interval rather than knowing exactly where the gap
    // ends. Both jobs finish by t=60, so every step from 100 through 400
    // lands in the idle gap with nothing left to process - this
    // exercises the postcondition across several consecutive idle
    // calls, not just one.
    for (sim_time_t target = 100.0; target <= 400.0; target += 100.0) {
        sim.advance_to(target);
        assert(approx_equal(sim.get_current_time(), target));
        assert(sim.get_nodes_in_use() == 0);
        std::cout << "  get_current_time() == " << target
                  << " during idle gap, 0 nodes in use" << std::endl;
    }

    // A third job genuinely arrives at t=500 - appended only now, not
    // known in advance like the other two.
    [[maybe_unused]] job_no_t j2 = sim.append_job(500.0, 30, "pbatch", 50);
    sim.advance_to(500.0);
    assert(approx_equal(sim.get_current_time(), 500.0));
    assert(sim.get_nodes_in_use() == 30);
    std::cout << "  get_current_time() == 500, job 2 running (30 nodes)" << std::endl;

    sim.advance_to(std::numeric_limits<sim_time_t>::max());
    assert(sim.get_nodes_in_use() == 0);
    std::cout << "  All jobs complete after draining to infinity" << std::endl;

    std::cout << "  PASSED" << std::endl;
}

// Test 15: append_jobs()'s own capacity/overflow handling must be the
// only thing that decides grow-vs-abort for a batch - reclaim_front_jobs()
// must not also weigh in with its own single-job-shaped fallback (see
// its handle_overflow parameter). Uses a batch (5) bigger than
// capacity's first doubling (1 -> 2) specifically because a smaller
// batch can land both paths on the same final capacity by coincidence,
// hiding the bug (this happened during development: an earlier,
// incorrect fix to reclaim_front_jobs() passed every existing test
// here, including test_append_jobs_reclaims_before_growing() above,
// because that test's batch of 2 from a starting capacity of 1
// happens to match reclaim_front_jobs()'s own doubling exactly).
void test_append_jobs_batch_capacity_isolated_from_single_job_fallback() {
    std::cout << "\n=== Test 15: append_jobs() capacity handling isolated from "
                 "reclaim_front_jobs()'s own fallback ===" << std::endl;

    // GROW: must go straight from capacity 1 to 8 (1->2->4->8, doubling
    // until >= needed=5) - not stop at an intermediate 2 that
    // reclaim_front_jobs() might otherwise pick on its own.
    {
        Sim_Params params = make_params();
        params.set_outfile("/tmp/test_append_jobs_isolated_grow_out.csv");
        params.m_job_store_capacity = 1;
        params.m_job_store_overflow = CircularOverflowPolicy::GROW;

        Simulation sim(params);
        sim.get_trace().set_job_store_capacity(1);
        sim.get_trace().set_job_store_overflow(CircularOverflowPolicy::GROW);
        sim.get_trace().load_data(0);

        [[maybe_unused]] job_no_t f0 = sim.append_job(0.0, 10, "pbatch", 50);
        sim.advance_to(60.0);
        assert(sim.get_trace().data().capacity() == 1);

        std::vector<Simulation::Job_Append_Request> batch;
        for (int i = 0; i < 5; ++i) batch.push_back({60.0 + i, 10, "pbatch", 50});
        auto job_nos = sim.append_jobs(batch);
        assert(job_nos.size() == 5);
        assert(sim.get_trace().data().capacity() == 8);
    }

    // ABORT: must fail with append_jobs()'s own, batch-aware message,
    // not reclaim_front_jobs()'s generic single-job one (which would be
    // actively wrong here anyway, since the front WAS reclaimed - this
    // batch fails on total size, not on an unreclaimable front).
    {
        Sim_Params params = make_params();
        params.m_job_store_capacity = 1;
        params.m_job_store_overflow = CircularOverflowPolicy::ABORT;

        Simulation sim(params);
        sim.get_trace().set_job_store_capacity(1);
        sim.get_trace().set_job_store_overflow(CircularOverflowPolicy::ABORT);
        sim.get_trace().load_data(0);

        [[maybe_unused]] job_no_t f0 = sim.append_job(0.0, 10, "pbatch", 50);
        sim.advance_to(60.0);

        std::vector<Simulation::Job_Append_Request> batch;
        for (int i = 0; i < 5; ++i) batch.push_back({60.0 + i, 10, "pbatch", 50});

        [[maybe_unused]] bool threw = false;
        try {
            sim.append_jobs(batch);
        } catch (const std::runtime_error& e) {
            threw = true;
            std::string msg = e.what();
            assert(msg.find("batch of 5 requests") != std::string::npos);
            assert(msg.find("front job isn't safe to reclaim yet") == std::string::npos);
        }
        assert(threw);
    }

    std::cout << "  PASSED" << std::endl;
}

// Test 16: submit_job() must record m_busy_nodes (other jobs' node
// occupancy at this job's own arrival, written out as the trace's
// "busy_nodes" column) for a streaming-arrived job, same as
// load_data()'s own submission loop already does for a batch-loaded
// one. Without this, a streaming job's busy_nodes silently stays at
// the constructor's default of 0 forever - wrong, not just unset,
// whenever something else happens to be running when it arrives.
void test_submit_job_records_busy_nodes() {
    std::cout << "\n=== Test 16: submit_job() records busy_nodes for streaming jobs ==="
              << std::endl;

    Simulation sim(make_params());
    sim.get_trace().load_data(0);

    // job0 arrives alone - nothing else running yet, busy_nodes must be 0.
    job_no_t j0 = sim.append_job(0.0, 30, "pbatch", 100);
    sim.advance_to(0.0);
    assert(sim.get_nodes_in_use() == 30);

    // job1 arrives at t=10, while job0's 30 nodes are still in use -
    // busy_nodes for job1 must reflect that occupancy (30), not 0.
    job_no_t j1 = sim.append_job(10.0, 20, "pbatch", 100);

    [[maybe_unused]] const auto& job0 = sim.get_trace().job_at(j0);
    [[maybe_unused]] const auto& job1 = sim.get_trace().job_at(j1);
    assert(job0.get_busy_nodes() == 0);
    assert(job1.get_busy_nodes() == 30);

    std::cout << "  PASSED" << std::endl;
}

// Test 17: append_jobs() must honor --check_memory_pressure the same
// way load_next_file() (progressive loading) does, since both share
// ensure_batch_capacity() - forced deterministically via the
// DR_EVT_TEST_AVAILABLE_MEMORY_BYTES test seam, not by relying on the
// test machine actually being low on memory.
void test_append_jobs_memory_pressure() {
    std::cout << "\n=== Test 17: append_jobs() honors --check_memory_pressure ===" << std::endl;

    setenv("DR_EVT_TEST_AVAILABLE_MEMORY_BYTES", "10", 1);

    Simulation sim(make_params());
    sim.get_trace().set_memory_pressure_fraction(0.8);
    sim.get_trace().load_data(0);

    std::vector<Simulation::Job_Append_Request> batch = {
        {10.0, 20, "pbatch", 200.0},
        {15.0, 10, "pbatch", 100.0},
    };
    [[maybe_unused]] bool threw = false;
    try {
        sim.append_jobs(batch);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);

    unsetenv("DR_EVT_TEST_AVAILABLE_MEMORY_BYTES");
    std::cout << "  PASSED" << std::endl;
}

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "Append-Job Test Suite" << std::endl;
    std::cout << "====================================" << std::endl;

    write_empty_trace_fixture();

    try {
        test_append_with_empty_trace();
        test_append_reclaims_before_growing();
        test_append_rejects_past_submit_time();
        test_append_jobs_batch();
        test_append_jobs_rejects_unsorted_batch();
        test_append_jobs_rejects_past_submit_time();
        test_append_jobs_reclaims_before_growing();
        test_append_jobs_abort_is_atomic();
        test_append_jobs_empty_batch();
        test_append_jobs_batch_capacity_isolated_from_single_job_fallback();
        test_basic_append_and_run();
        test_exclusive_vs_inclusive();
        test_online_scheduling();
        test_no_resource_leaks();
        test_advance_to_idle_gap();
        test_submit_job_records_busy_nodes();
        test_append_jobs_memory_pressure();

        std::cout << "\n====================================" << std::endl;
        std::cout << "ALL APPEND_JOB TESTS PASSED" << std::endl;
        std::cout << "====================================" << std::endl;
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "\nTEST FAILED: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
