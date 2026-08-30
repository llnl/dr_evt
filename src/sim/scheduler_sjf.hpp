/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULER_SJF_HPP
#define DR_EVT_SIM_SCHEDULER_SJF_HPP

#include "sim/scheduler_base.hpp"
#include <map>
#include <set>

namespace dr_evt {

/**
 * Shortest Job First (SJF) scheduler with iterator tracking
 *
 * Uses std::multimap ordered by runtime for priority,
 * plus tracking of eligibility boundary to avoid rescanning.
 */
class SJFScheduler : public SchedulerBase {
public:
    SJFScheduler(num_nodes_t total_nodes,
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

    size_t wait_queue_size() const override {
        return m_wait_queue.size();
    }

    size_t active_job_count(sim_time_t current_time) const override {
        // Count non-removed jobs that have arrived
        size_t count = 0;
        for (const auto& pair : m_wait_queue) {
            const JobEntry& entry = pair.second;
            if (!entry.removed && entry.submit_time <= current_time) {
                count++;
            }
        }
        return count;
    }

    sim_time_t get_next_arrival_time(sim_time_t current_time) const override {
        sim_time_t next = std::numeric_limits<sim_time_t>::max();
        for (const auto& pair : m_wait_queue) {
            const JobEntry& entry = pair.second;
            if (entry.removed) continue;
            if (entry.submit_time > current_time && entry.submit_time < next) {
                next = entry.submit_time;
            }
        }
        return next;
    }

    bool has_eligible_jobs(sim_time_t current_time) const override {
        return !m_eligible_jobs.empty();
    }

private:
    struct JobEntry {
        job_no_t job_id;
        sim_time_t submit_time;
        tdiff_t runtime;
        num_nodes_t nodes;
        bool removed;
    };

    // Ordered by runtime (shortest first), then by job_id for stability
    std::multimap<tdiff_t, JobEntry> m_wait_queue;

    // Track eligible jobs (submit_time <= current_time)
    // Store as set of job_ids that became eligible
    std::set<job_no_t> m_eligible_jobs;

    // Last tracked time for eligibility updates
    sim_time_t m_current_tracked_time;

    // Update eligible set as time advances
    void update_eligible_jobs(sim_time_t current_time);

    // Find FCFS head (earliest submit_time among eligible jobs)
    std::multimap<tdiff_t, JobEntry>::iterator find_fcfs_head();
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_SJF_HPP
