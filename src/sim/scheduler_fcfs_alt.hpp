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
 * Based on SJF scheduler framework but uses submit_time ordering instead of run_time.
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
    /** @copydoc SchedulerBase::SchedulerBase */
    FCFSAltScheduler(num_nodes_t total_nodes,
                     const Trace& job_data,
                     BackfillPolicy backfill_policy);

    /** @copydoc SchedulerBase::insert_job */
    void insert_job(job_no_t job_id,
                    sim_time_t submit_time,
                    tdiff_t run_time,
                    num_nodes_t nodes) override;

    /** @copydoc SchedulerBase::schedule */
    std::vector<job_no_t> schedule(
        num_nodes_t free_nodes,
        const std::map<job_no_t, sim_time_t>& running_jobs,
        sim_time_t current_time) override;

    /** @copydoc SchedulerBase::sync_to */
    void sync_to(sim_time_t current_time) override {
        update_eligible_jobs(current_time);
    }

    /** @copydoc SchedulerBase::active_job_count */
    size_t active_job_count() override {
        return m_eligible_jobs.size();
    }

    /** @copydoc SchedulerBase::get_next_arrival_time */
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

    /** @copydoc SchedulerBase::has_eligible_jobs */
    bool has_eligible_jobs() override {
        return !m_eligible_jobs.empty();
    }

protected:
    /** @copydoc SchedulerBase::wait_queue_size */
    size_t wait_queue_size() const override {
        return m_wait_queue.size();
    }

private:
    struct JobEntry {
        /// Stable identifier of the Trace job represented by this entry.
        job_no_t job_id;
        /// Arrival time; also the alternative implementation's priority key.
        sim_time_t submit_time;
        /// Requested time limit used for reservation and backfill projection.
        tdiff_t run_time;
        /// Nodes requested when this job is started.
        num_nodes_t nodes;
        // No removed flag - see SJFScheduler for full reasoning.
    };

    /// Jobs ordered by arrival time, then stable Trace job identifier.
    std::multimap<sim_time_t, JobEntry> m_wait_queue;

    /// Identifiers of arrived jobs still waiting to be scheduled.
    std::set<job_no_t> m_eligible_jobs;

    /// Latest time to which arrival eligibility has been synchronized.
    sim_time_t m_current_tracked_time;

    /** @brief Add newly arrived jobs to the eligibility set. @param[in] current_time New scheduler time. */
    void update_eligible_jobs(sim_time_t current_time);

    /** @brief Find the earliest-arriving eligible job. @return Iterator to the FCFS head. */
    std::multimap<sim_time_t, JobEntry>::iterator find_fcfs_head();
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_FCFS_ALT_HPP
