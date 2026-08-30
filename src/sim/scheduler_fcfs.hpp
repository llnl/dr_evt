/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
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
        tdiff_t runtime_estimate;
        num_nodes_t nodes_requested;
        bool removed;

        JobEntry(job_no_t id, sim_time_t submit, tdiff_t runtime, num_nodes_t nodes)
            : job_id(id), submit_time(submit), runtime_estimate(runtime),
              nodes_requested(nodes), removed(false) {}
    };

    std::deque<JobEntry> m_wait_queue;
    size_t m_eligible_end_idx;  // Index of first job NOT eligible yet
    mutable sim_time_t m_current_tracked_time;  // Mutable so we can update in const functions
    size_t m_removed_count;  // Track garbage for collection

public:
    FCFSScheduler(num_nodes_t total_nodes,
                  const std::vector<Job_Record>& job_data,
                  BackfillPolicy bf_policy,
                  RuntimeEstimateMode rt_mode)
        : SchedulerBase(total_nodes, job_data, bf_policy, rt_mode)
        , m_eligible_end_idx(0)
        , m_current_tracked_time(0.0)
        , m_removed_count(0)
    {}

    void insert_job(job_no_t job_id, sim_time_t submit_time,
                   tdiff_t runtime_estimate, num_nodes_t nodes_requested) override {
        m_wait_queue.emplace_back(job_id, submit_time, runtime_estimate, nodes_requested);

        // If this job is already eligible, advance index
        if (submit_time <= m_current_tracked_time) {
            m_eligible_end_idx = m_wait_queue.size();
        }
    }

    std::vector<job_no_t> schedule(
        num_nodes_t free_nodes,
        const std::map<job_no_t, sim_time_t>& running_jobs,
        sim_time_t current_time) override;

    /**
     * Return total size of wait queue (ALL jobs, all states).
     * Includes future arrivals, waiting jobs, and scheduled (removed) jobs.
     */
    size_t wait_queue_size() const override {
        return m_wait_queue.size();
    }

    /**
     * Count WAITING jobs (arrived but not scheduled).
     *
     * Returns: count of jobs where submit_time <= current_time AND !removed
     *
     * Implementation:
     * 1. Update m_eligible_end_idx if current_time advanced (via update_time)
     *    - m_eligible_end_idx = first index where submit_time > current_time
     *    - All jobs in [0, m_eligible_end_idx) have arrived
     * 2. Count non-removed jobs in [0, m_eligible_end_idx)
     *
     * Invariants:
     * - m_wait_queue is sorted by submit_time (jobs inserted from sorted trace)
     * - m_eligible_end_idx advances monotonically as time advances
     * - removed flag set only by mark_removed() when job is scheduled
     */
    size_t active_job_count(sim_time_t current_time) const override {
        // Update boundary if time advanced
        update_time(current_time);

        // Count non-removed jobs before m_eligible_end_idx
        size_t count = 0;
        for (size_t i = 0; i < m_eligible_end_idx && i < m_wait_queue.size(); ++i) {
            if (!m_wait_queue[i].removed) {
                count++;
            }
        }

        return count;
    }

    sim_time_t get_next_arrival_time(sim_time_t current_time) const override {
        // Update boundary if time advanced
        update_time(current_time);

        // Next arrival is at m_eligible_end_idx or later
        for (size_t i = m_eligible_end_idx; i < m_wait_queue.size(); ++i) {
            if (!m_wait_queue[i].removed) {
                return m_wait_queue[i].submit_time;
            }
        }
        return std::numeric_limits<sim_time_t>::max();
    }

    bool has_eligible_jobs(sim_time_t current_time) const override {
        // Update boundary if time advanced
        update_time(current_time);

        // Check if any non-removed job before m_eligible_end_idx
        for (size_t i = 0; i < m_eligible_end_idx && i < m_wait_queue.size(); ++i) {
            if (!m_wait_queue[i].removed) {
                return true;
            }
        }
        return false;
    }

private:
    void update_eligible_boundary(sim_time_t current_time);
    void mark_removed(job_no_t job_id);

    // Update tracked time and boundary - called from const functions
    void update_time(sim_time_t current_time) const {
        if (current_time > m_current_tracked_time) {
            // Time advanced - update boundary
            // Cast away const since we're updating cached state
            const_cast<FCFSScheduler*>(this)->update_eligible_boundary(current_time);
        }
    }
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_FCFS_HPP
