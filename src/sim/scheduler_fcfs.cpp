/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include "sim/scheduler_fcfs.hpp"
#include <algorithm>

namespace dr_evt {

void FCFSScheduler::update_eligible_boundary(sim_time_t current_time) {
    // Advance index to mark newly eligible jobs
    // Jobs are in submit_time order, so stop at first ineligible
    while (m_eligible_end_idx < m_wait_queue.size() &&
           m_wait_queue[m_eligible_end_idx].submit_time <= current_time) {
        ++m_eligible_end_idx;
    }
    m_current_tracked_time = current_time;
}

void FCFSScheduler::mark_removed(job_no_t job_id) {
    // Mark job as removed - actual deletion deferred to garbage collection
    for (auto& entry : m_wait_queue) {
        if (entry.job_id == job_id && !entry.removed) {
            entry.removed = true;
            ++m_removed_count;
            return;
        }
    }
}

std::vector<job_no_t> FCFSScheduler::schedule(
    num_nodes_t free_nodes,
    const std::map<job_no_t, sim_time_t>& running_jobs,
    sim_time_t current_time)
{
    // Update eligibility boundary
    update_eligible_boundary(current_time);

    // Garbage collection: compact queue if >50% is garbage
    if (m_removed_count > 0 && m_removed_count * 2 > m_wait_queue.size()) {
        size_t eligible_removed = 0;

        // Remove all entries marked as removed
        auto new_end = std::remove_if(m_wait_queue.begin(), m_wait_queue.end(),
            [&eligible_removed, this](const JobEntry& entry) {
                if (entry.removed && &entry - &m_wait_queue.front() < m_eligible_end_idx) {
                    ++eligible_removed;
                }
                return entry.removed;
            });

        m_wait_queue.erase(new_end, m_wait_queue.end());
        m_eligible_end_idx -= eligible_removed;
        m_removed_count = 0;
    }

    if (m_eligible_end_idx == 0) {
        return {};  // No eligible jobs
    }

    std::vector<job_no_t> jobs_to_run;
    num_nodes_t available_nodes = free_nodes;

    // Find FCFS head (first non-removed eligible job)
    size_t fcfs_head_idx = 0;
    while (fcfs_head_idx < m_eligible_end_idx && m_wait_queue[fcfs_head_idx].removed) {
        ++fcfs_head_idx;
    }

    if (fcfs_head_idx >= m_eligible_end_idx) {
        return {};  // All eligible jobs removed
    }

    // Try to run FCFS head
    if (m_wait_queue[fcfs_head_idx].nodes_requested <= free_nodes) {
        jobs_to_run.push_back(m_wait_queue[fcfs_head_idx].job_id);
        mark_removed(m_wait_queue[fcfs_head_idx].job_id);
        return jobs_to_run;
    }

    // FCFS head can't fit - calculate reservation for backfilling
    m_fcfs_reservation_time = calculate_fcfs_reservation(
        m_wait_queue[fcfs_head_idx].nodes_requested, free_nodes, running_jobs, current_time);

    // Try backfilling with remaining eligible jobs
    for (size_t i = fcfs_head_idx + 1; i < m_eligible_end_idx; ++i) {
        const auto& job = m_wait_queue[i];
        if (job.removed) continue;

        // Check resource fit
        if (job.nodes_requested > available_nodes) continue;

        // Check backfill window
        // IMPORTANT: Use strict < (not <=) because resources don't become available
        // instantly when a job completes. If a backfill job would complete exactly
        // at the reservation time, the FCFS head must wait for that completion event
        // to be processed first, causing a delay. Therefore, backfill jobs must
        // complete BEFORE (not at) the reservation time.
        if (current_time + job.runtime_estimate < m_fcfs_reservation_time) {
            jobs_to_run.push_back(job.job_id);
            available_nodes -= job.nodes_requested;
            mark_removed(job.job_id);

            // Continue backfilling if resources available
            if (available_nodes == 0) break;
        }
    }

    return jobs_to_run;
}


} // namespace dr_evt
