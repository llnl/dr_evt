#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace dr_evt;

int main() {
  const auto root =
      std::filesystem::temp_directory_path() / "dr_evt_estimated_job_starts";
  std::filesystem::create_directories(root);
  const auto trace = root / "empty.csv";
  std::ofstream(trace) << "job_submit_time,num_nodes,time_limit\n";
  const auto params = [&] {
    Sim_Params p;
    p.m_infile = trace.string();
    p.m_total_nodes = 4;
    p.m_trace_format = "simple";
    p.m_run_time_mode = RunTimeMode::LIMIT;
    p.set_outfile((root / "jobs.csv").string());
    return p;
  };
#if DR_EVT_LEGACY_QUEUE_INPUT
  const char *queue = "pbatch";
#else
  const char *queue = "1";
#endif
  using Kind = Simulation::Start_Time_Kind;

  // EASY saves only its head projection, independently of on-demand queries.
  for (const auto implementation :
       {QueueImplementation::CIRCULAR, QueueImplementation::DEQUE,
        QueueImplementation::MULTIMAP, QueueImplementation::BLOCK}) {
    auto p = params();
    p.m_queue_impl = implementation;
    Simulation sim(p);
    sim.get_trace().load_data(0);
    assert(sim.get_start_times_by_est_caches().jobs.empty());
    const auto ids = sim.append_jobs({{0, 3, queue, 100},
                                      {0, 4, queue, 10},
                                      {0, 4, queue, 10},
                                      {200, 4, queue, 10}});
    auto cached = sim.get_start_times_by_est_caches();
    assert(!cached.estimated_at && cached.jobs.size() == 3);
    for (size_t i = 0; i < cached.jobs.size(); ++i) {
      const auto &job = cached.jobs[i];
      assert(job.job_idx == ids[i]);
      assert(!job.expected_start_time && job.start_time_kind == Kind::NONE);
    }
    assert(sim.get_job_statuses({ids[1]}).front().expected_start_time == 100);
    assert(!sim.get_start_times_by_est_caches().estimated_at);
    sim.advance_to(0);
    cached = sim.get_start_times_by_est_caches();
    assert(cached.estimated_at == 0 && cached.jobs.size() == 2);
    assert(cached.jobs[0].job_idx == ids[1]);
    assert(cached.jobs[0].expected_start_time == 100);
    assert(cached.jobs[0].start_time_kind == Kind::RESERVED);
    assert(cached.jobs[1].job_idx == ids[2]);
    assert(!cached.jobs[1].expected_start_time);
    const auto before = sim.get_statistics().jobs_started;
    (void)sim.get_job_statuses(ids);
    for (int i = 0; i < 3; ++i) {
      assert(sim.get_start_times_by_est_caches().jobs[0].expected_start_time ==
             100);
    }
    assert(sim.get_current_time() == 0);
    assert(sim.get_statistics().jobs_started == before);
    sim.advance_to(100);
    cached = sim.get_start_times_by_est_caches();
    assert(cached.jobs.size() == 1 && cached.jobs[0].job_idx == ids[2]);
    assert(cached.jobs[0].expected_start_time == 110);
#if defined(DR_EVT_HAS_SER20)
    const auto checkpoint = (root / "state.bin").string();
    sim.save_checkpoint(checkpoint);
    sim.load_checkpoint(checkpoint);
    assert(!sim.get_start_times_by_est_caches().estimated_at);
#endif
  }

  // The query does not extend the live backfill examination budget.
  for (const auto implementation :
       {QueueImplementation::CIRCULAR, QueueImplementation::DEQUE}) {
    auto p = params();
    p.m_queue_impl = implementation;
    p.m_backfill_policy = BackfillPolicy::CONSERVATIVE;
    p.m_priority_policy = PriorityPolicy::FCFS_CONSERVATIVE;
    p.m_backfill_queue_depth = 2;
    Simulation sim(p);
    sim.get_trace().load_data(0);
    const auto ids = sim.append_jobs({{0, 3, queue, 100},
                                      {0, 4, queue, 10},
                                      {0, 4, queue, 10},
                                      {0, 4, queue, 10}});
    sim.advance_to(0);
    const auto cached = sim.get_start_times_by_est_caches();
    assert(cached.jobs.size() == 3);
    assert(cached.jobs[0].expected_start_time == 100);
    assert(cached.jobs[1].expected_start_time == 110);
    assert(cached.jobs[1].start_time_kind == Kind::RESERVED);
    assert(cached.jobs[2].job_idx == ids[3]);
    assert(!cached.jobs[2].expected_start_time);
    assert(sim.get_job_statuses({ids[3]}).front().expected_start_time == 120);
    assert(!sim.get_start_times_by_est_caches().jobs[2].expected_start_time);
  }

  // Hybrid protects only its R prefix, while retaining later computed fits.
  for (const auto implementation :
       {QueueImplementation::CIRCULAR, QueueImplementation::DEQUE}) {
    auto p = params();
    p.m_queue_impl = implementation;
    p.m_backfill_policy = BackfillPolicy::HYBRID;
    p.m_reservation_depth = 1;
    Simulation sim(p);
    const auto ids = sim.append_jobs(
        {{0, 3, queue, 100}, {0, 4, queue, 10}, {0, 4, queue, 10}});
    sim.advance_to(0);
    const auto cached = sim.get_start_times_by_est_caches();
    assert(cached.jobs.size() == 2);
    assert(cached.jobs[0].job_idx == ids[1]);
    assert(cached.jobs[1].job_idx == ids[2]);
    assert(cached.jobs[0].expected_start_time == 100);
    assert(cached.jobs[0].start_time_kind == Kind::RESERVED);
    assert(cached.jobs[1].expected_start_time == 100);
    assert(cached.jobs[1].start_time_kind == Kind::ESTIMATED);
  }

  // Cache-first reporting retains live opportunities, while the missing tail
  // still accounts for full-duration placements of earlier queue entries.
  for (const auto implementation :
       {QueueImplementation::CIRCULAR, QueueImplementation::DEQUE}) {
    auto p = params();
    p.m_queue_impl = implementation;
    p.m_backfill_policy = BackfillPolicy::HYBRID;
    p.m_reservation_depth = 1;
    p.m_backfill_queue_depth = 2;
    Simulation sim(p);
    const auto ids = sim.append_jobs({{0, 3, queue, 100},
                                      {0, 4, queue, 10},
                                      {0, 4, queue, 10},
                                      {0, 4, queue, 10}});
    sim.advance_to(0);
    const auto plan = sim.get_start_times_by_est_caches();
    assert(plan.jobs.size() == 3 && !plan.jobs[2].expected_start_time);
    const auto mixed = sim.get_job_statuses({ids[3], ids[2], ids[1], ids[2]});
    assert(mixed[0].expected_start_time == 120);
    assert(mixed[1].expected_start_time == 100);
    assert(mixed[1].start_time_kind == Kind::ESTIMATED);
    assert(mixed[2].expected_start_time == 100);
    assert(mixed[2].start_time_kind == Kind::RESERVED);
    assert(mixed[3].expected_start_time == mixed[1].expected_start_time);
    assert(sim.get_job_statuses({ids[2]}).front().expected_start_time == 100);
    assert(
        sim.get_job_statuses({ids[2]}, 0, false).front().expected_start_time ==
        110);
    const auto protected_only =
        sim.get_job_statuses({ids[3], ids[2], ids[1], ids[2]}, 0, true, false);
    assert(protected_only[0].expected_start_time == 120);
    assert(protected_only[1].expected_start_time == 110);
    assert(protected_only[1].start_time_kind == Kind::ESTIMATED);
    assert(protected_only[2].expected_start_time == 100);
    assert(protected_only[2].start_time_kind == Kind::RESERVED);
    assert(protected_only[3].expected_start_time == 110);
    const auto enumerated = sim.get_job_statuses({}, 0, true, false);
    assert(enumerated.size() == 3);
    assert(enumerated[0].expected_start_time == 100);
    assert(enumerated[1].expected_start_time == 110);
    assert(enumerated[2].expected_start_time == 120);
    assert(sim.get_job_statuses({ids[2]}, 0, false, false)
               .front()
               .expected_start_time == 110);
    const auto protected_bounded =
        sim.get_job_statuses({ids[1], ids[2], ids[3]}, 1, true, false);
    assert(protected_bounded[0].expected_start_time == 100);
    assert(!protected_bounded[1].expected_start_time &&
           !protected_bounded[2].expected_start_time);
    const auto bounded = sim.get_job_statuses({ids[1], ids[2], ids[3]}, 1);
    assert(bounded[0].expected_start_time == 100);
    assert(!bounded[1].expected_start_time && !bounded[2].expected_start_time);
    const auto unchanged = sim.get_start_times_by_est_caches();
    assert(unchanged.estimated_at == plan.estimated_at);
    for (size_t i = 0; i < plan.jobs.size(); ++i) {
      assert(unchanged.jobs[i].expected_start_time ==
             plan.jobs[i].expected_start_time);
      assert(unchanged.jobs[i].start_time_kind == plan.jobs[i].start_time_kind);
    }
    assert(sim.get_current_time() == 0 &&
           sim.get_statistics().jobs_started == 1);
  }

#ifndef DR_EVT_LEGACY_RESERVATION_BARRIER
  // A later in-horizon reservation must constrain an earlier unprotected
  // forecast, even when the later job is not requested or is beyond depth.
  for (const auto implementation :
       {QueueImplementation::CIRCULAR, QueueImplementation::DEQUE}) {
    auto p = params();
    p.m_queue_impl = implementation;
    p.m_total_nodes = 5;
    p.m_backfill_policy = BackfillPolicy::HYBRID;
    p.m_reservation_depth = 1;
    p.m_planning_horizon = 20;
    Simulation sim(p);
    const auto ids = sim.append_jobs({{0, 2, queue, 10},
                                      {0, 2, queue, 100},
                                      {0, 5, queue, 10},
                                      {0, 3, queue, 200}});
    sim.advance_to(0);
    const auto cached = sim.get_start_times_by_est_caches();
    assert(cached.jobs[0].job_idx == ids[3]);
    assert(cached.jobs[0].start_time_kind == Kind::RESERVED);
    assert(cached.jobs[0].expected_start_time == 10);
    const auto protected_only =
        sim.get_job_statuses({ids[2], ids[3]}, 0, true, false);
    assert(protected_only[0].expected_start_time == 210);
    assert(protected_only[1].expected_start_time == 10);
    assert(protected_only[1].start_time_kind == Kind::RESERVED);
    const auto fresh = sim.get_job_statuses({ids[2], ids[3]}, 0, false);
    assert(fresh[0].expected_start_time == 100);
    assert(fresh[1].expected_start_time == 110);
    assert(sim.get_job_statuses({ids[2]}, 1, true, false)
               .front()
               .expected_start_time == 210);
  }
#endif

  // Interleaved unknowns move after predictions without changing queue-order
  // ties. A permanent capacity reduction leaves the four-node jobs unknown.
  for (const auto implementation :
       {QueueImplementation::CIRCULAR, QueueImplementation::DEQUE}) {
    const auto capacity = root / "capacity.csv";
    std::ofstream(capacity) << "time,total_nodes\n0,2\n";
    auto p = params();
    p.m_capacity_schedule = capacity.string();
    p.m_queue_impl = implementation;
    p.m_backfill_policy = BackfillPolicy::HYBRID;
    p.m_reservation_depth = 1;
    Simulation sim(p);
    const auto ids = sim.append_jobs({{0, 1, queue, 100},
                                      {0, 4, queue, 10},
                                      {0, 2, queue, 10},
                                      {0, 4, queue, 10},
                                      {0, 2, queue, 10}});
    sim.advance_to(0);
    const auto cached = sim.get_start_times_by_est_caches();
    assert(cached.jobs.size() == 4);
    assert(cached.jobs[0].job_idx == ids[2]);
    assert(cached.jobs[1].job_idx == ids[4]);
    assert(cached.jobs[0].expected_start_time == 100);
    assert(cached.jobs[1].expected_start_time == 100);
    assert(cached.jobs[0].start_time_kind == Kind::RESERVED);
    assert(cached.jobs[1].start_time_kind == Kind::ESTIMATED);
    assert(cached.jobs[2].job_idx == ids[1]);
    assert(cached.jobs[3].job_idx == ids[3]);
    assert(!cached.jobs[2].expected_start_time);
    assert(!cached.jobs[3].expected_start_time);
  }

  // Pass-accounting caches the selected forecast rule, including OpenPBS's
  // grouped releases, rather than recomputing starts in the query.
  for (const auto priority :
       {PriorityPolicy::FCFS, PriorityPolicy::FCFS_1S_EVT_GROUP}) {
    auto p = params();
    p.m_total_nodes = 3;
    p.m_priority_policy = priority;
    p.m_backfill_policy = BackfillPolicy::HYBRID;
    p.m_reservation_depth = 1;
    p.m_per_pass_accounting = true;
    Simulation sim(p);
    const auto ids = sim.append_jobs({{0, 1, queue, 10},
                                      {0, 1, queue, 11},
                                      {0, 2, queue, 2},
                                      {0, 1, queue, 11}});
    sim.advance_to(0);
    const auto cached = sim.get_start_times_by_est_caches();
    const bool grouped = priority == PriorityPolicy::FCFS_1S_EVT_GROUP;
    assert(cached.jobs.size() == (grouped ? 1 : 2));
    assert(cached.jobs[0].job_idx == ids[2]);
    assert(cached.jobs[0].expected_start_time == (grouped ? 11 : 10));
    assert(cached.jobs[0].start_time_kind == Kind::RESERVED);
    const auto protected_only = sim.get_job_statuses({ids[2]}, 0, true, false);
    assert(protected_only.front().expected_start_time ==
           cached.jobs[0].expected_start_time);
    assert(protected_only.front().start_time_kind == Kind::RESERVED);
  }

  // A computed out-of-horizon start is saved, without claiming protection.
  {
    auto p = params();
    p.m_backfill_policy = BackfillPolicy::CONSERVATIVE;
    p.m_priority_policy = PriorityPolicy::FCFS_CONSERVATIVE;
    p.m_planning_horizon = 10;
    Simulation sim(p);
    sim.get_trace().load_data(0);
    sim.append_jobs({{0, 3, queue, 100}, {0, 4, queue, 10}, {0, 4, queue, 10}});
    sim.advance_to(0);
    const auto cached = sim.get_start_times_by_est_caches();
    assert(cached.jobs.size() == 2);
    for (const auto &job : cached.jobs) {
      assert(job.expected_start_time == 100);
      assert(job.start_time_kind == Kind::ESTIMATED);
    }
  }

  // Periodic backfill retains its last pass between ticks and excludes starts.
  {
    auto p = params();
    p.m_backfill_policy = BackfillPolicy::CONSERVATIVE;
    p.m_priority_policy = PriorityPolicy::FCFS_CONSERVATIVE;
    p.m_backfill_interval = 10;
    p.m_backfill_queue_depth = 2;
    Simulation sim(p);
    sim.get_trace().load_data(0);
    sim.append_jobs({{0, 3, queue, 100},
                     {0, 4, queue, 10},
                     {0, 4, queue, 10},
                     {0, 4, queue, 10}});
    sim.advance_to(0);
    assert(!sim.get_start_times_by_est_caches().estimated_at);
    sim.advance_to(10);
    auto cached = sim.get_start_times_by_est_caches();
    assert(cached.estimated_at == 10 && cached.jobs.size() == 3);
    assert(cached.jobs[0].expected_start_time == 100);
    assert(cached.jobs[1].expected_start_time == 110);
    assert(!cached.jobs[2].expected_start_time);
    sim.advance_to(11);
    assert(sim.get_start_times_by_est_caches().estimated_at == 10);
    sim.advance_to(500);
    assert(sim.get_start_times_by_est_caches().jobs.empty());
  }
  std::filesystem::remove_all(root);
  std::cout << "PASS: cached starts, unknowns, depth/horizon, periodic "
               "freshness, and query isolation\n";
}
