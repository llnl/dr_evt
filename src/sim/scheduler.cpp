/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <algorithm>
#include <stdexcept>
#include "sim/scheduler.hpp"

namespace dr_evt {

Scheduler::Scheduler(num_nodes_t total_nodes,
                     const Trace::trace_data_t& job_data,
                     BackfillPolicy bf_policy,
                     PriorityPolicy pri_policy,
                     RuntimeEstimateMode rt_mode)
  : m_total_nodes(total_nodes),
    m_free_nodes(total_nodes),
    m_backfill_policy(bf_policy),
    m_priority_policy(pri_policy),
    m_runtime_mode(rt_mode),
    m_schedule(total_nodes, job_data),
    m_job_data_ptr(&job_data)
{
}

std::vector<job_no_t> Scheduler::submit_jobs(const std::vector<job_no_t>& jobs,
                                               sim_time_t current_time)
{
    // Add all jobs to wait queue
    for (job_no_t job_idx : jobs) {
        const auto& job = (*m_job_data_ptr)[job_idx];
        if (job.get_num_nodes() > m_total_nodes) {
            throw std::runtime_error(
                "Job requests more nodes than system capacity");
        }
        m_wait_queue.insert(job_idx);
    }

    // Trigger scheduling to find which jobs can start now
    return trigger_schedule(current_time);
}

std::vector<job_no_t> Scheduler::start_jobs(const std::vector<job_no_t>& jobs,
                                              sim_time_t current_time)
{
    std::vector<job_no_t> started;

    for (job_no_t job_idx : jobs) {
        // Verify job is scheduled
        auto sched_it = m_scheduled_jobs.find(job_idx);
        if (sched_it == m_scheduled_jobs.end()) {
            continue; // Job not scheduled, skip
        }

        // Verify it's time to start
        if (sched_it->second > current_time) {
            continue; // Not time yet
        }

        const auto& job = (*m_job_data_ptr)[job_idx];
        num_nodes_t nodes = job.get_num_nodes();

        // Verify we have enough nodes
        if (nodes > m_free_nodes) {
            // Not enough resources right now, skip this job
            // It will be reconsidered on the next schedule trigger
            continue;
        }

        // Move from scheduled to running
        m_running_jobs[job_idx] = current_time;
        m_scheduled_jobs.erase(sched_it);

        // Allocate resources
        m_free_nodes -= nodes;

        std::cout << "  Resources allocated: " << nodes << " nodes, "
                  << m_free_nodes << "/" << m_total_nodes << " remaining\n";

        started.push_back(job_idx);
    }

    // After starting jobs, trigger schedule for potential backfill
    std::vector<job_no_t> backfill_jobs = trigger_schedule(current_time);

    // Try to start backfill jobs, but only if resources available
    for (job_no_t job_idx : backfill_jobs) {
        // Check if job is already running (might have started in previous loop)
        if (m_running_jobs.find(job_idx) != m_running_jobs.end()) {
            continue;
        }

        // Check if job is scheduled
        auto sched_it = m_scheduled_jobs.find(job_idx);
        if (sched_it == m_scheduled_jobs.end()) {
            continue; // Not scheduled, skip
        }

        const auto& job = (*m_job_data_ptr)[job_idx];
        num_nodes_t nodes = job.get_num_nodes();

        if (nodes <= m_free_nodes) {
            // Enough resources, start this backfill job
            m_running_jobs[job_idx] = current_time;
            m_scheduled_jobs.erase(sched_it);
            m_free_nodes -= nodes;
            std::cout << "  Backfill: allocated " << nodes << " nodes, "
                      << m_free_nodes << "/" << m_total_nodes << " remaining\n";
            started.push_back(job_idx);
        } else {
            std::cout << "  Backfill failed: job " << job_idx << " needs "
                      << nodes << " nodes, only " << m_free_nodes << " available\n";
        }
    }

    return started;
}

std::vector<job_no_t> Scheduler::end_jobs(const std::vector<job_no_t>& jobs,
                                            sim_time_t current_time)
{
    for (job_no_t job_idx : jobs) {
        // Verify job is running
        auto run_it = m_running_jobs.find(job_idx);
        if (run_it == m_running_jobs.end()) {
            continue; // Job not running, skip
        }

        const auto& job = (*m_job_data_ptr)[job_idx];
        num_nodes_t nodes = job.get_num_nodes();

        // Release resources
        m_free_nodes += nodes;

        std::cout << "  Resources freed: " << nodes << " nodes, "
                  << "now " << m_free_nodes << "/" << m_total_nodes
                  << " free\n";

        // Remove from running jobs
        m_running_jobs.erase(run_it);

        // Remove reservation from schedule gaps
        m_schedule.remove_reservation(job_idx);
    }

    // Update schedule to potentially shift reservations earlier
    std::vector<job_no_t> ready_jobs = update_schedule(current_time);

    // Trigger scheduling for new opportunities
    std::vector<job_no_t> new_jobs = trigger_schedule(current_time);
    ready_jobs.insert(ready_jobs.end(), new_jobs.begin(), new_jobs.end());

    return ready_jobs;
}

sim_time_t Scheduler::get_reservation_time(job_no_t job_idx) const
{
    auto it = m_scheduled_jobs.find(job_idx);
    if (it != m_scheduled_jobs.end()) {
        return it->second;
    }
    return static_cast<sim_time_t>(-1.0);
}

bool Scheduler::is_waiting(job_no_t job_idx) const
{
    return m_wait_queue.find(job_idx) != m_wait_queue.end();
}

bool Scheduler::is_scheduled(job_no_t job_idx) const
{
    return m_scheduled_jobs.find(job_idx) != m_scheduled_jobs.end();
}

bool Scheduler::is_running(job_no_t job_idx) const
{
    return m_running_jobs.find(job_idx) != m_running_jobs.end();
}

std::vector<job_no_t> Scheduler::trigger_schedule(sim_time_t current_time)
{
    if (m_wait_queue.empty()) {
        return {};
    }

    // Sort wait queue according to priority policy
    std::vector<job_no_t> sorted_jobs = sort_jobs(m_wait_queue);

    std::vector<job_no_t> jobs_to_start;
    std::set<job_no_t> jobs_to_remove_from_wait;

    for (job_no_t job_idx : sorted_jobs) {
        // Find earliest start time for this job
        sim_time_t start_time = fit_in_schedule(job_idx, current_time);

        // If job can start now
        if (start_time == current_time) {
            jobs_to_start.push_back(job_idx);
            m_scheduled_jobs[job_idx] = start_time;
            jobs_to_remove_from_wait.insert(job_idx);

            // Add to schedule gaps
            tdiff_t runtime_est = get_runtime_estimate(job_idx);
            m_schedule.add_reservation(job_idx, start_time, runtime_est);
        } else {
            // Determine if all jobs should be scheduled or just the first
            bool should_schedule = false;

            if (m_backfill_policy == BackfillPolicy::EASY) {
                // Only schedule the first job in queue (if no jobs scheduled yet)
                if (m_scheduled_jobs.empty()) {
                    should_schedule = true;
                }
            } else if (m_backfill_policy == BackfillPolicy::CONSERVATIVE) {
                // Schedule all jobs
                should_schedule = true;
            }

            if (should_schedule) {
                m_scheduled_jobs[job_idx] = start_time;
                jobs_to_remove_from_wait.insert(job_idx);

                // Add to schedule gaps
                tdiff_t runtime_est = get_runtime_estimate(job_idx);
                m_schedule.add_reservation(job_idx, start_time, runtime_est);
            }
        }
    }

    // Remove scheduled jobs from wait queue
    for (job_no_t job_idx : jobs_to_remove_from_wait) {
        m_wait_queue.erase(job_idx);
    }

    return jobs_to_start;
}

sim_time_t Scheduler::fit_in_schedule(job_no_t job_idx, sim_time_t current_time)
{
    const auto& job = (*m_job_data_ptr)[job_idx];
    epoch_t submit_time = job.get_submit_time();
    sim_time_t submit_sim_time = static_cast<sim_time_t>(submit_time.first) + submit_time.second;
    sim_time_t earliest_start = std::max(current_time, submit_sim_time);
    num_nodes_t nodes = job.get_num_nodes();
    tdiff_t runtime_est = get_runtime_estimate(job_idx);

    // First, try to find a gap in the existing schedule
    std::vector<Window> gaps = m_schedule.get_windows(earliest_start, runtime_est, nodes);
    if (!gaps.empty()) {
        // Return the earliest gap start time
        sim_time_t earliest_gap = gaps[0].start;
        for (const auto& gap : gaps) {
            earliest_gap = std::min(earliest_gap, gap.start);
        }
        return std::max(earliest_gap, earliest_start);
    }

    // No gap found, fit at the end of the schedule
    return m_schedule.fit_at_the_end(earliest_start, nodes);
}

std::vector<job_no_t> Scheduler::sort_jobs(const std::set<job_no_t>& jobs) const
{
    std::vector<job_no_t> sorted(jobs.begin(), jobs.end());

    if (m_priority_policy == PriorityPolicy::FCFS) {
        // Sort by submission time, then by job index
        std::sort(sorted.begin(), sorted.end(),
                  [this](job_no_t a, job_no_t b) {
                      const auto& job_a = (*m_job_data_ptr)[a];
                      const auto& job_b = (*m_job_data_ptr)[b];
                      auto t_a = job_a.get_submit_time();
                      auto t_b = job_b.get_submit_time();
                      if (t_a == t_b) {
                          return a < b;
                      }
                      return t_a < t_b;
                  });
    } else if (m_priority_policy == PriorityPolicy::SJF) {
        // Sort by runtime estimate (ascending), then by job index
        std::sort(sorted.begin(), sorted.end(),
                  [this](job_no_t a, job_no_t b) {
                      tdiff_t runtime_a = get_runtime_estimate(a);
                      tdiff_t runtime_b = get_runtime_estimate(b);
                      if (runtime_a == runtime_b) {
                          return a < b;
                      }
                      return runtime_a < runtime_b;
                  });
    } else if (m_priority_policy == PriorityPolicy::LJF) {
        // Sort by runtime estimate (descending), then by job index
        std::sort(sorted.begin(), sorted.end(),
                  [this](job_no_t a, job_no_t b) {
                      tdiff_t runtime_a = get_runtime_estimate(a);
                      tdiff_t runtime_b = get_runtime_estimate(b);
                      if (runtime_a == runtime_b) {
                          return a < b;
                      }
                      return runtime_a > runtime_b;
                  });
    }

    return sorted;
}

tdiff_t Scheduler::get_runtime_estimate(job_no_t job_idx) const
{
    const auto& job = (*m_job_data_ptr)[job_idx];

    if (m_runtime_mode == RuntimeEstimateMode::USE_LIMIT) {
        // Use user-provided time limit
        return static_cast<tdiff_t>(job.get_limit_time());
    } else {
        // Use actual runtime (oracle mode)
        return job.get_exec_time();
    }
}

std::vector<job_no_t> Scheduler::update_schedule(sim_time_t current_time)
{
    if (m_scheduled_jobs.empty()) {
        return {};
    }

    // Recalculate start times for scheduled jobs
    // They may be able to start earlier now that some jobs have ended

    std::vector<job_no_t> jobs_to_reschedule;
    for (const auto& pair : m_scheduled_jobs) {
        jobs_to_reschedule.push_back(pair.first);
    }

    // Sort by their current reservation time
    std::sort(jobs_to_reschedule.begin(), jobs_to_reschedule.end(),
              [this](job_no_t a, job_no_t b) {
                  return m_scheduled_jobs[a] < m_scheduled_jobs[b];
              });

    std::vector<job_no_t> jobs_ready_now;

    for (job_no_t job_idx : jobs_to_reschedule) {
        // Remove from schedule temporarily
        m_schedule.remove_reservation(job_idx);

        // Find new start time
        sim_time_t new_time = fit_in_schedule(job_idx, current_time);

        // Update reservation
        m_scheduled_jobs[job_idx] = new_time;
        tdiff_t runtime_est = get_runtime_estimate(job_idx);
        m_schedule.add_reservation(job_idx, new_time, runtime_est);

        // If job can now start immediately
        if (new_time == current_time) {
            jobs_ready_now.push_back(job_idx);
        }
    }

    return jobs_ready_now;
}

} // end of namespace dr_evt
