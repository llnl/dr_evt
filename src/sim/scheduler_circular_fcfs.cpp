/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include "sim/scheduler_circular_fcfs.hpp"
#include <algorithm>

namespace dr_evt {

void CircularBufferFCFSScheduler::sync_to(sim_time_t current_time) {
    if (current_time <= m_current_tracked_time) {
        return;
    }

    while (m_eligible_end_idx < m_wait_queue.size() &&
           m_wait_queue[m_eligible_end_idx].submit_time <= current_time) {
        ++m_eligible_end_idx;
    }
    m_current_tracked_time = current_time;
}

void CircularBufferFCFSScheduler::mark_removed(job_no_t job_id) {
    for (auto& entry : m_wait_queue) {
        if (entry.job_id == job_id && !entry.removed) {
            entry.removed = true;
            ++m_removed_count;
            return;
        }
    }
}

std::vector<job_no_t> CircularBufferFCFSScheduler::schedule(
    num_nodes_t free_nodes,
    const std::map<job_no_t, sim_time_t>& running_jobs,
    sim_time_t current_time)
{
    sync_to(current_time);

    // Garbage collection: compact queue if >50% is garbage. Same
    // approach as FCFSScheduler, but no pointer-subtraction pitfall to
    // avoid here - operator[] is used throughout instead.
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

    // Step 1: consume the front of the eligible queue with pop_front()
    // (O(1) on a circular_buffer, same as on a deque) instead of
    // scanning by index. See FCFSScheduler::schedule() for the full
    // reasoning - identical here.
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

    if (m_backfill_policy == BackfillPolicy::NONE) {
        return jobs_to_run;
    }

    // FCFS head is now genuinely at the front and blocked - calculate
    // reservation for backfilling. See FCFSScheduler::schedule() for
    // why effective_running_jobs must include jobs_to_run.
    std::map<job_no_t, sim_time_t> effective_running_jobs = running_jobs;
    for (job_no_t job_id : jobs_to_run) {
        effective_running_jobs[job_id] = current_time;
    }

    m_fcfs_reservation_time = calculate_fcfs_reservation(
        m_wait_queue.front().nodes_requested, available_nodes, effective_running_jobs, current_time);

    // Try backfilling with remaining eligible jobs. These can be at any
    // position (not just the front), so pop_front() doesn't apply here -
    // keep the lazy mark-and-defer-to-GC removal for this part.
    for (size_t i = 1; i < m_eligible_end_idx; ++i) {
        const auto& job = m_wait_queue[i];
        if (job.removed) continue;

        if (job.nodes_requested > available_nodes) continue;

        // IMPORTANT: strict < (not <=) - see FCFSScheduler::schedule()
        // for why. Backfill jobs must complete BEFORE the reservation
        // time, not at it.
        if (current_time + job.runtime_estimate < m_fcfs_reservation_time) {
            jobs_to_run.push_back(job.job_id);
            available_nodes -= job.nodes_requested;
            m_wait_queue[i].removed = true;
            ++m_removed_count;

            if (available_nodes == 0) break;
        }
    }

    return jobs_to_run;
}

} // namespace dr_evt
