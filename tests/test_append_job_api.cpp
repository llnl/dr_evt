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
#include "sim/scheduler_fcfs_periodic_easy_circular.hpp"
#include "sim/scheduler_fcfs_periodic_hybrid_circular.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace dr_evt;

#if DR_EVT_LEGACY_QUEUE_INPUT
constexpr const char *kTestQueueInput = "pbatch";
#else
constexpr const char *kTestQueueInput =
    "1"; ///< Numeric ID of the default Queue1.
#endif

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
const char *EMPTY_TRACE_PATH = "/tmp/test_append_job_api_empty.csv";
const char *CAPACITY_SCHEDULE_PATH =
    "/tmp/test_append_job_api_capacity_schedule.csv";
const char *CAPACITY_OUTLOOK_INCREASE_PATH =
    "/tmp/test_append_job_capacity_outlook_increase.csv";
const char *CAPACITY_OUTLOOK_DROP_PATH =
    "/tmp/test_append_job_capacity_outlook_drop.csv";
const char *CAPACITY_RESET_TRACE_PATH =
    "/tmp/test_append_job_api_capacity_reset_resources.csv";

void write_empty_trace_fixture() {
  std::ofstream ofs(EMPTY_TRACE_PATH);
  ofs << "job_submit_time,num_nodes,time_limit\n";

  std::ofstream capacity(CAPACITY_SCHEDULE_PATH);
  capacity << "time,total_nodes\n"
           << "5,0\n"
           << "20,100\n"
           << "1000,25\n";
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
  std::cout << "\n=== Test 1: append_job() with zero jobs preloaded ==="
            << std::endl;

  Sim_Params params = make_params();
  params.set_outfile("/tmp/test_append_empty_out.csv");

  Simulation sim(params);
  [[maybe_unused]] int rc = sim.get_trace().load_data(0);
  assert(rc == 0);
  assert(sim.get_trace().data().size() == 0);

  [[maybe_unused]] job_no_t j0 = sim.append_job(0.0, 10, kTestQueueInput, 100);
  assert(j0 == 0);
  [[maybe_unused]] job_no_t j1 = sim.append_job(5.0, 20, kTestQueueInput, 200);
  assert(j1 == 1);
  assert(sim.get_trace().data().size() == 2);

  sim.advance_to(1000.0);

  [[maybe_unused]] const auto &job0 = sim.get_trace().job_at(0);
  [[maybe_unused]] const auto &job1 = sim.get_trace().job_at(1);
  assert(job0.is_scheduled());
  assert(job1.is_scheduled());
  assert(job0.get_end_time().first == 100); // 0 + limit_time 100
  assert(job1.get_end_time().first == 205); // 5 + limit_time 200

  sim.write_simulated_trace();
  assert(sim.get_trace().completed_count() == 2);

  std::cout << "  PASSED" << std::endl;
}

void test_appended_job_statuses() {
  std::cout << "\n=== Appended-job status query ===" << std::endl;
  Sim_Params params = make_params();
  params.m_total_nodes = 10;
  params.m_job_store_capacity = 2;
  params.m_job_flush_interval = 1;
  params.set_outfile("/tmp/test_job_status_out.csv");
  Simulation sim(params);
  sim.get_trace().load_data(0);

  const auto ids = sim.append_jobs(
      {{0.0, 10, kTestQueueInput, 10.0}, {0.0, 10, kTestQueueInput, 5.0}});
  const auto rejected_id = sim.append_job(0.0, 11, kTestQueueInput, 5.0);
  const auto rejected = sim.get_job_statuses({rejected_id}).front();
  assert(rejected.state == Simulation::Job_State::REJECTED);
  assert(!rejected.start_time && !rejected.end_time &&
         !rejected.expected_start_time);
  auto pending = sim.get_job_statuses({ids[1], ids[0], ids[1]});
  assert(pending.size() == 3);
  assert(pending[0].state == Simulation::Job_State::PENDING);
  assert(pending[0].expected_start_time == 10.0);
  assert(pending[1].expected_start_time == 0.0);
  assert(pending[2].job_idx == ids[1]);

  sim.advance_to(0.0);
  auto active = sim.get_job_statuses(ids);
  assert(active[0].state == Simulation::Job_State::RUNNING);
  assert(active[0].start_time == 0.0 && active[0].end_time == 10.0);
  assert(active[1].state == Simulation::Job_State::PENDING);
  assert(active[1].expected_start_time == 10.0);

  sim.advance_to(15.0);
  sim.flush_completed_jobs();
  auto completed = sim.get_job_statuses(ids);
  assert(completed[0].state == Simulation::Job_State::COMPLETED);
  assert(completed[1].state == Simulation::Job_State::COMPLETED);
  assert(completed[1].start_time == 10.0 && completed[1].end_time == 15.0);

  bool invalid_rejected = false;
  try {
    (void)sim.get_job_statuses({999});
  } catch (const std::out_of_range &) {
    invalid_rejected = true;
  }
  assert(invalid_rejected);
  std::cout << "  PASSED" << std::endl;
}

// Test 2: reclaim-before-grow at the actual insertion point (append_job(),
// not load_data() - see OUTPUT_TRACE_BUFFERS.md's "Reclaim at the point
// of need" section for why load_data() itself never needs this).
// Forces capacity=1 so a second append can only succeed by reclaiming
// the first (already-finished) job's slot, not by growing.
// expected_start_time is an earliest-fit projection, not a strict FCFS
// walk: a short job behind a blocked head is projected into the gap ahead
// of the head, and a job that fits now but not for its whole limit is not.
void test_expected_start_is_earliest_fit() {
  std::cout << "\n=== Expected start: earliest fit behind a blocked head ==="
            << std::endl;
  Sim_Params params = make_params();
  params.m_total_nodes = 10;
  params.set_outfile("/tmp/test_expected_start_out.csv");
  Simulation sim(params);
  sim.get_trace().load_data(0);

  // A: 6 nodes for 10 s; B: 10 nodes for 100 s (blocked until A ends);
  // C: 2 nodes for 5 s fits beside A and ends before B's reservation;
  // D: 2 nodes for 20 s fits beside A now but would overlap B.
  const auto ids = sim.append_jobs({{0.0, 6, kTestQueueInput, 10.0},
                                    {0.0, 10, kTestQueueInput, 100.0},
                                    {0.0, 2, kTestQueueInput, 5.0},
                                    {0.0, 2, kTestQueueInput, 20.0}});
  auto projected = sim.get_job_statuses(ids);
  assert(projected[0].expected_start_time == 0.0);
  assert(projected[1].expected_start_time == 10.0);
  assert(projected[2].expected_start_time == 0.0);
  assert(projected[3].expected_start_time == 110.0);
  assert(projected[0].start_time_kind ==
         Simulation::Start_Time_Kind::ESTIMATED);
  assert(projected[1].start_time_kind == Simulation::Start_Time_Kind::RESERVED);
  assert(projected[2].start_time_kind ==
         Simulation::Start_Time_Kind::ESTIMATED);
  assert(projected[3].start_time_kind ==
         Simulation::Start_Time_Kind::ESTIMATED);

  // Every projected placement is retained in the temporary profile. With the
  // cluster occupied through t=100, the head starts at 100, the first 60-node
  // job starts at 150 after the head, and the second starts at 350 after that
  // 60-node placement releases its nodes.
  Sim_Params chained_params = make_params();
  Simulation chained(chained_params);
  chained.get_trace().load_data(0);
  const auto chained_ids =
      chained.append_jobs({{0.0, 100, kTestQueueInput, 100.0},
                           {0.0, 100, kTestQueueInput, 50.0},
                           {0.0, 60, kTestQueueInput, 200.0},
                           {0.0, 60, kTestQueueInput, 10.0}});
  const auto chained_statuses = chained.get_job_statuses(chained_ids);
  assert(chained_statuses[1].expected_start_time == 100.0);
  assert(chained_statuses[2].expected_start_time == 150.0);
  assert(chained_statuses[3].expected_start_time == 350.0);

  // A finite query depth limits only pending start-time results. Statuses
  // remain request-order preserving for the complete caller-supplied list.
  const auto bounded = sim.get_job_statuses(ids, 2);
  assert(bounded.size() == ids.size());
  assert(bounded[0].expected_start_time == 0.0);
  assert(bounded[1].expected_start_time == 10.0);
  assert(bounded[0].start_time_kind == Simulation::Start_Time_Kind::ESTIMATED);
  assert(bounded[1].start_time_kind == Simulation::Start_Time_Kind::RESERVED);
  assert(!bounded[2].expected_start_time.has_value());
  assert(!bounded[3].expected_start_time.has_value());
  assert(bounded[2].start_time_kind == Simulation::Start_Time_Kind::NONE);
  assert(bounded[3].start_time_kind == Simulation::Start_Time_Kind::NONE);

  const auto enumerated = sim.get_job_statuses({}, 2);
  assert(enumerated.size() == 2);
  assert(enumerated[0].job_idx == ids[0]);
  assert(enumerated[1].job_idx == ids[1]);
  assert(enumerated[0].expected_start_time == 0.0);
  assert(enumerated[1].expected_start_time == 10.0);

  // After scheduling at t=0, A and C run; the head's projection is the
  // shadow time and D's projection is unchanged.
  sim.advance_to(0.0);
  auto scheduled = sim.get_job_statuses(ids);
  assert(scheduled[0].state == Simulation::Job_State::RUNNING);
  assert(scheduled[2].state == Simulation::Job_State::RUNNING);
  assert(scheduled[1].state == Simulation::Job_State::PENDING);
  assert(scheduled[1].expected_start_time == 10.0);
  assert(sim.get_fcfs_head_shadow_time() == 10.0);
  assert(scheduled[3].expected_start_time == 110.0);

  // The projections match what the EASY scheduler then does.
  sim.advance_to(200.0);
  auto done = sim.get_job_statuses(ids);
  assert(done[1].start_time == 10.0);
  assert(done[3].start_time == 110.0);

  std::cout << "  PASSED" << std::endl;
}

