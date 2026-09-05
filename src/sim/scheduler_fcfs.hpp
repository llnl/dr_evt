/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULER_FCFS_HPP
#define DR_EVT_SIM_SCHEDULER_FCFS_HPP

#include <deque>
#include "sim/scheduler_base.hpp"

namespace dr_evt {

/**
 * FCFS (First-Come-First-Served) scheduler with iterator tracking.
 *
 * Key optimization: Maintains iterator marking boundary between
 * "eligible" (submit_time <= current_time) and "not yet eligible" jobs.
 *
 * No rescanning or submit_time comparison in schedule().
 */
class FCFSScheduler : public SchedulerBase {
private:
    struct JobEntry {
        job_no_t job_id;
        sim_time_t submit_time;
        tdiff_t run_time_estimate;
        num_nodes_t nodes_requested;
        bool removed;

        JobEntry(job_no_t id, sim_time_t submit, tdiff_t run_time, num_nodes_t nodes)
            : job_id(id), submit_time(submit), run_time_estimate(run_time),
              nodes_requested(nodes), removed(false) {}
    };

    std::deque<JobEntry> m_wait_queue;
    size_t m_eligible_end_idx;  // Index of first job NOT eligible yet
    sim_time_t m_current_tracked_time;
    size_t m_removed_count;  // Track garbage for collection

public:
    FCFSScheduler(num_nodes_t total_nodes,
                  const std::vector<Job_Record>& job_data,
                  BackfillPolicy bf_policy)
        : SchedulerBase(total_nodes, job_data, bf_policy)
        , m_eligible_end_idx(0)
        , m_current_tracked_time(0.0)
        , m_removed_count(0)
    {}

    void insert_job(job_no_t job_id, sim_time_t submit_time,
                   tdiff_t run_time_estimate, num_nodes_t nodes_requested) override {
        m_wait_queue.emplace_back(job_id, submit_time, run_time_estimate, nodes_requested);

        // If this job is already eligible, advance index. Can jump by more
        // than 1 in a single call: if this new job's submit_time is
        // already <= current time, the sorted-submit-time invariant means
        // every entry already in the deque becomes eligible too.
        if (submit_time <= m_current_tracked_time) {
            m_eligible_end_idx = m_wait_queue.size();
        }
    }

    std::vector<job_no_t> schedule(
        num_nodes_t free_nodes,
        const std::map<job_no_t, sim_time_t>& running_jobs,
        sim_time_t current_time) override;

    /**
     * Advance m_eligible_end_idx to reflect current_time. Idempotent and
     * cheap to call redundantly (see body: early-returns if current_time
     * hasn't advanced past what's already tracked). schedule() calls
     * this unconditionally at its own top, so it stays correct
     * regardless of whether an external caller already synced.
     */
    void sync_to(sim_time_t current_time) override;

    /**
     * Count WAITING jobs (arrived but not scheduled), as of whatever
     * time this scheduler was last synced to via sync_to(). Derived, not
     * scanned: every removed entry lives within [0, m_eligible_end_idx)
     * by construction (removal only ever happens to entries that are
     * already eligible - see schedule() and mark_removed()), so
     * "eligible minus removed" is exactly "waiting".
     *
     * Invariants:
     * - m_wait_queue is sorted by submit_time (jobs inserted from sorted trace)
     * - m_eligible_end_idx advances monotonically as time advances
     * - removed flag is set either by mark_removed(), or directly within
     *   schedule() at call sites where the index is already known (to
     *   avoid mark_removed()'s O(n) linear re-scan by job id)
     */
    size_t active_job_count() override {
        return m_eligible_end_idx - m_removed_count;
    }

    sim_time_t get_next_arrival_time() override {
        // Next arrival is at m_eligible_end_idx or later
        for (size_t i = m_eligible_end_idx; i < m_wait_queue.size(); ++i) {
            if (!m_wait_queue[i].removed) {
                return m_wait_queue[i].submit_time;
            }
        }
        return std::numeric_limits<sim_time_t>::max();
    }

    bool has_eligible_jobs() override {
        return active_job_count() > 0;
    }

protected:
    /**
     * Return total size of wait queue (ALL jobs, all states).
     * Includes future arrivals, waiting jobs, and scheduled (removed) jobs.
     * Internal utility only - use active_job_count() externally.
     */
    size_t wait_queue_size() const override {
        return m_wait_queue.size();
    }

private:
    void mark_removed(job_no_t job_id);
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_FCFS_HPP
