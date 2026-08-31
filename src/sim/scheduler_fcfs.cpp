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

void FCFSScheduler::sync_to(sim_time_t current_time) {
    if (current_time <= m_current_tracked_time) {
        return;  // Time hasn't advanced - nothing to do, and skips
                 // unconditionally setting m_current_tracked_time below,
                 // which would otherwise regress it backward.
    }

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
    // Sync eligibility tracking - unconditional, so schedule() stays
    // correct regardless of whether an external caller already synced
    // (redundant call is cheap, see sync_to()'s own early-return).
    sync_to(current_time);

    // Garbage collection: compact queue if >50% is garbage
    if (m_removed_count > 0 && m_removed_count * 2 > m_wait_queue.size()) {
        // Count removed entries within the eligible range using indexed
        // access (deque::operator[] is well-defined random access).
        // NOTE: this used to be computed inside the remove_if predicate via
        // `&entry - &m_wait_queue.front()`, but pointer subtraction between
        // deque elements is undefined behavior once they span more than
        // one of the deque's internal chunks (deque does not guarantee
        // contiguous storage the way vector does) - confirmed empirically
        // to silently produce garbage index values for any real-sized
        // queue, which under-counted eligible_removed and left
        // m_eligible_end_idx too large after compaction, misclassifying
        // not-yet-arrived jobs as eligible.
        size_t eligible_removed = 0;
        for (size_t idx = 0; idx < m_eligible_end_idx && idx < m_wait_queue.size(); ++idx) {
            if (m_wait_queue[idx].removed) {
                ++eligible_removed;
            }
        }

        // Remove all entries marked as removed. remove_if itself is safe
        // on a deque - it advances via the container's own iterators,
        // which correctly handle chunk boundaries internally.
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
    // (O(1) on a deque) instead of scanning by index. Two cases land here:
    //  (a) a stale "removed" entry that reached the front because the
    //      legitimate jobs ahead of it (once at lower indices) already
    //      got popped in an earlier iteration of this same loop, or in a
    //      previous call to schedule() -- these were marked removed by
    //      backfilling at some non-front position and are just waiting to
    //      be physically dropped;
    //  (b) a genuine, not-yet-removed head that fits right now.
    // Both cases end in a pop_front(); only (b) also starts a job. The
    // *new* front is re-checked the same way after each pop -- it must NOT
    // be evaluated as a backfill candidate against a reservation computed
    // before earlier heads started.
    while (m_eligible_end_idx > 0 && !m_wait_queue.empty() &&
           (m_wait_queue.front().removed ||
            m_wait_queue.front().nodes_requested <= available_nodes)) {
        if (!m_wait_queue.front().removed) {
            jobs_to_run.push_back(m_wait_queue.front().job_id);
            available_nodes -= m_wait_queue.front().nodes_requested;
        } else {
            // Stale placeholder from an earlier backfill - it was already
            // counted in m_removed_count when marked; now that it's
            // physically gone, keep that count in sync.
            --m_removed_count;
        }
        m_wait_queue.pop_front();
        --m_eligible_end_idx;
    }

    if (active_job_count() == 0) {
        // Nothing left to consider for backfill. schedule() already
        // synced eligibility at its own top (see sync_to() call above).
        return jobs_to_run;
    }

    if (m_backfill_policy == BackfillPolicy::NONE) {
        // Backfilling disabled - the blocked FCFS head is the only thing
        // that matters; no other job may start ahead of it regardless of
        // fit or timing. Skip the reservation computation and full-queue
        // scan below entirely, since neither can ever be acted on.
        return jobs_to_run;
    }

    // FCFS head is now genuinely at the front and blocked - calculate
    // reservation for backfilling.
    // Use available_nodes (not the original free_nodes parameter): if any
    // heads started in the loop above, available_nodes correctly reflects
    // the resources they consumed, which the original single-pickup code
    // never needed to account for since it always returned after at most
    // one start.
    //
    // running_jobs (passed in by Simulation) does NOT yet include jobs
    // that were just started in Step 1's cascading loop above -
    // Simulation only records a job into its own running-jobs map AFTER
    // this schedule() call returns the full jobs_to_run list. If we pass
    // running_jobs through unmodified, a job like that is invisible to
    // calculate_fcfs_reservation: its completion time can't contribute
    // to "when will enough capacity free up" at all, which can push the
    // computed reservation far later than the true one - incorrectly
    // letting some other backfill candidate occupy capacity the blocked
    // head actually needs sooner than the wrong reservation implies.
    // Building this snapshot is only ever needed here, right before the
    // one reservation calculation per schedule() call, not for every
    // query - it's a local, one-time correction of the caller-supplied
    // map, not a change to how running jobs are tracked in general.
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
            // Index already known here - set the flag directly instead of
            // going through mark_removed(job_id), which would redo an O(n)
            // linear scan to re-find this same entry by id.
            m_wait_queue[i].removed = true;
            ++m_removed_count;

            // Continue backfilling if resources available
            if (available_nodes == 0) break;
        }
    }

    return jobs_to_run;
}


} // namespace dr_evt
