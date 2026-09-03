/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
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
 * Uses std::multimap ordered by run_time for priority,
 * plus tracking of eligibility boundary to avoid rescanning.
 */
class SJFScheduler : public SchedulerBase {
public:
    SJFScheduler(num_nodes_t total_nodes,
                 const std::vector<Job_Record>& job_data,
                 BackfillPolicy backfill_policy,
                 DurationEstimateMode duration_mode);

    void insert_job(job_no_t job_id,
                    sim_time_t submit_time,
                    tdiff_t run_time,
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
        tdiff_t run_time;
        num_nodes_t nodes;
        // No removed flag: schedule() erases scheduled entries from
        // m_wait_queue immediately via the iterator it already holds
        // (multimap erase-by-iterator is O(1), so there's no benefit to
        // lazy deletion the way there is for a sequential container).
        // m_eligible_jobs is the single source of truth for "still
        // waiting" - every read site that used to check .removed was
        // either already also checking m_eligible_jobs membership
        // (redundant), or checking it in a context where a removed
        // entry could never appear anyway (see the accompanying patch
        // notes for the specific reasoning at each site).
    };

    // Ordered by run_time (shortest first), then by job_id for stability
    std::multimap<tdiff_t, JobEntry> m_wait_queue;

    // Track eligible jobs (submit_time <= current_time)
    // Store as set of job_ids that became eligible
    std::set<job_no_t> m_eligible_jobs;

    // Last tracked time for eligibility updates
    sim_time_t m_current_tracked_time;

    // Update eligible set as time advances.
    void update_eligible_jobs(sim_time_t current_time);

    // Find FCFS head (earliest submit_time among eligible jobs)
    std::multimap<tdiff_t, JobEntry>::iterator find_fcfs_head();
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_SJF_HPP
