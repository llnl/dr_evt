/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULER_FCFS_ALT_HPP
#define DR_EVT_SIM_SCHEDULER_FCFS_ALT_HPP

#include "sim/scheduler_base.hpp"
#include <map>
#include <set>

namespace dr_evt {

/**
 * Alternative FCFS scheduler implementation for differential testing
 *
 * Based on SJF scheduler framework but uses submit_time ordering instead of runtime.
 * This provides an independent implementation to verify scheduler_fcfs correctness.
 *
 * Key difference from scheduler_fcfs:
 * - Uses std::multimap ordered by submit_time (not deque)
 * - Different internal data structure for queue management
 * - Should produce IDENTICAL results to scheduler_fcfs
 *
 * Purpose: Differential testing - if both implementations produce same results,
 * high confidence in correctness of both.
 */
class FCFSAltScheduler : public SchedulerBase {
public:
    FCFSAltScheduler(num_nodes_t total_nodes,
                     const std::vector<Job_Record>& job_data,
                     BackfillPolicy backfill_policy,
                     RuntimeEstimateMode runtime_mode);

    void insert_job(job_no_t job_id,
                    sim_time_t submit_time,
                    tdiff_t runtime,
                    num_nodes_t nodes) override;

    std::vector<job_no_t> schedule(
        num_nodes_t free_nodes,
        const std::map<job_no_t, sim_time_t>& running_jobs,
        sim_time_t current_time) override;

    void sync_to(sim_time_t current_time) override {
        update_eligible_jobs(current_time);
    }

    size_t active_job_count() override {
        return m_eligible_jobs.size();
    }

    sim_time_t get_next_arrival_time() override {
        // m_current_tracked_time is exactly "whatever time this
        // scheduler was last synced to via sync_to()" - using it here
        // instead of a parameter is equivalent, given the caller has
        // already synced before calling this.
        sim_time_t next = std::numeric_limits<sim_time_t>::max();
        for (const auto& pair : m_wait_queue) {
            const JobEntry& entry = pair.second;
            if (entry.submit_time > m_current_tracked_time && entry.submit_time < next) {
                next = entry.submit_time;
            }
        }
        return next;
    }

    bool has_eligible_jobs() override {
        return !m_eligible_jobs.empty();
    }

protected:
    size_t wait_queue_size() const override {
        return m_wait_queue.size();
    }

private:
    struct JobEntry {
        job_no_t job_id;
        sim_time_t submit_time;
        tdiff_t runtime;
        num_nodes_t nodes;
        // No removed flag - see SJFScheduler for full reasoning.
    };

    // Ordered by submit_time (FCFS order), then by job_id for stability
    // KEY DIFFERENCE: SJF uses runtime, this uses submit_time
    std::multimap<sim_time_t, JobEntry> m_wait_queue;

    // Track eligible jobs (submit_time <= current_time)
    std::set<job_no_t> m_eligible_jobs;

    // Last tracked time for eligibility updates
    sim_time_t m_current_tracked_time;

    // Update eligible set as time advances
    void update_eligible_jobs(sim_time_t current_time);

    // Find FCFS head (first eligible job in submit_time order)
    std::multimap<sim_time_t, JobEntry>::iterator find_fcfs_head();
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_FCFS_ALT_HPP
