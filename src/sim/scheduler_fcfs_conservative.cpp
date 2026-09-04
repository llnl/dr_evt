/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include "sim/scheduler_fcfs_conservative.hpp"
#include <algorithm>

namespace dr_evt {

void FCFSConservativeScheduler::sync_to(sim_time_t current_time) {
    if (current_time <= m_current_tracked_time) {
        return;
    }

    // Advance index to mark newly eligible jobs
    while (m_eligible_end_idx < m_wait_queue.size() &&
           m_wait_queue[m_eligible_end_idx].submit_time <= current_time) {
        ++m_eligible_end_idx;
    }
    m_current_tracked_time = current_time;
}

void FCFSConservativeScheduler::mark_removed(job_no_t job_id) {
    for (auto& entry : m_wait_queue) {
        if (entry.job_id == job_id && !entry.removed) {
            entry.removed = true;
            ++m_removed_count;
            return;
        }
    }
}

sim_time_t FCFSConservativeScheduler::calculate_conservative_window(
    size_t backfill_index,
    num_nodes_t available_nodes,
    const std::map<job_no_t, sim_time_t>& running_jobs,
    sim_time_t current_time)
{
    // For conservative backfilling, we need to ensure the backfill job
    // doesn't delay ANY waiting job that came before it.
    // Calculate reservation time for each job ahead of this one.

    sim_time_t earliest_conflict = std::numeric_limits<sim_time_t>::max();

    // Check all jobs from the front up to (but not including) this backfill candidate
    for (size_t i = 0; i < backfill_index; ++i) {
        const auto& job = m_wait_queue[i];
        if (job.removed) continue;

        // Calculate when this job could start
        sim_time_t job_reservation = calculate_fcfs_reservation(
            job.nodes_requested,
            available_nodes,
            running_jobs,
            current_time
        );

        // Track the earliest reservation time
        earliest_conflict = std::min(earliest_conflict, job_reservation);
    }

    return earliest_conflict;
}

std::vector<job_no_t> FCFSConservativeScheduler::schedule(
    num_nodes_t free_nodes,
    const std::map<job_no_t, sim_time_t>& running_jobs,
    sim_time_t current_time)
{
    // Sync eligibility tracking
    sync_to(current_time);

    // Garbage collection: compact queue if >50% is garbage
    if (m_removed_count > 0 && m_removed_count * 2 > m_wait_queue.size()) {
        size_t eligible_removed = 0;
        for (size_t idx = 0; idx < m_eligible_end_idx && idx < m_wait_queue.size(); ++idx) {
            if (m_wait_queue[idx].removed) {
                ++eligible_removed;
            }
        }

        auto new_end = std::remove_if(m_wait_queue.begin(), m_wait_queue.end(),
            [](const JobEntry& entry) { return entry.removed; });

        m_wait_queue.erase(new_end, m_wait_queue.end());
        m_eligible_end_idx -= eligible_removed;
        m_removed_count = 0;
    }

    if (m_eligible_end_idx == 0) {
        return {};  // No eligible jobs
    }

    std::vector<job_no_t> jobs_to_run;
    num_nodes_t available_nodes = free_nodes;

    // Step 1: Start jobs from the front of the queue (FCFS order)
    while (m_eligible_end_idx > 0 && !m_wait_queue.empty() &&
           (m_wait_queue.front().removed ||
            m_wait_queue.front().nodes_requested <= available_nodes)) {
        if (!m_wait_queue.front().removed) {
            jobs_to_run.push_back(m_wait_queue.front().job_id);
            available_nodes -= m_wait_queue.front().nodes_requested;
        } else {
            --m_removed_count;
        }
        m_wait_queue.pop_front();
        --m_eligible_end_idx;
    }

    if (active_job_count() == 0) {
        return jobs_to_run;
    }

    // Step 2: Check if backfilling is disabled
    if (m_backfill_policy == BackfillPolicy::NONE) {
        // No backfilling - strict FCFS order only
        return jobs_to_run;
    }

    // Step 3: Try backfilling based on policy
    // Update effective running jobs to include jobs just started
    std::map<job_no_t, sim_time_t> effective_running_jobs = running_jobs;
    for (job_no_t job_id : jobs_to_run) {
        effective_running_jobs[job_id] = current_time;
    }

    if (m_backfill_policy == BackfillPolicy::EASY) {
        // EASY backfilling: Only consider the head job's reservation
        m_fcfs_reservation_time = calculate_fcfs_reservation(
            m_wait_queue.front().nodes_requested,
            available_nodes,
            effective_running_jobs,
            current_time
        );

        // Try backfilling with remaining eligible jobs
        for (size_t i = 1; i < m_eligible_end_idx; ++i) {
            const auto& job = m_wait_queue[i];
            if (job.removed) continue;

            // Check resource fit
            if (job.nodes_requested > available_nodes) continue;

            // Check if job completes before head's reservation
            if (current_time + job.run_time_estimate < m_fcfs_reservation_time) {
                jobs_to_run.push_back(job.job_id);
                available_nodes -= job.nodes_requested;
                m_wait_queue[i].removed = true;
                ++m_removed_count;

                if (available_nodes == 0) break;
            }
        }
    }
    else if (m_backfill_policy == BackfillPolicy::CONSERVATIVE) {
        // CONSERVATIVE backfilling: Must not delay ANY waiting job

        // Try backfilling with jobs that won't delay anyone ahead of them
        for (size_t i = 1; i < m_eligible_end_idx; ++i) {
            const auto& job = m_wait_queue[i];
            if (job.removed) continue;

            // Check resource fit
            if (job.nodes_requested > available_nodes) continue;

            // Calculate conservative window: earliest reservation of all jobs ahead
            sim_time_t conservative_window = calculate_conservative_window(
                i, available_nodes, effective_running_jobs, current_time
            );

            // Job can backfill if it completes before ANY job ahead needs to start
            if (current_time + job.run_time_estimate < conservative_window) {
                jobs_to_run.push_back(job.job_id);
                available_nodes -= job.nodes_requested;
                m_wait_queue[i].removed = true;
                ++m_removed_count;

                // Update effective running jobs for next iteration
                effective_running_jobs[job.job_id] = current_time;

                if (available_nodes == 0) break;
            }
        }
    }

    return jobs_to_run;
}

} // namespace dr_evt
