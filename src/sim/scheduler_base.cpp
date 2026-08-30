/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include "sim/scheduler_base.hpp"
#include "sim/scheduler_fcfs.hpp"
#include "sim/scheduler_sjf.hpp"
#include "sim/scheduler_ljf.hpp"
#include <algorithm>

namespace dr_evt {

sim_time_t SchedulerBase::calculate_fcfs_reservation(
    num_nodes_t nodes_needed,
    num_nodes_t free_nodes,
    const std::map<job_no_t, sim_time_t>& running_jobs,
    sim_time_t current_time)
{
    if (nodes_needed <= free_nodes) {
        return current_time;  // Can start now
    }

    num_nodes_t nodes_deficit = nodes_needed - free_nodes;

    // Collect end times of running jobs with their node counts
    std::vector<std::pair<sim_time_t, num_nodes_t>> end_events;
    end_events.reserve(running_jobs.size());

    for (const auto& [job_idx, start_time] : running_jobs) {
        const auto& job = (*m_job_data_ptr)[job_idx];
        tdiff_t runtime = get_runtime_estimate(job_idx);
        sim_time_t end_time = start_time + runtime;
        num_nodes_t nodes = job.get_num_nodes();

        if (end_time > current_time) {
            end_events.push_back({end_time, nodes});
        }
    }

    // Sort by end time
    std::sort(end_events.begin(), end_events.end());

    // Accumulate freed nodes until we have enough
    num_nodes_t freed_nodes = 0;
    for (const auto& [end_time, nodes] : end_events) {
        freed_nodes += nodes;
        if (freed_nodes >= nodes_deficit) {
            return end_time;
        }
    }

    // Not enough nodes will be freed (shouldn't happen in correct usage)
    return current_time;
}

std::unique_ptr<SchedulerBase> create_scheduler(
    num_nodes_t total_nodes,
    const std::vector<Job_Record>& job_data,
    BackfillPolicy backfill_policy,
    PriorityPolicy priority_policy,
    RuntimeEstimateMode runtime_mode)
{
    switch (priority_policy) {
        case PriorityPolicy::FCFS:
            return std::make_unique<FCFSScheduler>(
                total_nodes, job_data, backfill_policy, runtime_mode);

        case PriorityPolicy::SJF:
            return std::make_unique<SJFScheduler>(
                total_nodes, job_data, backfill_policy, runtime_mode);

        case PriorityPolicy::LJF:
            return std::make_unique<LJFScheduler>(
                total_nodes, job_data, backfill_policy, runtime_mode);

        default:
            // Default to FCFS
            return std::make_unique<FCFSScheduler>(
                total_nodes, job_data, backfill_policy, runtime_mode);
    }
}

} // namespace dr_evt
