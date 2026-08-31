/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include "sim/scheduler_fcfs_alt.hpp"
#include <algorithm>

namespace dr_evt {

FCFSAltScheduler::FCFSAltScheduler(num_nodes_t total_nodes,
                                   const std::vector<Job_Record>& job_data,
                                   BackfillPolicy backfill_policy,
                                   RuntimeEstimateMode runtime_mode)
    : SchedulerBase(total_nodes, job_data, backfill_policy, runtime_mode),
      m_current_tracked_time(0.0)
{
}

void FCFSAltScheduler::insert_job(job_no_t job_id,
                                   sim_time_t submit_time,
                                   tdiff_t runtime,
                                   num_nodes_t nodes)
{
    JobEntry entry{job_id, submit_time, runtime, nodes};

    // KEY DIFFERENCE: Order by submit_time (FCFS) instead of runtime (SJF)
    m_wait_queue.insert({submit_time, entry});

    // If already eligible, add to eligible set
    if (submit_time <= m_current_tracked_time) {
        m_eligible_jobs.insert(job_id);
    }
}

void FCFSAltScheduler::update_eligible_jobs(sim_time_t current_time)
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

std::multimap<sim_time_t, FCFSAltScheduler::JobEntry>::iterator FCFSAltScheduler::find_fcfs_head()
{
    // FCFS head = first eligible job in submit_time order
    // Since multimap is ordered by submit_time, just find first eligible
    for (auto it = m_wait_queue.begin(); it != m_wait_queue.end(); ++it) {
        const JobEntry& entry = it->second;
        if (m_eligible_jobs.count(entry.job_id) == 0) continue;

        // Found first eligible job (earliest submit_time)
        return it;
    }

    return m_wait_queue.end();
}

std::vector<job_no_t> FCFSAltScheduler::schedule(
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
        // (see SJFScheduler for full reasoning).
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

    // Scan in FCFS order (earliest submit_time first) for backfill candidates
    for (auto it = m_wait_queue.begin(); it != m_wait_queue.end(); ++it) {
        if (it == fcfs_head_it) continue;  // Skip FCFS head

        const JobEntry& entry = it->second;
        if (m_eligible_jobs.count(entry.job_id) == 0) continue;

        // Check backfill constraints
        bool fits_nodes = (entry.nodes <= free_nodes);

        // IMPORTANT: Use strict < (not <=) because resources don't become available
        // instantly when a job completes. If a backfill job would complete exactly
        // at the reservation time, the FCFS head must wait for that completion event
        // to be processed first, causing a delay. Therefore, backfill jobs must
        // complete BEFORE (not at) the reservation time.
        bool fits_window = (entry.runtime < backfill_window);

        if (m_backfill_policy == BackfillPolicy::EASY) {
            // EASY: must fit both nodes AND window
            if (fits_nodes && fits_window) {
                // Found backfill candidate - copy id out before erasing.
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
