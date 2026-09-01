/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include "sim/scheduler_ljf.hpp"
#include <algorithm>

namespace dr_evt {

LJFScheduler::LJFScheduler(num_nodes_t total_nodes,
                           const std::vector<Job_Record>& job_data,
                           BackfillPolicy backfill_policy,
                           RuntimeEstimateMode runtime_mode)
    : SchedulerBase(total_nodes, job_data, backfill_policy, runtime_mode),
      m_current_tracked_time(0.0)
{
}

void LJFScheduler::insert_job(job_no_t job_id,
                               sim_time_t submit_time,
                               tdiff_t runtime,
                               num_nodes_t nodes)
{
    JobEntry entry{job_id, submit_time, runtime, nodes};
    m_wait_queue.insert({runtime, entry});

    // If already eligible, add to eligible set
    if (submit_time <= m_current_tracked_time) {
        m_eligible_jobs.insert(job_id);
    }
}

void LJFScheduler::update_eligible_jobs(sim_time_t current_time)
{
    if (current_time <= m_current_tracked_time) {
        return;  // Time hasn't advanced
    }

    // Scan wait_queue for newly eligible jobs
    for (auto& pair : m_wait_queue) {
        const JobEntry& entry = pair.second;
        if (entry.submit_time <= current_time && entry.submit_time > m_current_tracked_time) {
            m_eligible_jobs.insert(entry.job_id);
        }
    }

    m_current_tracked_time = current_time;
}

std::multimap<tdiff_t, LJFScheduler::JobEntry, LJFScheduler::DescendingRuntime>::iterator
LJFScheduler::find_fcfs_head()
{
    // FCFS head = eligible job with earliest submit_time
    auto fcfs_it = m_wait_queue.end();
    sim_time_t earliest_submit = std::numeric_limits<sim_time_t>::max();

    for (auto it = m_wait_queue.begin(); it != m_wait_queue.end(); ++it) {
        const JobEntry& entry = it->second;
        if (m_eligible_jobs.count(entry.job_id) == 0) continue;

        if (entry.submit_time < earliest_submit) {
            earliest_submit = entry.submit_time;
            fcfs_it = it;
        }
    }

    return fcfs_it;
}

std::vector<job_no_t> LJFScheduler::schedule(
    num_nodes_t free_nodes,
    const std::map<job_no_t, sim_time_t>& running_jobs,
    sim_time_t current_time)
{
    // Update eligibility tracking
    update_eligible_jobs(current_time);

    if (m_wait_queue.empty() || m_eligible_jobs.empty()) {
        return {};
    }

    // Find FCFS head (for reservation time calculation)
    auto fcfs_head_it = find_fcfs_head();
    if (fcfs_head_it == m_wait_queue.end()) {
        return {};  // No eligible jobs
    }

    // Copy out what's needed before any erase invalidates this iterator
    // and the reference into it.
    job_no_t head_job_id = fcfs_head_it->second.job_id;
    num_nodes_t head_nodes = fcfs_head_it->second.nodes;

    // Check if FCFS head can run
    if (head_nodes <= free_nodes) {
        // Start FCFS head - erase immediately rather than lazily marking
        // (see SJFScheduler for full reasoning: multimap erase-by-iterator
        // is O(1) amortized regardless of tree position).
        m_eligible_jobs.erase(head_job_id);
        m_wait_queue.erase(fcfs_head_it);
        return {head_job_id};
    }

    // FCFS head blocked - try backfilling
    // Calculate FCFS reservation time
    sim_time_t reservation_time = calculate_fcfs_reservation(
        head_nodes, free_nodes, running_jobs, current_time);

    if (reservation_time <= current_time) {
        return {};  // No valid reservation window
    }

    tdiff_t backfill_window = reservation_time - current_time;

    // Scan in LJF order (longest first) for backfill candidates
    for (auto it = m_wait_queue.begin(); it != m_wait_queue.end(); ++it) {
        if (it == fcfs_head_it) continue;  // Skip FCFS head

        const JobEntry& entry = it->second;
        if (m_eligible_jobs.count(entry.job_id) == 0) continue;

        // Check backfill constraints
        bool fits_nodes = (entry.nodes <= free_nodes);
        bool fits_window = (entry.runtime <= backfill_window);

        if (m_backfill_policy == BackfillPolicy::EASY) {
            // EASY: must fit both nodes AND window
            if (fits_nodes && fits_window) {
                // Found backfill candidate - copy id out before erasing,
                // same reasoning as the head case above.
                job_no_t id = entry.job_id;
                m_eligible_jobs.erase(id);
                m_wait_queue.erase(it);
                return {id};
            }
        } else if (m_backfill_policy == BackfillPolicy::CONSERVATIVE) {
            // CONSERVATIVE: must not delay ANY waiting job
            // For now, only allow if fits window (conservative approximation)
            if (fits_nodes && fits_window) {
                job_no_t id = entry.job_id;
                m_eligible_jobs.erase(id);
                m_wait_queue.erase(it);
                return {id};
            }
        }
    }

    return {};  // No backfill candidates found
}


} // namespace dr_evt
