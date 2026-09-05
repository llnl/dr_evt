/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULER_FCFS_CONSERVATIVE_HPP
#define DR_EVT_SIM_SCHEDULER_FCFS_CONSERVATIVE_HPP

#include <deque>
#include <vector>
#include "sim/scheduler_base.hpp"

namespace dr_evt {

/**
 * FCFS scheduler with Conservative backfilling support.
 *
 * Supports three backfill modes via BackfillPolicy:
 * - EASY: Only head job gets reservation (standard EASY backfilling)
 * - CONSERVATIVE: All queued jobs get reservations; backfill cannot delay ANY job
 * - NONE: No backfilling (strict FCFS order)
 *
 * Based on FCFSScheduler with the same deque-based iterator tracking optimization.
 *
 * NOTE: Currently only std::deque implementation exists. For optimal performance,
 * a CircularBufferFCFSConservativeScheduler should be implemented (similar to
 * CircularBufferFCFSScheduler) as circular buffer provides ~10-15% better
 * performance than deque for FCFS scheduling.
 */
class FCFSConservativeScheduler : public SchedulerBase {
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
    FCFSConservativeScheduler(num_nodes_t total_nodes,
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

        if (submit_time <= m_current_tracked_time) {
            m_eligible_end_idx = m_wait_queue.size();
        }
    }

    std::vector<job_no_t> schedule(
        num_nodes_t free_nodes,
        const std::map<job_no_t, sim_time_t>& running_jobs,
        sim_time_t current_time) override;

    void sync_to(sim_time_t current_time) override;

    size_t active_job_count() override {
        return m_eligible_end_idx - m_removed_count;
    }

    sim_time_t get_next_arrival_time() override {
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
    size_t wait_queue_size() const override {
        return m_wait_queue.size();
    }

private:
    void mark_removed(job_no_t job_id);

    /**
     * Calculate conservative backfill window for a specific job.
     * Returns the earliest reservation time among all waiting jobs ahead of it.
     */
    sim_time_t calculate_conservative_window(
        size_t job_index,
        num_nodes_t available_nodes,
        const std::map<job_no_t, sim_time_t>& running_jobs,
        sim_time_t current_time);
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_FCFS_CONSERVATIVE_HPP