// expected_start_time is a serial forecast for the whole waiting queue, not
// the protected-reservation set of one conservative/hybrid backfill pass. A
// depth-one scan and finite horizon therefore do not suppress estimates for
// jobs behind the scan prefix: each forecast placement constrains the next.
// Once scheduled, the horizon-skipped head does not constrain the candidate;
// that later outcome is intentionally distinct from the initial forecast.
void test_expected_start_covers_unreserved_waiting_jobs() {
  std::cout << "\n=== Expected start: unreserved waiting jobs ===" << std::endl;

  for (const auto policy : {BackfillPolicy::EASY, BackfillPolicy::CONSERVATIVE,
                            BackfillPolicy::HYBRID}) {
    auto params = make_params();
    params.m_backfill_policy = policy;
    params.m_backfill_queue_depth = 1;
    params.m_planning_horizon = 100.0;
    if (policy == BackfillPolicy::HYBRID) {
      params.m_reservation_depth = 1;
    }
    Simulation sim(params);
    sim.get_trace().load_data(0);

    // A occupies 90 nodes until t=1000. H needs all 100 nodes, so its
    // projected t=1000 reservation is beyond the 100 s planning horizon.
    // C and D are both deeper than the one-job backfill scan prefix.
    const auto a = sim.append_job(0.0, 90, kTestQueueInput, 1000.0);
    const auto head = sim.append_job(0.0, 100, kTestQueueInput, 10.0);
    const auto candidate = sim.append_job(0.0, 10, kTestQueueInput, 2000.0);
    const auto deep = sim.append_job(0.0, 1, kTestQueueInput, 1.0);

    const auto statuses = sim.get_job_statuses({a, head, candidate, deep});
    assert(statuses.size() == 4);
    for (const auto &status : statuses) {
      assert(status.state == Simulation::Job_State::PENDING);
      assert(status.expected_start_time.has_value());
    }
    assert(approx_equal(*statuses[0].expected_start_time, 0.0));
    assert(approx_equal(*statuses[1].expected_start_time, 1000.0));
    assert(approx_equal(*statuses[2].expected_start_time, 1010.0));
    assert(approx_equal(*statuses[3].expected_start_time, 0.0));
    for (const auto &status : statuses) {
      assert(status.start_time_kind == Simulation::Start_Time_Kind::ESTIMATED);
    }

    // The scheduler skips H's out-of-horizon reservation and starts C at the
    // current pass. This confirms the forecast above was not exposed as a
    // reservation or guarantee.
    sim.advance_to(0.0);
    const auto started = sim.get_job_statuses({candidate}).front();
    assert(started.state == Simulation::Job_State::RUNNING);
    assert(approx_equal(*started.start_time, 0.0));
  }

  // An in-horizon blocked head is protected by both policies. Conservative
  // also protects the later blocked job, while hybrid depth one reports that
  // later start as an estimate rather than a reservation.
  for (const auto policy :
       {BackfillPolicy::CONSERVATIVE, BackfillPolicy::HYBRID}) {
    auto params = make_params();
    params.m_backfill_policy = policy;
    if (policy == BackfillPolicy::HYBRID) {
      params.m_reservation_depth = 1;
    }
    Simulation sim(params);
    sim.get_trace().load_data(0);
    const auto a = sim.append_job(0.0, 90, kTestQueueInput, 10.0);
    const auto head = sim.append_job(0.0, 100, kTestQueueInput, 5.0);
    const auto later = sim.append_job(0.0, 100, kTestQueueInput, 5.0);
    const auto statuses = sim.get_job_statuses({a, head, later});
    assert(approx_equal(*statuses[0].expected_start_time, 0.0));
    assert(approx_equal(*statuses[1].expected_start_time, 10.0));
    assert(approx_equal(*statuses[2].expected_start_time, 15.0));
    assert(statuses[0].start_time_kind ==
           Simulation::Start_Time_Kind::ESTIMATED);
    assert(statuses[1].start_time_kind ==
           Simulation::Start_Time_Kind::RESERVED);
    assert(statuses[2].start_time_kind ==
           (policy == BackfillPolicy::CONSERVATIVE
                ? Simulation::Start_Time_Kind::RESERVED
                : Simulation::Start_Time_Kind::ESTIMATED));
  }

  std::cout << "  PASSED" << std::endl;
}

// The reservation planner must retain the node-time interval of each earlier
// protected job.  Otherwise J is incorrectly reserved at t=50 (after A),
// and C is refused because it ends after that false reservation.  A chained
// plan puts H at 100, J at 150, and lets C run immediately.
void test_chained_conservative_reservations_allow_early_backfill() {
  std::cout << "\n=== Chained conservative reservation regression ==="
            << std::endl;
  for (const auto policy :
       {BackfillPolicy::CONSERVATIVE, BackfillPolicy::HYBRID}) {
    auto params = make_params();
    params.m_backfill_policy = policy;
    if (policy == BackfillPolicy::HYBRID) {
      params.m_reservation_depth = 2;
    }
    Simulation sim(params);
    sim.get_trace().load_data(0);
    const auto a = sim.append_job(0.0, 40, kTestQueueInput, 50.0);
    const auto b = sim.append_job(0.0, 50, kTestQueueInput, 100.0);
    const auto head = sim.append_job(0.0, 100, kTestQueueInput, 50.0);
    const auto later = sim.append_job(0.0, 20, kTestQueueInput, 200.0);
    const auto candidate = sim.append_job(0.0, 10, kTestQueueInput, 70.0);

    sim.advance_to(0.0);
    const auto statuses = sim.get_job_statuses({a, b, head, later, candidate});
    assert(statuses[2].expected_start_time == 100.0);
    assert(statuses[3].expected_start_time == 150.0);
    assert(statuses[4].state == Simulation::Job_State::RUNNING);
    assert(statuses[4].start_time == 0.0);

    sim.advance_to(150.0);
    const auto later_status = sim.get_job_statuses({later}).front();
    assert(later_status.state == Simulation::Job_State::RUNNING);
    assert(later_status.start_time == 150.0);
  }
  std::cout << "  PASSED" << std::endl;
}

#if defined(DR_EVT_HAS_SER20)
void test_exact_lookahead_replays_without_mutating_live_state() {
  std::cout << "\n=== Exact lookahead clone ===" << std::endl;
  auto params = make_params();
  params.m_total_nodes = 10;
  params.m_backfill_policy = BackfillPolicy::EASY;
  Simulation sim(params);
  sim.get_trace().load_data(0);
  const auto running = sim.append_job(0.0, 6, kTestQueueInput, 10.0);
  const auto head = sim.append_job(0.0, 10, kTestQueueInput, 100.0);
  const auto candidate = sim.append_job(0.0, 2, kTestQueueInput, 5.0);
  const auto impossible = sim.append_job(0.0, 11, kTestQueueInput, 1.0);
  sim.advance_to(0.0);

  const auto before_time = sim.get_current_time();
  const auto before = sim.get_job_statuses({running, head, candidate});
  const auto exact = sim.get_exact_job_start_times({head, candidate});
  assert(exact.size() == 2);
  assert(exact[0].start_time == 10.0);
  assert(exact[1].start_time == 0.0);
  assert(exact[0].outcome == Simulation::Exact_Start_Outcome::STARTED);
  assert(exact[1].outcome == Simulation::Exact_Start_Outcome::STARTED);
  const auto explicit_depth =
      sim.get_exact_job_start_times({head, candidate}, 1);
  assert(explicit_depth.size() == 2);
  const auto never = sim.get_exact_job_start_times({impossible});
  assert(never[0].outcome == Simulation::Exact_Start_Outcome::REJECTED);
  assert(!never[0].start_time.has_value());

  // The lookahead clone drains independently; the live simulation remains at
  // its original event boundary with the same status records.
  assert(sim.get_current_time() == before_time);
  const auto after = sim.get_job_statuses({running, head, candidate});
  assert(after[0].state == before[0].state);
  assert(after[1].state == before[1].state);
  assert(after[2].state == before[2].state);

  const auto enumerated = sim.get_exact_job_start_times({}, 1);
  assert(enumerated.size() == 1);
  assert(enumerated[0].job_idx == head);
  assert(enumerated[0].start_time == 10.0);
  assert(enumerated[0].outcome == Simulation::Exact_Start_Outcome::STARTED);

  // A request accepted by the physical machine limit can still become
  // unreachable when its scheduled capacity falls below the request.
  const std::string capacity_file = "/tmp/test_exact_lookahead_capacity.csv";
  {
    std::ofstream capacity(capacity_file);
    capacity << "time,total_nodes\n0,4\n";
  }
  auto reduced_params = make_params();
  reduced_params.m_total_nodes = 10;
  reduced_params.m_capacity_schedule = capacity_file;
  Simulation reduced(reduced_params);
  reduced.get_trace().load_data(0);
  const auto constrained = reduced.append_job(0.0, 8, kTestQueueInput, 1.0);
  reduced.advance_to(0.0);
  const auto unreachable = reduced.get_exact_job_start_times({constrained});
  assert(unreachable[0].outcome ==
         Simulation::Exact_Start_Outcome::CAPACITY_UNREACHABLE);
  assert(!unreachable[0].start_time.has_value());
  std::cout << "  PASSED" << std::endl;
}
#endif

