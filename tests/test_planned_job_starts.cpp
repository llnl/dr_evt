#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace dr_evt;

int main() {
  const auto root = std::filesystem::temp_directory_path() /
                    "dr_evt_planned_job_starts";
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
  for (const auto implementation : {QueueImplementation::CIRCULAR,
                                    QueueImplementation::DEQUE,
                                    QueueImplementation::MULTIMAP,
                                    QueueImplementation::BLOCK}) {
    auto p = params();
    p.m_queue_impl = implementation;
    Simulation sim(p);
    sim.get_trace().load_data(0);
    const auto ids = sim.append_jobs({{0, 3, queue, 100}, {0, 4, queue, 10},
                                      {0, 4, queue, 10}, {200, 4, queue, 10}});
    auto cached = sim.get_planned_job_starts();
    assert(!cached.planned_at && cached.jobs.size() == 3);
    for (const auto &job : cached.jobs) {
      assert(!job.expected_start_time && job.start_time_kind == Kind::NONE);
    }
    assert(sim.get_job_statuses({ids[1]}).front().expected_start_time == 100);
    assert(!sim.get_planned_job_starts().planned_at);
    sim.advance_to(0);
    cached = sim.get_planned_job_starts();
    assert(cached.planned_at == 0 && cached.jobs.size() == 2);
    assert(cached.jobs[0].job_idx == ids[1]);
    assert(cached.jobs[0].expected_start_time == 100);
    assert(cached.jobs[0].start_time_kind == Kind::RESERVED);
    assert(cached.jobs[1].job_idx == ids[2]);
    assert(!cached.jobs[1].expected_start_time);
    const auto before = sim.get_statistics().jobs_started;
    (void)sim.get_job_statuses(ids);
    for (int i = 0; i < 3; ++i) {
      assert(sim.get_planned_job_starts().jobs[0].expected_start_time == 100);
    }
    assert(sim.get_current_time() == 0);
    assert(sim.get_statistics().jobs_started == before);
    sim.advance_to(100);
    cached = sim.get_planned_job_starts();
    assert(cached.jobs.size() == 1 && cached.jobs[0].job_idx == ids[2]);
    assert(cached.jobs[0].expected_start_time == 110);
#if defined(DR_EVT_HAS_SER20)
    const auto checkpoint = (root / "state.bin").string();
    sim.save_checkpoint(checkpoint);
    sim.load_checkpoint(checkpoint);
    assert(!sim.get_planned_job_starts().planned_at);
#endif
  }

  // The query does not extend the live backfill examination budget.
  for (const auto implementation : {QueueImplementation::CIRCULAR,
                                    QueueImplementation::DEQUE}) {
    auto p = params();
    p.m_queue_impl = implementation;
    p.m_backfill_policy = BackfillPolicy::CONSERVATIVE;
    p.m_priority_policy = PriorityPolicy::FCFS_CONSERVATIVE;
    p.m_backfill_queue_depth = 2;
    Simulation sim(p);
    sim.get_trace().load_data(0);
    const auto ids = sim.append_jobs({{0, 3, queue, 100}, {0, 4, queue, 10},
                                      {0, 4, queue, 10}, {0, 4, queue, 10}});
    sim.advance_to(0);
    const auto cached = sim.get_planned_job_starts();
    assert(cached.jobs.size() == 3);
    assert(cached.jobs[0].expected_start_time == 100);
    assert(cached.jobs[1].expected_start_time == 110);
    assert(cached.jobs[1].start_time_kind == Kind::RESERVED);
    assert(cached.jobs[2].job_idx == ids[3]);
    assert(!cached.jobs[2].expected_start_time);
    assert(sim.get_job_statuses({ids[3]}).front().expected_start_time == 120);
    assert(!sim.get_planned_job_starts().jobs[2].expected_start_time);
  }

  // Hybrid protects only its R prefix, while retaining later computed fits.
  for (const auto implementation : {QueueImplementation::CIRCULAR,
                                    QueueImplementation::DEQUE}) {
    auto p = params();
    p.m_queue_impl = implementation;
    p.m_backfill_policy = BackfillPolicy::HYBRID;
    p.m_reservation_depth = 1;
    Simulation sim(p);
    sim.append_jobs({{0, 3, queue, 100}, {0, 4, queue, 10},
                     {0, 4, queue, 10}});
    sim.advance_to(0);
    const auto cached = sim.get_planned_job_starts();
    assert(cached.jobs.size() == 2);
    assert(cached.jobs[0].expected_start_time == 100);
    assert(cached.jobs[0].start_time_kind == Kind::RESERVED);
    assert(cached.jobs[1].expected_start_time == 100);
    assert(cached.jobs[1].start_time_kind == Kind::ESTIMATED);
  }

  // Pass-accounting caches the selected forecast rule, including OpenPBS's
  // grouped releases, rather than recomputing starts in the query.
  for (const auto priority : {PriorityPolicy::FCFS,
                             PriorityPolicy::FCFS_1S_EVT_GROUP}) {
    auto p = params();
    p.m_total_nodes = 3;
    p.m_priority_policy = priority;
    p.m_backfill_policy = BackfillPolicy::HYBRID;
    p.m_reservation_depth = 1;
    p.m_per_pass_accounting = true;
    Simulation sim(p);
    const auto ids = sim.append_jobs({{0, 1, queue, 10}, {0, 1, queue, 11},
                                      {0, 2, queue, 2}, {0, 1, queue, 11}});
    sim.advance_to(0);
    const auto cached = sim.get_planned_job_starts();
    const bool grouped = priority == PriorityPolicy::FCFS_1S_EVT_GROUP;
    assert(cached.jobs.size() == (grouped ? 1 : 2));
    assert(cached.jobs[0].job_idx == ids[2]);
    assert(cached.jobs[0].expected_start_time ==
           (grouped ? 11 : 10));
    assert(cached.jobs[0].start_time_kind == Kind::RESERVED);
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
    const auto cached = sim.get_planned_job_starts();
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
    sim.append_jobs({{0, 3, queue, 100}, {0, 4, queue, 10},
                     {0, 4, queue, 10}, {0, 4, queue, 10}});
    sim.advance_to(0);
    assert(!sim.get_planned_job_starts().planned_at);
    sim.advance_to(10);
    auto cached = sim.get_planned_job_starts();
    assert(cached.planned_at == 10 && cached.jobs.size() == 3);
    assert(cached.jobs[0].expected_start_time == 100);
    assert(cached.jobs[1].expected_start_time == 110);
    assert(!cached.jobs[2].expected_start_time);
    sim.advance_to(11);
    assert(sim.get_planned_job_starts().planned_at == 10);
    sim.advance_to(500);
    assert(sim.get_planned_job_starts().jobs.empty());
  }
  std::filesystem::remove_all(root);
  std::cout << "PASS: cached starts, unknowns, depth/horizon, periodic freshness, and query isolation\n";
}
