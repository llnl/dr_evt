/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include "sim/scheduler.hpp"
#include <algorithm>
#include <iostream>
#include <map>

namespace dr_evt {

Scheduler::Scheduler(num_nodes_t total_nodes,
                     const std::vector<Job_Record>& job_data,
                     BackfillPolicy bf_policy,
                     PriorityPolicy pri_policy,
                     RuntimeEstimateMode rt_mode)
  : m_total_nodes(total_nodes),
    m_backfill_policy(bf_policy),
    m_priority_policy(pri_policy),
    m_runtime_mode(rt_mode),
    m_job_data_ptr(&job_data),
    m_fcfs_reservation_time(0.0)
{}

std::vector<job_no_t> Scheduler::schedule(
    std::set<job_no_t>& wait_queue,
    num_nodes_t free_nodes,
    const std::map<job_no_t, sim_time_t>& running_jobs,
    sim_time_t current_time)
{
    if (wait_queue.empty()) {
        return {};
    }

    // Filter to only eligible jobs (submit_time <= current_time)
    std::set<job_no_t> eligible_jobs;
    for (job_no_t job_idx : wait_queue) {
        const auto& job = (*m_job_data_ptr)[job_idx];
        const auto& ts = job.get_submit_time();
        sim_time_t submit_time = static_cast<sim_time_t>(ts.first) + ts.second;
        if (submit_time <= current_time) {
            eligible_jobs.insert(job_idx);
        }
    }

    if (eligible_jobs.empty()) {
        return {};
    }

    std::vector<job_no_t> jobs_to_run;
    std::vector<job_no_t> sorted_jobs = sort_jobs(eligible_jobs);

    // Track available nodes as we schedule
    num_nodes_t available_nodes = free_nodes;

    // EASY Backfilling: FCFS head first
    job_no_t fcfs_head = sorted_jobs[0];
    const auto& head_job = (*m_job_data_ptr)[fcfs_head];
    num_nodes_t head_nodes = head_job.get_num_nodes();

    // Can FCFS head start now?
    if (head_nodes <= available_nodes) {
        // FCFS head can run
        jobs_to_run.push_back(fcfs_head);
        available_nodes -= head_nodes;
        m_fcfs_reservation_time = current_time;
    } else {
        // FCFS head can't fit - calculate when it CAN start
        m_fcfs_reservation_time = calculate_fcfs_reservation(
            fcfs_head, free_nodes, running_jobs, current_time);
    }

    // Try backfilling with remaining jobs
    for (size_t i = 1; i < sorted_jobs.size(); i++) {
        job_no_t job_idx = sorted_jobs[i];
        const auto& job = (*m_job_data_ptr)[job_idx];
        num_nodes_t nodes = job.get_num_nodes();

        // Check 1: Fits in currently available nodes?
        if (nodes > available_nodes) {
            // Check if any remaining jobs could fit
            bool any_fit = false;
            for (size_t j = i + 1; j < sorted_jobs.size(); j++) {
                if ((*m_job_data_ptr)[sorted_jobs[j]].get_num_nodes() <= available_nodes) {
                    any_fit = true;
                    break;
                }
            }
            if (!any_fit) {
                break;  // No remaining jobs fit
            }
            continue;
        }

        // Check 2: Completes before FCFS reservation?
        tdiff_t runtime_est = get_runtime_estimate(job_idx);
        if (current_time + runtime_est <= m_fcfs_reservation_time) {
            // Can backfill!
            jobs_to_run.push_back(job_idx);
            available_nodes -= nodes;
        } else {
            // Check if window too short for ALL remaining
            tdiff_t shortest_remaining = runtime_est;
            for (size_t j = i + 1; j < sorted_jobs.size(); j++) {
                shortest_remaining = std::min(shortest_remaining,
                                             get_runtime_estimate(sorted_jobs[j]));
            }
            if (current_time + shortest_remaining > m_fcfs_reservation_time) {
                break;  // Window too short
            }
        }
    }

    // Remove scheduled jobs from wait queue
    for (job_no_t job_idx : jobs_to_run) {
        wait_queue.erase(job_idx);
    }

    return jobs_to_run;
}

std::vector<job_no_t> Scheduler::sort_jobs(const std::set<job_no_t>& jobs) const
{
    std::vector<job_no_t> sorted(jobs.begin(), jobs.end());

    switch (m_priority_policy) {
        case PriorityPolicy::FCFS:
            // Sort by submit time (FCFS = First Come First Served)
            std::sort(sorted.begin(), sorted.end(),
                [this](job_no_t a, job_no_t b) {
                    const auto& job_a = (*m_job_data_ptr)[a];
                    const auto& job_b = (*m_job_data_ptr)[b];
                    sim_time_t submit_a = static_cast<sim_time_t>(job_a.get_submit_time().first) +
                                          job_a.get_submit_time().second;
                    sim_time_t submit_b = static_cast<sim_time_t>(job_b.get_submit_time().first) +
                                          job_b.get_submit_time().second;
                    return submit_a < submit_b;
                });
            break;

        case PriorityPolicy::SJF:
            // Shortest Job First
            std::sort(sorted.begin(), sorted.end(),
                [this](job_no_t a, job_no_t b) {
                    return get_runtime_estimate(a) < get_runtime_estimate(b);
                });
            break;

        case PriorityPolicy::LJF:
            // Longest Job First
            std::sort(sorted.begin(), sorted.end(),
                [this](job_no_t a, job_no_t b) {
                    return get_runtime_estimate(a) > get_runtime_estimate(b);
                });
            break;
    }

    return sorted;
}

tdiff_t Scheduler::get_runtime_estimate(job_no_t job_idx) const
{
    const auto& job = (*m_job_data_ptr)[job_idx];

    switch (m_runtime_mode) {
        case RuntimeEstimateMode::USE_LIMIT:
            return job.get_limit_time();
        case RuntimeEstimateMode::USE_ACTUAL:
            return job.get_actual_duration();
        default:
            return job.get_limit_time();
    }
}

sim_time_t Scheduler::calculate_fcfs_reservation(
    job_no_t job_idx,
    num_nodes_t free_nodes,
    const std::map<job_no_t, sim_time_t>& running_jobs,
    sim_time_t current_time)
{
    const auto& job = (*m_job_data_ptr)[job_idx];
    num_nodes_t nodes_needed = job.get_num_nodes();

    // Collect end times of running jobs
    std::vector<std::pair<sim_time_t, num_nodes_t>> end_events;
    for (const auto& pair : running_jobs) {
        job_no_t running_idx = pair.first;
        sim_time_t start_time = pair.second;
        tdiff_t runtime = get_runtime_estimate(running_idx);
        sim_time_t end_time = start_time + runtime;

        if (end_time > current_time) {
            end_events.emplace_back(end_time, (*m_job_data_ptr)[running_idx].get_num_nodes());
        }
    }

    std::sort(end_events.begin(), end_events.end());

    // Simulate jobs completing
    num_nodes_t available = free_nodes;
    for (const auto& event : end_events) {
        available += event.second;
        if (available >= nodes_needed) {
            return event.first;  // Can start when this job completes
        }
    }

    // All running jobs complete but still not enough nodes?
    // This shouldn't happen if job fits in system
    return current_time;
}

} // namespace dr_evt