void test_append_reclaims_before_growing() {
  std::cout << "\n=== Test 2: append_job() reclaims before growing ==="
            << std::endl;

  Sim_Params params = make_params();
  params.set_outfile("/tmp/test_append_reclaim_out.csv");
  params.m_job_store_capacity = 1;
  params.m_job_store_overflow = CircularOverflowPolicy::GROW;

  Simulation sim(params);
  sim.get_trace().set_job_store_capacity(1);
  sim.get_trace().set_job_store_overflow(CircularOverflowPolicy::GROW);
  sim.get_trace().load_data(0);

  [[maybe_unused]] job_no_t j0 = sim.append_job(0.0, 10, kTestQueueInput, 50);
  sim.advance_to(60.0); // job0 finishes at t=50 - its slot is now reclaimable

  assert(sim.get_trace().data().capacity() == 1);

  [[maybe_unused]] job_no_t j1 = sim.append_job(60.0, 10, kTestQueueInput, 50);
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
  std::cout
      << "\n=== Test 3: append_job() rejects submit_time < current_time ==="
      << std::endl;

  Simulation sim(make_params());
  sim.get_trace().load_data(0);

  [[maybe_unused]] job_no_t j0 = sim.append_job(0.0, 10, kTestQueueInput, 50);
  sim.advance_to(100.0);

  [[maybe_unused]] bool threw = false;
  try {
    sim.append_job(50.0, 10, kTestQueueInput, 50); // 50 < current_time (100)
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  // Out-of-order appends are rejected too: the wait queue releases
  // arrivals in insertion order, so a t=200 job appended after a t=300
  // job would silently wait until t=300.
  [[maybe_unused]] job_no_t j_late =
      sim.append_job(300.0, 10, kTestQueueInput, 50);
  threw = false;
  try {
    sim.append_job(200.0, 10, kTestQueueInput, 50);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);
  // Equal submit times remain fine.
  [[maybe_unused]] job_no_t j_same =
      sim.append_job(300.0, 10, kTestQueueInput, 50);
  std::cout << "  append_job(200) after append_job(300) rejected; equal time ok"
            << std::endl;

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
      {0.0, 10, kTestQueueInput, 100},
      {5.0, 20, kTestQueueInput, 200},
      {10.0, 15, kTestQueueInput, 150},
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
  std::cout << "\n=== Test 5: append_jobs() rejects unsorted batch ==="
            << std::endl;

  Simulation sim(make_params());
  sim.get_trace().load_data(0);

  std::vector<Simulation::Job_Append_Request> bad_reqs = {
      {10.0, 10, kTestQueueInput, 100},
      {5.0, 20, kTestQueueInput, 200}, // out of order
  };
  [[maybe_unused]] bool threw = false;
  try {
    sim.append_jobs(bad_reqs);
  } catch (const std::runtime_error &) {
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
  std::cout << "\n=== Test 6: append_jobs() rejects a batch with a past "
               "submit_time ==="
            << std::endl;

  Simulation sim(make_params());
  sim.get_trace().load_data(0);

  [[maybe_unused]] job_no_t j0 = sim.append_job(0.0, 10, kTestQueueInput, 50);
  sim.advance_to(100.0);

  std::vector<Simulation::Job_Append_Request> past_reqs = {
      {150.0, 10, kTestQueueInput, 50},
      {50.0, 10, kTestQueueInput, 50}, // 50 < current_time (100)
  };
  [[maybe_unused]] bool threw = false;
  try {
    sim.append_jobs(past_reqs);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);
  assert(sim.get_trace().data().size() ==
         1); // only j0 - batch rejected entirely

  std::cout << "  PASSED" << std::endl;
}

// Test 7: reclaim-before-grow (established for the single-job path in
// test 2) holds the same way within a batch call - each request in the
// batch goes through the same per-job check-full()-reclaim-grow order,
// not a single capacity computation for the whole batch upfront.
void test_append_jobs_reclaims_before_growing() {
  std::cout << "\n=== Test 7: append_jobs() reclaims before growing, within a "
               "batch ==="
            << std::endl;

  Sim_Params params = make_params();
  params.set_outfile("/tmp/test_append_jobs_reclaim_out.csv");
  params.m_job_store_capacity = 1;
  params.m_job_store_overflow = CircularOverflowPolicy::GROW;

  Simulation sim(params);
  sim.get_trace().set_job_store_capacity(1);
  sim.get_trace().set_job_store_overflow(CircularOverflowPolicy::GROW);
  sim.get_trace().load_data(0);

  [[maybe_unused]] job_no_t f0 = sim.append_job(0.0, 10, kTestQueueInput, 50);
  sim.advance_to(60.0); // f0 finishes at t=50 - its slot is now reclaimable
  assert(sim.get_trace().data().capacity() == 1);

  // First request in the batch reclaims f0's slot (capacity stays 1);
  // the second can't be accommodated by reclaiming again (nothing
  // else is finished yet), so it grows the buffer - same order as a
  // lone append_job() call would follow, just executed twice in a
  // row within one append_jobs() call.
  std::vector<Simulation::Job_Append_Request> batch = {
      {60.0, 10, kTestQueueInput, 50},
      {65.0, 10, kTestQueueInput, 50},
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
  std::cout << "\n=== Test 8: append_jobs() abort-on-exhaustion leaves m_data "
               "untouched ==="
            << std::endl;

  Sim_Params params = make_params();
  params.m_job_store_capacity = 1;
  params.m_job_store_overflow = CircularOverflowPolicy::ABORT;

  Simulation sim(params);
  sim.get_trace().set_job_store_capacity(1);
  sim.get_trace().set_job_store_overflow(CircularOverflowPolicy::ABORT);
  sim.get_trace().load_data(0);

  [[maybe_unused]] job_no_t f0 = sim.append_job(0.0, 10, kTestQueueInput, 50);
  // Still running (hasn't reached end_time=50 yet) - not reclaimable.
  assert(sim.get_trace().data().size() == 1);

  std::vector<Simulation::Job_Append_Request> batch = {
      {10.0, 10, kTestQueueInput, 50},
      {20.0, 10, kTestQueueInput, 50},
  };
  [[maybe_unused]] bool threw = false;
  try {
    sim.append_jobs(batch);
  } catch (const std::runtime_error &) {
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
  std::cout << "\n=== Test 9: append_jobs() with an empty request vector ==="
            << std::endl;

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
  std::cout << "\n=== Test 10: Basic append_job() and run_until ==="
            << std::endl;

  Simulation sim(make_params());
  sim.get_trace().load_data(0);

  assert(sim.get_nodes_in_use() == 0);
  std::cout << "  Initial state: 0 nodes in use" << std::endl;

  [[maybe_unused]] job_no_t j0 = sim.append_job(0.0, 10, kTestQueueInput, 100);
  sim.advance_to(0.0);
  assert(sim.get_nodes_in_use() == 10);
  std::cout << "  After job 0 appended+submitted: 10 nodes in use" << std::endl;

  [[maybe_unused]] job_no_t j1 = sim.append_job(50.0, 20, kTestQueueInput, 100);
  sim.advance_to(50.0);
  assert(sim.get_nodes_in_use() == 30);
  std::cout << "  After job 1 appended+submitted: 30 nodes in use" << std::endl;

  sim.advance_to(100.0); // job 0 ends
  assert(sim.get_nodes_in_use() == 20);
  std::cout << "  After job 0 completes: 20 nodes in use" << std::endl;

  sim.advance_to(150.0); // job 1 ends
  assert(sim.get_nodes_in_use() == 0);
  std::cout << "  After all jobs complete: 0 nodes in use" << std::endl;

  std::cout << "  PASSED" << std::endl;
}

// Test 11: exclusive vs inclusive advance - run_until_exclusive() must
// not process an event exactly at its target time, advance_to() must.
void test_exclusive_vs_inclusive() {
  std::cout << "\n=== Test 11: Exclusive vs Inclusive run_until ==="
            << std::endl;

  Simulation sim(make_params());
  sim.get_trace().load_data(0);

  [[maybe_unused]] job_no_t j0 = sim.append_job(0.0, 10, kTestQueueInput, 10);

  // run_until_exclusive(0) must not process the START event at t=0.
  assert(sim.get_nodes_in_use() == 0);
  std::cout << "  run_until_exclusive(0): START not processed, 0 nodes"
            << std::endl;

  sim.advance_to(0.0);
  assert(sim.get_nodes_in_use() == 10);
  std::cout << "  advance_to(0): START processed, 10 nodes" << std::endl;

  // A second job that starts at t=2 and ends at t=5 lies strictly before
  // the target, so run_until_exclusive(10) must process both of its events
  // and leave the clock at t=5. A no-op implementation (which this once
  // was) would leave the clock at 0 and never run the job.
  [[maybe_unused]] job_no_t j1 = sim.append_job(2.0, 5, kTestQueueInput, 3);
  // Two jobs accepted, one started so far: the counters must differ here.
  assert(sim.get_statistics().jobs_submitted == 2);
  assert(sim.get_statistics().jobs_started == 1);
  sim.run_until_exclusive(10.0);
  assert(sim.get_current_time() == 5.0);
  assert(sim.get_nodes_in_use() == 10);
  assert(sim.get_statistics().jobs_completed == 1);
  std::cout << "  run_until_exclusive(10): events at 2 and 5 processed, END"
            << " at 10 not processed, still 10 nodes" << std::endl;

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
  struct Arrival {
    sim_time_t time;
    num_nodes_t num_nodes;
  };
  std::vector<Arrival> arrivals = {
      {0.0, 30}, {10.0, 20}, {20.0, 10}, {30.0, 40}, {40.0, 5}};

  size_t next_arrival = 0;
  sim_time_t current_time = 0.0;
  std::vector<job_no_t> running_jobs;

  while (next_arrival < arrivals.size() || !running_jobs.empty()) {
    while (next_arrival < arrivals.size() &&
           approx_equal(arrivals[next_arrival].time, current_time)) {
      const auto &arr = arrivals[next_arrival];
      num_nodes_t free_nodes = 100 - sim.get_nodes_in_use();

      if (arr.num_nodes <= free_nodes) {
        job_no_t job_idx =
            sim.append_job(current_time, arr.num_nodes, kTestQueueInput, 50);
        sim.advance_to(current_time);
        running_jobs.push_back(job_idx);
        std::cout << "    t=" << current_time << ": started job " << job_idx
                  << " (" << arr.num_nodes << " nodes, " << free_nodes
                  << " free)" << std::endl;
      } else {
        std::cout << "    t=" << current_time << ": arrival needing "
                  << arr.num_nodes << " nodes queued (" << free_nodes
                  << " free)" << std::endl;
      }
      next_arrival++;
    }

    current_time += 10.0;
    if (current_time > 100.0)
      break;

    sim.advance_to(current_time);

    auto it = running_jobs.begin();
    while (it != running_jobs.end()) {
      const auto &job = sim.get_trace().job_at(*it);
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
    [[maybe_unused]] job_no_t job_idx =
        sim.append_job(start_time, 10, kTestQueueInput, 20);
    sim.advance_to(start_time);
    std::cout << "    t=" << start_time << ": " << sim.get_nodes_in_use()
              << " nodes in use" << std::endl;
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
  std::cout << "\n=== Test 14: advance_to() Idle Gap Postcondition ==="
            << std::endl;

  Simulation sim(make_params());
  sim.get_trace().load_data(0);

  [[maybe_unused]] job_no_t j0 = sim.append_job(0.0, 30, kTestQueueInput, 50);
  [[maybe_unused]] job_no_t j1 = sim.append_job(10.0, 30, kTestQueueInput, 50);

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
  [[maybe_unused]] job_no_t j2 = sim.append_job(500.0, 30, kTestQueueInput, 50);
  sim.advance_to(500.0);
  assert(approx_equal(sim.get_current_time(), 500.0));
  assert(sim.get_nodes_in_use() == 30);
  std::cout << "  get_current_time() == 500, job 2 running (30 nodes)"
            << std::endl;

  sim.advance_to(std::numeric_limits<sim_time_t>::max());
  assert(sim.get_nodes_in_use() == 0);
  assert(approx_equal(sim.get_current_time(), 550.0));
  std::cout << "  All jobs complete after draining to infinity" << std::endl;

  std::cout << "  PASSED" << std::endl;
}

// Reservations anticipate scheduled capacity changes. A head that only fits
// after a scheduled increase is reserved at that increase, so a candidate
// that ends before it is backfilled now instead of being refused against a
// reservation the scheduler could never honor; a reduction that leaves the
// head unsatisfiable yields no reservation at all, so the queue behind it
// keeps moving.
void test_reservations_follow_capacity_schedule() {
  std::cout << "\n=== Reservations follow the capacity schedule ==="
            << std::endl;

  // 100 nodes; A holds 50 until t=100. Capacity drops to 55 at t=50 and
  // returns to 100 at t=150. H (60 nodes) therefore fits only at t=150:
  // t=50 balance 50-45=5, t=100 +50=55 < 60, t=150 +45=100. C (40 nodes,
  // 120 s) cannot run through the future 55-node capacity reduction, so it
  // remains queued until A finishes at t=100.
  {
    std::ofstream capacity(CAPACITY_OUTLOOK_INCREASE_PATH);
    capacity << "time,total_nodes\n"
             << "50,55\n"
             << "150,100\n";
  }
  {
    Sim_Params params = make_params();
    params.m_capacity_schedule = CAPACITY_OUTLOOK_INCREASE_PATH;
    Simulation sim(params);
    sim.get_trace().load_data(0);
    const auto a = sim.append_job(0.0, 50, kTestQueueInput, 100.0);
    const auto head = sim.append_job(0.0, 60, kTestQueueInput, 10.0);
    const auto candidate = sim.append_job(0.0, 40, kTestQueueInput, 120.0);
    sim.advance_to(0.0);
    assert(approx_equal(sim.get_fcfs_head_shadow_time(), 150.0));
    const auto statuses = sim.get_job_statuses({a, head, candidate});
    assert(statuses[0].state == Simulation::Job_State::RUNNING);
    assert(statuses[1].state == Simulation::Job_State::PENDING);
    assert(statuses[1].expected_start_time.has_value());
    assert(approx_equal(*statuses[1].expected_start_time, 150.0));
    assert(statuses[2].state == Simulation::Job_State::PENDING);

    // No new job crosses the capacity reduction, and H still starts exactly
    // at the increase.
    sim.advance_to(75.0);
    assert(sim.get_current_capacity() == 55);
    assert(sim.get_nodes_in_use() == 50);
    assert(approx_equal(sim.get_fcfs_head_shadow_time(), 150.0));
    sim.advance_to(std::numeric_limits<sim_time_t>::max());
    const auto final_status = sim.get_job_statuses({head}).front();
    assert(approx_equal(*final_status.start_time, 150.0));
    assert(sim.get_statistics().jobs_completed == 3);
  }

  // Capacity drops to 30 at t=10 and never recovers. H (60 nodes) can never
  // start: t=10 balance 50-70=-20, t=100 +50=30 < 60. The head holds no
  // reservation, so C (20 nodes, 5 s) is held in both modes: an unavailable
  // head start is not an unprotected backfill window.
  // Draining with a job that can never start must end in both modes instead
  // of looping on the max() sentinel.
  {
    std::ofstream capacity(CAPACITY_OUTLOOK_DROP_PATH);
    capacity << "time,total_nodes\n"
             << "10,30\n";
  }
  for (const double interval : {0.0, 7.0}) {
    Sim_Params params = make_params();
    params.m_capacity_schedule = CAPACITY_OUTLOOK_DROP_PATH;
    params.m_backfill_interval = interval;
    Simulation sim(params);
    sim.get_trace().load_data(0);
    (void)sim.append_job(0.0, 50, kTestQueueInput, 100.0);
    const auto head = sim.append_job(0.0, 60, kTestQueueInput, 10.0);
    const auto candidate = sim.append_job(0.0, 20, kTestQueueInput, 5.0);
    sim.advance_to(0.0);
    assert(approx_equal(sim.get_fcfs_head_shadow_time(), -1.0));
    assert(sim.get_backfill_window().shadow_time == -1.0);
    const auto statuses = sim.get_job_statuses({head});
    assert(statuses[0].state == Simulation::Job_State::PENDING);
    assert(!statuses[0].expected_start_time.has_value());
    sim.advance_to(8.0);
    const auto started = sim.get_job_statuses({candidate}).front();
    assert(!started.start_time.has_value());
    sim.advance_to(std::numeric_limits<sim_time_t>::max());
    assert(sim.get_statistics().jobs_completed == 1);
    assert(sim.get_active_job_count() == 2);
    assert(sim.get_nodes_in_use() == 0);
    assert(approx_equal(sim.get_current_time(), 100.0)); // last real event
  }

  std::cout << "  PASSED" << std::endl;
}

// Event-driven planning horizon (Slurm bf_window): a head whose reservation
// starts beyond the horizon is not planned, so a candidate that would run
// past that reservation backfills now; the head keeps its queue position and
// the shadow time still reports the projection.
void test_planning_horizon_event_driven() {
  std::cout << "\n=== Event-driven planning horizon ===" << std::endl;

  struct Case {
    BackfillPolicy policy;
    size_t depth;
    size_t queue_depth;
    PriorityPolicy priority;
    const char *name;
  };
  const Case cases[] = {
      {BackfillPolicy::EASY, 0, 0, PriorityPolicy::FCFS, "easy"},
      // A one-job scan depth proves that the out-of-horizon head does not
      // consume the only slot needed by C.
      {BackfillPolicy::EASY, 0, 1, PriorityPolicy::FCFS, "easy, depth 1"},
      {BackfillPolicy::HYBRID, 1, 1, PriorityPolicy::FCFS, "hybrid"},
      {BackfillPolicy::CONSERVATIVE, 0, 1, PriorityPolicy::FCFS,
       "conservative"},
      {BackfillPolicy::EASY, 0, 0, PriorityPolicy::SJF, "sjf"},
      {BackfillPolicy::EASY, 0, 1, PriorityPolicy::SJF, "sjf, depth 1"},
  };
  for (const auto &c : cases) {
    for (const double horizon :
         {std::numeric_limits<tdiff_t>::max(), tdiff_t{100.0}}) {
      Sim_Params params = make_params();
      params.m_backfill_policy = c.policy;
      params.m_reservation_depth = c.depth;
      params.m_backfill_queue_depth = c.queue_depth;
      params.m_priority_policy = c.priority;
      params.m_planning_horizon = horizon;
      Simulation sim(params);
      sim.get_trace().load_data(0);
      // A holds 90 nodes until t=1000; H (100 nodes) is reserved at 1000;
      // C (10 nodes, 2000 s) fits now but would end after the reservation.
      (void)sim.append_job(0.0, 90, kTestQueueInput, 1000.0);
      // Start A before H and C enter the queue.  This is essential for the
      // SJF variant, which would otherwise correctly choose H's short limit
      // ahead of A and eliminate the blocked-head scenario.
      sim.advance_to(0.0);
      const auto head = sim.append_job(0.0, 100, kTestQueueInput, 10.0);
      const auto candidate = sim.append_job(0.0, 10, kTestQueueInput, 2000.0);
      sim.advance_to(0.0);
      const auto statuses = sim.get_job_statuses({head, candidate});
      const bool bounded = horizon < 1000.0;
      std::cout << "  " << c.name
                << (bounded ? " (horizon 100)" : " (unlimited)") << std::endl;
      assert(statuses[0].state == Simulation::Job_State::PENDING);
      if (bounded) {
        // Unplanned head: C backfills past it and the projection moves.
        assert(statuses[1].state == Simulation::Job_State::RUNNING);
        assert(approx_equal(*statuses[1].start_time, 0.0));
        assert(approx_equal(sim.get_fcfs_head_shadow_time(), 2000.0));
      } else {
        assert(statuses[1].state == Simulation::Job_State::PENDING);
        assert(statuses[0].start_time_kind ==
               Simulation::Start_Time_Kind::RESERVED);
        assert(approx_equal(*statuses[0].expected_start_time, 1000.0));
        assert(approx_equal(sim.get_fcfs_head_shadow_time(), 1000.0));
      }
      sim.advance_to(std::numeric_limits<sim_time_t>::max());
      const auto final_head = sim.get_job_statuses({head}).front();
      assert(approx_equal(*final_head.start_time, bounded ? 2000.0 : 1000.0));
      assert(sim.get_statistics().jobs_completed == 3);
    }
  }

  std::cout << "  PASSED" << std::endl;
}

// Periodic backfill is one cycle per tick, as in Slurm: FCFS dispatch stays
// event-driven and never scans for backfill, and every backfill pass at a
// tick shares one queue-depth budget that resets at the next tick. With depth
// 3, H (the blocked head), C, and D use the budget at the tick at t=5 and D
// starts. The repeated pass at the same tick must not start E from a reset
// budget; E waits for FCFS dispatch at t=20. EASY and hybrid with one
// reservation behave identically.
void test_periodic_backfill_queue_depth_is_one_cycle_per_tick() {
  std::cout << "\n=== Periodic backfill queue depth: one budget per tick ==="
            << std::endl;

  for (const auto policy : {BackfillPolicy::EASY, BackfillPolicy::HYBRID}) {
    auto params = make_params();
    params.m_total_nodes = 3;
    params.m_queue_impl = QueueImplementation::CIRCULAR;
    params.m_backfill_policy = policy;
    params.m_reservation_depth = 1;
    params.m_backfill_interval = 5.0;
    params.m_backfill_queue_depth = 3;
    Simulation sim(params);
    sim.get_trace().load_data(0);

    // A leaves two free nodes until t=10, blocking the three-node head H.
    (void)sim.append_job(0.0, 1, kTestQueueInput, 10.0);
    sim.advance_to(0.1);
    (void)sim.append_job(0.1, 3, kTestQueueInput, 10.0); // H
    // C ends after H's slot, so it cannot backfill.
    (void)sim.append_job(0.1, 2, kTestQueueInput, 20.0);
    const auto d = sim.append_job(0.1, 1, kTestQueueInput, 1.0); // D: backfills
    const auto e = sim.append_job(0.1, 1, kTestQueueInput, 1.0); // E: next tick

    // No backfill happens before the first tick.
    sim.advance_to(4.9);
    assert(sim.get_job_statuses({d}).front().state ==
           Simulation::Job_State::PENDING);

    sim.advance_to(5.0);
    const auto d_status = sim.get_job_statuses({d}).front();
    assert(d_status.start_time.has_value());
    assert(approx_equal(*d_status.start_time, 5.0));
    assert(sim.get_job_statuses({e}).front().state ==
           Simulation::Job_State::PENDING);

    // A ends at t=10 and H starts by FCFS dispatch; the next tick at t=10
    // finds no free node, so E starts only when H ends at t=20.
    sim.advance_to(std::numeric_limits<sim_time_t>::max());
    const auto e_status = sim.get_job_statuses({e}).front();
    assert(e_status.start_time.has_value());
    assert(approx_equal(*e_status.start_time, 20.0));
  }

  std::cout << "  PASSED" << std::endl;
}

// A job whose reservation lies beyond the planning horizon holds no
// reservation in either mode. Whether it also uses a queue-depth slot follows
// the system being reproduced. Event-driven scheduling follows Fluxion, which
// takes an unreservable job out of its scheduled count, so the small job
// behind it is examined at once. Periodic scheduling follows Slurm, which
// counts every tested job toward bf_max_job_test before it learns the planned
// start is beyond bf_window, so the blocked head and the second job spend the
// depth of 2 and the small job is never examined.
void test_horizon_skipped_jobs_and_queue_depth() {
  std::cout << "\n=== Out-of-horizon jobs and the queue depth ===" << std::endl;

  for (const double interval : {0.0, 10.0}) {
    auto params = make_params();
    params.m_total_nodes = 10;
    params.m_queue_impl = QueueImplementation::CIRCULAR;
    params.m_backfill_policy = BackfillPolicy::HYBRID;
    params.m_reservation_depth = 1;
    params.m_backfill_queue_depth = 2;
    params.m_planning_horizon = 50.0;
    params.m_backfill_interval = interval;
    Simulation sim(params);
    sim.get_trace().load_data(0);

    // A leaves four nodes free until t=1000, so every blocked job's
    // reservation is beyond the 50 s horizon.
    (void)sim.append_job(0.0, 6, kTestQueueInput, 1000.0);
    sim.advance_to(0.1);
    (void)sim.append_job(0.1, 10, kTestQueueInput, 10.0); // H: blocked head
    (void)sim.append_job(0.1, 8, kTestQueueInput, 5.0);   // C1: cannot fit
    const auto c2 = sim.append_job(0.1, 2, kTestQueueInput, 5.0); // C2: fits
    sim.advance_to(25.0);

    const auto status = sim.get_job_statuses({c2}).front();
    if (interval == 0.0) {
      assert(status.start_time.has_value());
      assert(approx_equal(*status.start_time, 0.1));
    } else {
      assert(status.state == Simulation::Job_State::PENDING);
    }
  }

  std::cout << "  PASSED" << std::endl;
}

// FCFS_CONSERVATIVE ordering with the EASY policy is served by the FCFS EASY
// schedulers, because the hybrid classes implement conservative and hybrid
// reservations only. The head is protected and a short job backfills.
void test_fcfs_conservative_ordering_with_easy() {
  std::cout << "\n=== fcfs_conservative ordering with EASY ===" << std::endl;

  for (const auto queue_impl :
       {QueueImplementation::CIRCULAR, QueueImplementation::DEQUE}) {
    auto params = make_params();
    params.m_queue_impl = queue_impl;
    params.m_priority_policy = PriorityPolicy::FCFS_CONSERVATIVE;
    params.m_backfill_policy = BackfillPolicy::EASY;
    Simulation sim(params);
    sim.get_trace().load_data(0);
    (void)sim.append_job(0.0, 90, kTestQueueInput, 100.0);
    sim.advance_to(0.1);
    const auto head = sim.append_job(0.1, 100, kTestQueueInput, 10.0);
    const auto shorter = sim.append_job(0.1, 10, kTestQueueInput, 50.0);
    const auto longer = sim.append_job(0.1, 10, kTestQueueInput, 500.0);
    sim.advance_to(0.1);

    assert(sim.get_job_statuses({head}).front().state ==
           Simulation::Job_State::PENDING);
    assert(sim.get_job_statuses({shorter}).front().state ==
           Simulation::Job_State::RUNNING);
    assert(sim.get_job_statuses({longer}).front().state ==
           Simulation::Job_State::PENDING); // would delay the head
    assert(approx_equal(sim.get_fcfs_head_shadow_time(), 100.0));
  }

  std::cout << "  PASSED" << std::endl;
}

// Repeated scheduler passes at one event timestamp must share the configured
// queue-depth budget. Here D starts in the first pass after H and C consume
// the three slots; the old per-pass accounting then reset and incorrectly
// started E at t=0.1 in the next pass. E is reconsidered after D completes at
// t=1.1, when the timestamp (and therefore the depth budget) changes.
void test_backfill_queue_depth_is_shared_per_timestamp() {
  std::cout << "\n=== Backfill queue depth: one budget per timestamp ==="
            << std::endl;

  for (const auto queue_impl :
       {QueueImplementation::CIRCULAR, QueueImplementation::DEQUE}) {
    for (const auto policy :
         {BackfillPolicy::CONSERVATIVE, BackfillPolicy::HYBRID}) {
      auto params = make_params();
      params.m_total_nodes = 3;
      params.m_queue_impl = queue_impl;
      params.m_priority_policy = PriorityPolicy::FCFS_CONSERVATIVE;
      params.m_backfill_policy = policy;
      params.m_reservation_depth = 1;
      params.m_backfill_queue_depth = 3;
      Simulation sim(params);
      sim.get_trace().load_data(0);

      // A leaves two free nodes until t=10, blocking the three-node head H.
      (void)sim.append_job(0.0, 1, kTestQueueInput, 10.0);
      // Move past A's START event so it is represented in the scheduler's
      // running-job timeline before H's reservation is projected.
      sim.advance_to(0.1);
      (void)sim.append_job(0.1, 3, kTestQueueInput, 10.0); // H
      (void)sim.append_job(0.1, 2, kTestQueueInput, 20.0); // C: cannot fit
      (void)sim.append_job(0.1, 1, kTestQueueInput, 1.0);  // D: starts now
      const auto e = sim.append_job(0.1, 1, kTestQueueInput, 1.0);

      sim.advance_to(0.1);
      const auto at_first_timestamp = sim.get_job_statuses({e}).front();
      assert(at_first_timestamp.state == Simulation::Job_State::PENDING);

      sim.advance_to(1.1);
      const auto at_next_timestamp = sim.get_job_statuses({e}).front();
      assert(at_next_timestamp.state == Simulation::Job_State::RUNNING);
      assert(at_next_timestamp.start_time.has_value());
      assert(approx_equal(*at_next_timestamp.start_time, 1.1));
    }
  }

  std::cout << "  PASSED" << std::endl;
}

// A finite streaming advance must consume capacity changes even if no jobs
// exist yet. Capacity zero then pauses a newly appended job until the increase
// at t=20. Once that job finishes, draining an otherwise empty simulation to
// infinity must not consume the irrelevant change at t=1000.
void test_capacity_schedule_across_streaming_advances() {
  std::cout << "\n=== Capacity schedule across streaming advances ==="
            << std::endl;

  Sim_Params params = make_params();
  params.m_capacity_schedule = CAPACITY_SCHEDULE_PATH;
  Simulation sim(params);
  sim.get_trace().load_data(0);

  sim.advance_to(10.0);
  assert(approx_equal(sim.get_current_time(), 10.0));
  assert(sim.get_current_capacity() == 0);
  assert(sim.get_available_nodes() == 0);

  const job_no_t job = sim.append_job(10.0, 30, kTestQueueInput, 10);
  sim.advance_to(15.0);
  assert(!sim.get_trace().job_at(job).is_scheduled());
  assert(sim.get_nodes_in_use() == 0);

  sim.advance_to(20.0);
  assert(sim.get_current_capacity() == 100);
  assert(sim.get_trace().job_at(job).is_scheduled());
  assert(sim.get_nodes_in_use() == 30);

  sim.advance_to(30.0);
  assert(sim.get_nodes_in_use() == 0);
  sim.advance_to(std::numeric_limits<sim_time_t>::max());
  assert(sim.get_current_capacity() == 100);

  // A finite advance on another idle simulation does consume the transition.
  // Reinitializing that same Simulation must reset both Simulation's capacity
  // and Trace's capacity used to derive free_nodes in resource samples.
  Simulation reused(params);
  reused.get_trace().load_data(0);
  reused.advance_to(1000.0);
  assert(reused.get_current_capacity() == 25);
  reused.initialize_trace();
  assert(reused.get_current_capacity() == 100);
  const job_no_t reset_job = reused.append_job(0.0, 30, kTestQueueInput, 10);
  reused.advance_to(0.0);
  assert(reused.get_trace().job_at(reset_job).is_scheduled());
  reused.write_resource_trace(CAPACITY_RESET_TRACE_PATH);

  std::ifstream resource_trace(CAPACITY_RESET_TRACE_PATH);
  std::string line;
  std::string last_line;
  while (std::getline(resource_trace, line)) {
    if (!line.empty()) {
      last_line = line;
    }
  }
  assert(last_line == "0,70,30");

  // A non-preemptive reduction below live occupancy uses that occupancy as
  // effective capacity until the running work drains. Down nodes therefore
  // do not dilute either instantaneous or time-accounted utilization.
  Simulation draining(params);
  draining.get_trace().load_data(0);
  draining.append_job(0.0, 30, kTestQueueInput, 10);
  draining.advance_to(5.0);
  assert(draining.get_current_capacity() == 0);
  assert(draining.get_nodes_in_use() == 30);
  assert(approx_equal(draining.get_current_utilization(), 1.0));
  const auto draining_stats = draining.get_statistics();
  assert(approx_equal(draining_stats.resource_area, 300.0));
  assert(approx_equal(draining_stats.utilization, 300.0 / 650.0));

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
               "reclaim_front_jobs()'s own fallback ==="
            << std::endl;

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

    [[maybe_unused]] job_no_t f0 = sim.append_job(0.0, 10, kTestQueueInput, 50);
    sim.advance_to(60.0);
    assert(sim.get_trace().data().capacity() == 1);

    std::vector<Simulation::Job_Append_Request> batch;
    for (int i = 0; i < 5; ++i)
      batch.push_back({60.0 + i, 10, kTestQueueInput, 50});
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

    [[maybe_unused]] job_no_t f0 = sim.append_job(0.0, 10, kTestQueueInput, 50);
    sim.advance_to(60.0);

    std::vector<Simulation::Job_Append_Request> batch;
    for (int i = 0; i < 5; ++i)
      batch.push_back({60.0 + i, 10, kTestQueueInput, 50});

    [[maybe_unused]] bool threw = false;
    try {
      sim.append_jobs(batch);
    } catch (const std::runtime_error &e) {
      threw = true;
      std::string msg = e.what();
      assert(msg.find("batch of 5 requests") != std::string::npos);
      assert(msg.find("front job isn't safe to reclaim yet") ==
             std::string::npos);
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
  std::cout
      << "\n=== Test 16: submit_job() records busy_nodes for streaming jobs ==="
      << std::endl;

  Simulation sim(make_params());
  sim.get_trace().load_data(0);

  // job0 arrives alone - nothing else running yet, busy_nodes must be 0.
  job_no_t j0 = sim.append_job(0.0, 30, kTestQueueInput, 100);
  sim.advance_to(0.0);
  assert(sim.get_nodes_in_use() == 30);

  // job1 arrives at t=10, while job0's 30 nodes are still in use -
  // busy_nodes for job1 must reflect that occupancy (30), not 0.
  job_no_t j1 = sim.append_job(10.0, 20, kTestQueueInput, 100);

  [[maybe_unused]] const auto &job0 = sim.get_trace().job_at(j0);
  [[maybe_unused]] const auto &job1 = sim.get_trace().job_at(j1);
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
  std::cout << "\n=== Test 17: append_jobs() honors --check_memory_pressure ==="
            << std::endl;

  setenv("DR_EVT_TEST_AVAILABLE_MEMORY_BYTES", "10", 1);

  Simulation sim(make_params());
  sim.get_trace().set_memory_pressure_fraction(0.8);
  sim.get_trace().load_data(0);

  std::vector<Simulation::Job_Append_Request> batch = {
      {10.0, 20, kTestQueueInput, 200.0},
      {15.0, 10, kTestQueueInput, 100.0},
  };
  [[maybe_unused]] bool threw = false;
  try {
    sim.append_jobs(batch);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  unsetenv("DR_EVT_TEST_AVAILABLE_MEMORY_BYTES");
  std::cout << "  PASSED" << std::endl;
}

// Test 18: Custom-FCFS resource area weights each settled allocation by its
// duration and combines multiple allocations and releases at one timestamp.
void test_resource_area_and_time_accounted_utilization() {
  std::cout << "\n=== Test 18: time-accounted resource area ===" << std::endl;

  auto params = make_params();
  Simulation sim(
      params,
      [](job_no_t job_id, sim_time_t, tdiff_t, num_nodes_t) {
        return static_cast<job_cost_t>(job_id);
      },
      [](const backfill_candidates_t &candidates) -> std::optional<size_t> {
        return candidates.empty()
                   ? std::nullopt
                   : std::optional<size_t>{0};
      });
  sim.get_trace().load_data(0);

  sim.append_job(0.0, 20, kTestQueueInput, 10.0);
  sim.append_job(0.0, 30, kTestQueueInput, 10.0);
  sim.advance_to(0.0);
  assert(approx_equal(sim.get_resource_area(), 0.0));

  // At t=10 two jobs return 20+30 nodes and two jobs start using 15+26.
  // All four events are settled before accounting stores the next interval's
  // state, so the allocation changes from 50 to 41 without assigning any
  // positive-duration interval to an intermediate same-time state.
  sim.append_job(10.0, 15, kTestQueueInput, 100.0);
  sim.append_job(10.0, 26, kTestQueueInput, 100.0);
  sim.advance_to(10.0);
  assert(approx_equal(sim.get_resource_area(), 50.0 * 10.0));

  sim.advance_to(60.0); // Include the still-running job's partial interval.
  assert(approx_equal(sim.get_resource_area(), 50.0 * 10.0 + 41.0 * 50.0));

  auto partial = sim.get_statistics();
  assert(approx_equal(partial.resource_area, 2550.0));
  assert(approx_equal(partial.utilization, 2550.0 / (100.0 * 60.0)));

  sim.advance_to(110.0);
  auto complete = sim.get_statistics();
  assert(approx_equal(complete.resource_area, 50.0 * 10.0 + 41.0 * 100.0));
  assert(approx_equal(complete.utilization, 4600.0 / (100.0 * 110.0)));

  // Standard schedulers do not pay for or expose live area accounting.
  auto standard_params = make_params();
  Simulation standard(standard_params);
  standard.get_trace().load_data(0);
  bool rejected_for_standard_scheduler = false;
  try {
    (void)standard.get_resource_area();
  } catch (const std::logic_error &) {
    rejected_for_standard_scheduler = true;
  }
  assert(rejected_for_standard_scheduler);

  std::cout << "  PASSED" << std::endl;
}

// Test 19: prediction horizon integrates utilization-adjusted available
// capacity from the FCFS shadow time and excludes not-yet-arrived jobs.
void test_prediction_horizon() {
  std::cout << "\n=== Test 19: prediction horizon ===" << std::endl;

  auto params = make_params();
  Simulation sim(
      params,
      [](job_no_t job_id, sim_time_t, tdiff_t, num_nodes_t) {
        return static_cast<job_cost_t>(job_id);
      },
      [](const backfill_candidates_t &candidates) -> std::optional<size_t> {
        return candidates.empty()
                   ? std::nullopt
                   : std::optional<size_t>{0};
      });
  sim.get_trace().load_data(0);

  // Four jobs enter the running set and occupy 95 of the 100 nodes. Their
  // predicted completion times are 40, 60, 80, and 100 seconds.
  for (const auto &[nodes, runtime] :
       std::vector<std::pair<num_nodes_t, tdiff_t>>{
           {50, 40.0}, {20, 60.0}, {15, 80.0}, {10, 100.0}}) {
    sim.append_job(0.0, nodes, kTestQueueInput, runtime);
    sim.advance_to(0.0);
  }

  // Four more jobs remain waiting because none fits in the five free nodes.
  // Their total resource-time demand is
  // A_Q = 50*8 + 20*15 + 15*10 + 10*15 = 1000 node-seconds.
  for (const auto &[nodes, runtime] :
       std::vector<std::pair<num_nodes_t, tdiff_t>>{
           {50, 8.0}, {20, 15.0}, {15, 10.0}, {10, 15.0}}) {
    sim.append_job(0.0, nodes, kTestQueueInput, runtime);
    sim.advance_to(0.0);
  }

  // This demand arrives in the future and must not be included in A_Q(0).
  sim.append_job(10.0, 100, kTestQueueInput, 100.0);

  assert(sim.get_active_job_count() == 4);
  assert(approx_equal(sim.get_fcfs_head_shadow_time(), 40.0));

  // At the shadow time, 50 nodes have just been released, so 55 nodes are
  // available. The remaining completion events produce these nominal areas:
  //   [40, 60]: 55*20 = 1100
  //   [60, 80]: 75*20 = 1500
  //   [80,100]: 90*20 = 1800
  // Varying U therefore places the horizon at each successive event or after
  // the replay, covering every result path in the estimator.
  assert(approx_equal(sim.get_prediction_horizon(1.0), 20.0));
  assert(approx_equal(sim.get_prediction_horizon(0.5), 40.0));
  assert(approx_equal(sim.get_prediction_horizon(0.25), 60.0));

  // U=0 is the documented sentinel for the U=1 fallback.
  assert(approx_equal(sim.get_prediction_horizon(0.0), 20.0));

  // At U=0.1, the replay supplies 110+150+180=440 node-seconds. The remaining
  // 560 node-seconds use all 100 nodes at effective rate 0.1, adding 56
  // seconds after t=100: H=(100-40)+56=116 seconds.
  assert(approx_equal(sim.get_prediction_horizon(0.1), 116.0));

  for (double invalid_utilization :
       {-0.01, 1.01, std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()}) {
    bool threw = false;
    try {
      (void)sim.get_prediction_horizon(invalid_utilization);
    } catch (const std::invalid_argument &) {
      threw = true;
    }
    assert(threw);
  }

  auto empty_params = make_params();
  Simulation empty(
      empty_params,
      [](job_no_t job_id, sim_time_t, tdiff_t, num_nodes_t) {
        return static_cast<job_cost_t>(job_id);
      },
      [](const backfill_candidates_t &candidates) -> std::optional<size_t> {
        return candidates.empty()
                   ? std::nullopt
                   : std::optional<size_t>{0};
      });
  empty.get_trace().load_data(0);
  assert(approx_equal(empty.get_prediction_horizon(0.5), 0.0));

  // Every standard FCFS queue computes the same demand by scanning only its
  // existing eligible entries when this query is made.
  for (const auto queue_impl :
       {QueueImplementation::CIRCULAR, QueueImplementation::DEQUE,
        QueueImplementation::MULTIMAP, QueueImplementation::BLOCK}) {
    auto standard_params = make_params();
    standard_params.m_queue_impl = queue_impl;
    Simulation standard(standard_params);
    standard.get_trace().load_data(0);
    standard.append_job(0.0, 100, kTestQueueInput, 40.0);
    standard.advance_to(0.0);
    standard.append_job(0.0, 50, kTestQueueInput, 8.0);
    standard.advance_to(0.0);
    assert(approx_equal(standard.get_fcfs_head_shadow_time(), 40.0));
    assert(approx_equal(standard.get_prediction_horizon(0.5), 8.0));
    const auto starts = standard.get_queue_drain_start_times(1.0, 1);
    assert(starts.size() == 1);
    assert(approx_equal(starts[0].expected_start_time, 40.0));
    assert(starts[0].start_time_kind == Simulation::Start_Time_Kind::RESERVED);
  }

  auto unsupported_params = make_params();
  unsupported_params.m_priority_policy = PriorityPolicy::SJF;
  Simulation unsupported(unsupported_params);
  unsupported.get_trace().load_data(0);
  bool rejected_for_unsupported_scheduler = false;
  try {
    (void)unsupported.get_prediction_horizon(0.5);
  } catch (const std::logic_error &) {
    rejected_for_unsupported_scheduler = true;
  }
  assert(rejected_for_unsupported_scheduler);

  auto no_backfill_params = make_params();
  no_backfill_params.m_backfill_policy = BackfillPolicy::NONE;
  Simulation no_backfill(
      no_backfill_params,
      [](job_no_t job_id, sim_time_t, tdiff_t, num_nodes_t) {
        return static_cast<job_cost_t>(job_id);
      },
      [](const backfill_candidates_t &candidates) -> std::optional<size_t> {
        return candidates.empty()
                   ? std::nullopt
                   : std::optional<size_t>{0};
      });
  no_backfill.get_trace().load_data(0);
  bool rejected_without_easy = false;
  try {
    (void)no_backfill.get_prediction_horizon(0.5);
  } catch (const std::logic_error &) {
    rejected_without_easy = true;
  }
  assert(rejected_without_easy);

  std::cout << "  PASSED" << std::endl;
}

// A periodic tick that passes while nothing is waiting must not gate later
// events: completions still happen, a drain still finishes, the clock never
// moves backward, and a job never starts before its own submission.
void test_periodic_backfill_idle_ticks() {
  std::cout << "\n=== Periodic backfill across idle ticks ===" << std::endl;

  auto params = make_params();
  params.m_backfill_policy = BackfillPolicy::HYBRID;
  params.m_reservation_depth = 1;
  params.m_backfill_interval = 10.0;

  // One job, nothing waiting, drained in one call: the end at t=30 must be
  // processed although the tick at t=10 was never consumed.
  {
    Simulation sim(params);
    sim.get_trace().load_data(0);
    (void)sim.append_job(0.0, 90, kTestQueueInput, 30.0);
    sim.advance_to(std::numeric_limits<sim_time_t>::max());
    assert(sim.get_statistics().jobs_completed == 1);
    assert(sim.get_nodes_in_use() == 0);
    assert(sim.get_trace().pending_events().empty());
    assert(approx_equal(sim.get_current_time(), 30.0));
  }

  // The same job advanced in steps that straddle the idle tick.
  {
    Simulation sim(params);
    sim.get_trace().load_data(0);
    (void)sim.append_job(0.0, 90, kTestQueueInput, 30.0);
    sim.advance_to(15.0);
    assert(sim.get_nodes_in_use() == 90);
    sim.advance_to(40.0);
    assert(sim.get_statistics().jobs_completed == 1);
    assert(sim.get_nodes_in_use() == 0);
    assert(approx_equal(sim.get_current_time(), 40.0));

    // A job appended now starts now, not at a stale tick in the past, and
    // the clock only moves forward.
    const auto later = sim.append_job(40.0, 50, kTestQueueInput, 5.0);
    sim.advance_to(41.0);
    assert(approx_equal(sim.get_current_time(), 41.0));
    const auto status = sim.get_job_statuses({later}).front();
    assert(status.start_time.has_value());
    assert(approx_equal(*status.start_time, 40.0));
    sim.advance_to(std::numeric_limits<sim_time_t>::max());
    assert(sim.get_statistics().jobs_completed == 2);
    assert(sim.get_trace().pending_events().empty());
    assert(approx_equal(sim.get_current_time(), 45.0));
  }

  // Backfill still waits for ticks once a job is blocked: after the idle
  // stretch, a blocked head and a candidate arriving between ticks see the
  // candidate start at the next tick, with the cadence kept from t=0.
  {
    Simulation sim(params);
    sim.get_trace().load_data(0);
    (void)sim.append_job(0.0, 90, kTestQueueInput, 30.0);
    sim.advance_to(15.0);
    (void)sim.append_job(15.0, 100, kTestQueueInput, 1.0); // blocked head
    const auto candidate = sim.append_job(15.0, 10, kTestQueueInput, 1.0);
    sim.advance_to(25.0);
    const auto status = sim.get_job_statuses({candidate}).front();
    assert(status.start_time.has_value());
    assert(approx_equal(*status.start_time, 20.0));
  }

  // The same scenario in a single call, as a batch run sees it: the tick at
  // t=10 passes while nothing waits, and the arrivals at t=15 must not
  // consume that stale tick as a backfill pass. The candidate waits for t=20.
  {
    Simulation sim(params);
    sim.get_trace().load_data(0);
    (void)sim.append_job(0.0, 90, kTestQueueInput, 30.0);
    (void)sim.append_job(15.0, 100, kTestQueueInput, 1.0); // blocked head
    const auto candidate = sim.append_job(15.0, 10, kTestQueueInput, 1.0);
    sim.advance_to(25.0);
    const auto status = sim.get_job_statuses({candidate}).front();
    assert(status.start_time.has_value());
    assert(approx_equal(*status.start_time, 20.0));
  }

  std::cout << "  PASSED" << std::endl;
}

// run_until_exclusive() must step through backfill ticks like advance_to():
// a waiting candidate is backfilled at the tick before the target, and the
// clock ends at the last tick processed, not at the last ordinary event.
void test_periodic_backfill_run_until_exclusive() {
  std::cout << "\n=== Periodic backfill under run_until_exclusive ==="
            << std::endl;

  auto params = make_params();
  params.m_backfill_policy = BackfillPolicy::HYBRID;
  params.m_reservation_depth = 1;
  params.m_backfill_interval = 10.0;

  Simulation sim(params);
  sim.get_trace().load_data(0);
  (void)sim.append_job(0.0, 90, kTestQueueInput, 30.0);
  (void)sim.append_job(5.0, 100, kTestQueueInput, 1.0); // blocked head
  const auto candidate = sim.append_job(5.0, 10, kTestQueueInput, 1.0);

  // Events strictly before 25: start at 0, arrivals at 5, tick at 10
  // (candidate backfilled), its end at 11, tick at 20 (head still blocked).
  sim.run_until_exclusive(25.0);
  const auto status = sim.get_job_statuses({candidate}).front();
  assert(status.start_time.has_value());
  assert(approx_equal(*status.start_time, 10.0));
  assert(sim.get_statistics().jobs_completed == 1);
  assert(approx_equal(sim.get_current_time(), 20.0));

  // The tick at exactly the target is excluded, like any other event.
  sim.run_until_exclusive(30.0);
  assert(approx_equal(sim.get_current_time(), 20.0));
  sim.advance_to(30.0);
  assert(sim.get_statistics().jobs_completed == 2);
  assert(sim.get_nodes_in_use() == 100); // head started when A ended at 30

  std::cout << "  PASSED" << std::endl;
}

// Periodic EASY uses its dedicated scheduler instead of translating the
// policy to hybrid depth one. Its observable schedule remains equivalent to
// that hybrid configuration, which the periodic tests below cover.
void test_periodic_easy_scheduler_selection() {
  std::cout << "\n=== Periodic EASY scheduler selection ===" << std::endl;

  const auto scheduler = create_scheduler(
      100, 0, BackfillPolicy::EASY, PriorityPolicy::FCFS,
      QueueImplementation::CIRCULAR, 128, 0,
      CircularOverflowPolicy::GROW, 0, 0, 10.0);
  assert(dynamic_cast<FCFSPeriodicEasyCircularScheduler *>(scheduler.get()) !=
         nullptr);
  assert(dynamic_cast<FCFSPeriodicHybridCircularScheduler *>(scheduler.get()) ==
         nullptr);

  std::cout << "  PASSED" << std::endl;
}

// Periodic hybrid evaluation retains event-driven FCFS dispatch, but reserves
// backfill scans for ticks. A candidate that arrives between ticks therefore
// waits for the first tick even though it fits before the blocked head.
void test_periodic_hybrid_backfill() {
  std::cout << "\n=== Periodic hybrid backfill ===" << std::endl;

  auto params = make_params();
  params.m_backfill_policy = BackfillPolicy::HYBRID;
  params.m_reservation_depth = 1;
  params.m_backfill_interval = 2.7;
  Simulation sim(params);
  sim.get_trace().load_data(0);

  (void)sim.append_job(0.0, 90, kTestQueueInput, 10.0);
  sim.advance_to(0.0);
  const auto blocked = sim.append_job(0.0, 100, kTestQueueInput, 1.0);
  sim.advance_to(0.0);
  const auto backfill_1 = sim.append_job(0.0, 10, kTestQueueInput, 1.0);
  const auto backfill_2 = sim.append_job(0.0, 10, kTestQueueInput, 1.0);
  const auto backfill_3 = sim.append_job(0.0, 10, kTestQueueInput, 1.0);

  // Each 10-node candidate occupies the only free 10 nodes for one second.
  // Its end triggers only FCFS dispatch, so the next candidate must wait for
  // the periodic scans at t=2.7, t=5.4, and t=8.1. Those ticks are not
  // aligned with the candidates' completion events at t=3.7, t=6.4, and
  // t=9.1, or with the original job's completion at t=10.
  sim.advance_to(8.2);
  const auto backfill_status =
      sim.get_job_statuses({backfill_1, backfill_2, backfill_3});
  assert(backfill_status.size() == 3);
  for (const auto &status : backfill_status) {
    assert(status.start_time.has_value());
  }
  assert(approx_equal(*backfill_status[0].start_time, 2.7));
  assert(approx_equal(*backfill_status[1].start_time, 5.4));
  assert(approx_equal(*backfill_status[2].start_time, 8.1));

  // Backfill ticks are farther apart than the arrival events at t=2, 4, and
  // 6. Every built-in policy must defer the fitting candidate until t=10.
  for (const auto policy : {BackfillPolicy::EASY, BackfillPolicy::CONSERVATIVE,
                            BackfillPolicy::HYBRID}) {
    auto policy_params = make_params();
    policy_params.m_backfill_policy = policy;
    policy_params.m_backfill_interval = 10.0;
    if (policy == BackfillPolicy::HYBRID) {
      policy_params.m_reservation_depth = 1;
    }
    Simulation policy_sim(policy_params);
    policy_sim.get_trace().load_data(0);
    (void)policy_sim.append_job(0.0, 90, kTestQueueInput, 30.0);
    const auto policy_head =
        policy_sim.append_job(0.0, 100, kTestQueueInput, 1.0);
    const auto policy_candidate =
        policy_sim.append_job(0.0, 10, kTestQueueInput, 1.0);
    (void)policy_head;
    (void)policy_sim.append_job(2.0, 100, kTestQueueInput, 1.0);
    (void)policy_sim.append_job(4.0, 100, kTestQueueInput, 1.0);
    (void)policy_sim.append_job(6.0, 100, kTestQueueInput, 1.0);

    policy_sim.advance_to(9.0);
    assert(!policy_sim.get_job_statuses({policy_candidate})[0]
                .start_time.has_value());
    policy_sim.advance_to(10.0);
    const auto status = policy_sim.get_job_statuses({policy_candidate})[0];
    assert(status.start_time.has_value());
    assert(approx_equal(*status.start_time, 10.0));
  }

  // An arrival exactly at a periodic timestamp receives its ordinary FCFS
  // dispatch first, then the tick's backfill pass at the same timestamp.
  auto aligned_params = make_params();
  aligned_params.m_backfill_policy = BackfillPolicy::HYBRID;
  aligned_params.m_reservation_depth = 1;
  aligned_params.m_backfill_interval = 10.0;
  Simulation aligned_sim(aligned_params);
  aligned_sim.get_trace().load_data(0);
  (void)aligned_sim.append_job(0.0, 90, kTestQueueInput, 100.0);
  (void)aligned_sim.append_job(0.0, 100, kTestQueueInput, 1.0);
  const auto aligned_candidate =
      aligned_sim.append_job(10.0, 10, kTestQueueInput, 1.0);
  aligned_sim.advance_to(10.0);
  const auto aligned_status =
      aligned_sim.get_job_statuses({aligned_candidate}).front();
  assert(aligned_status.start_time.has_value());
  assert(approx_equal(*aligned_status.start_time, 10.0));

  sim.advance_to(10.0);
  const auto status = sim.get_job_statuses({blocked});
  assert(status.size() == 1);
  assert(status.front().state == Simulation::Job_State::RUNNING);
  assert(status.front().start_time.has_value());
  assert(approx_equal(*status.front().start_time, 10.0));

  auto invalid_params = make_params();
  invalid_params.m_backfill_interval = 1.0;
  invalid_params.m_backfill_policy = BackfillPolicy::NONE;
  bool rejected = false;
  try {
    Simulation invalid(invalid_params);
    (void)invalid;
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);

  std::cout << "  PASSED" << std::endl;
}

void test_append_with_known_actual_runtime() {
  std::cout << "\n=== Known actual runtime for streaming jobs ===" << std::endl;
  Sim_Params params = make_params();
  Simulation sim(params);
  sim.get_trace().load_data(0);

  const auto job = sim.append_job(0.0, 10, kTestQueueInput, 100.0, 25.0);
  sim.advance_to(24.0);
  assert(sim.get_job_statuses({job}).front().state ==
         Simulation::Job_State::RUNNING);
  sim.advance_to(25.0);
  const auto completed = sim.get_job_statuses({job}).front();
  assert(completed.state == Simulation::Job_State::COMPLETED);
  assert(completed.end_time == 25.0);

  bool rejected = false;
  try {
    sim.append_job(25.0, 10, kTestQueueInput, 10.0, 11.0);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  std::cout << "  PASSED" << std::endl;
}

void test_fractional_streaming_limits_validate_stored_value() {
  std::cout << "\n=== Fractional limits validate the stored value ==="
            << std::endl;
  Simulation sim(make_params());
  sim.get_trace().load_data(0);

  bool rejected = false;
  try {
    (void)sim.append_job(0.0, 1, kTestQueueInput, 10.25);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  assert(sim.get_trace().data().empty());

  const auto single = sim.append_job(0.0, 1, kTestQueueInput, 11, 10.25);
  assert(sim.get_trace().job_at(single).get_limit_time() == 11);
  assert(approx_equal(sim.get_trace().job_at(single).get_actual_run_time(),
                      10.25));

  const std::vector<Simulation::Job_Append_Request> invalid_batch = {
      {1.0, 1, kTestQueueInput, 21, 20.01},
      {2.0, 1, kTestQueueInput, 30.99},
  };
  rejected = false;
  try {
    (void)sim.append_jobs(invalid_batch);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  assert(sim.get_trace().data().size() == 1);

  const std::vector<Simulation::Job_Append_Request> batch = {
      {1.0, 1, kTestQueueInput, 21, 20.01},
      {2.0, 1, kTestQueueInput, 31, 30.99},
  };
  const auto jobs = sim.append_jobs(batch);
  assert(sim.get_trace().job_at(jobs[0]).get_limit_time() == 21);
  assert(sim.get_trace().job_at(jobs[1]).get_limit_time() == 31);
  assert(approx_equal(sim.get_trace().job_at(jobs[0]).get_actual_run_time(),
                      20.01));
  assert(approx_equal(sim.get_trace().job_at(jobs[1]).get_actual_run_time(),
                      30.99));

  std::cout << "  PASSED" << std::endl;
}

int main() {
  std::cout << "====================================" << std::endl;
  std::cout << "Append-Job Test Suite" << std::endl;
  std::cout << "====================================" << std::endl;

  write_empty_trace_fixture();

  try {
    test_append_with_empty_trace();
    test_appended_job_statuses();
    test_expected_start_is_earliest_fit();
    test_expected_start_covers_unreserved_waiting_jobs();
    test_chained_conservative_reservations_allow_early_backfill();
#if defined(DR_EVT_HAS_SER20)
    test_exact_lookahead_replays_without_mutating_live_state();
#endif
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
    test_capacity_schedule_across_streaming_advances();
    test_reservations_follow_capacity_schedule();
    test_planning_horizon_event_driven();
    test_backfill_queue_depth_is_shared_per_timestamp();
    test_periodic_backfill_queue_depth_is_one_cycle_per_tick();
    test_horizon_skipped_jobs_and_queue_depth();
    test_fcfs_conservative_ordering_with_easy();
    test_submit_job_records_busy_nodes();
    test_append_jobs_memory_pressure();
    test_resource_area_and_time_accounted_utilization();
    test_prediction_horizon();
    test_periodic_hybrid_backfill();
    test_periodic_backfill_idle_ticks();
    test_periodic_backfill_run_until_exclusive();
    test_periodic_easy_scheduler_selection();
    test_append_with_known_actual_runtime();
    test_fractional_streaming_limits_validate_stored_value();

    std::cout << "\n====================================" << std::endl;
    std::cout << "ALL APPEND_JOB TESTS PASSED" << std::endl;
    std::cout << "====================================" << std::endl;
    return EXIT_SUCCESS;
  } catch (const std::exception &e) {
    std::cerr << "\nTEST FAILED: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
}
