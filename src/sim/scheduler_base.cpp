/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include "sim/scheduler_base.hpp"
#include "sim/scheduler_fcfs.hpp"
#include "sim/scheduler_fcfs_alt.hpp"
#include "sim/scheduler_block_fcfs.hpp"
#include "sim/scheduler_sjf.hpp"
#include "sim/scheduler_ljf.hpp"
#include <algorithm>
#include <iostream>
#include <functional>
#include <unordered_map>

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
    RuntimeEstimateMode runtime_mode,
    QueueImplementation queue_impl,
    size_t block_size)
{
    switch (priority_policy) {
        case PriorityPolicy::FCFS:
            // FCFS has 3 queue options
            if (queue_impl == QueueImplementation::BLOCK) {
                // Validate block_size is power of 2
                if (block_size == 0 || (block_size & (block_size - 1)) != 0) {
                    std::cerr << "Error: block_size must be a power of 2\n";
                    std::exit(1);
                }

                // Compute log2 and use lookup table
                size_t log2_size = 0;
                size_t temp = block_size;
                while (temp > 1) {
                    temp >>= 1;
                    log2_size++;
                }

                // Verify: (1 << log2_size) == block_size
                if ((1ULL << log2_size) != block_size) {
                    std::cerr << "Error: block_size verification failed\n";
                    std::exit(1);
                }

                // Factory function table indexed by log2(block_size)
                using FactoryFunc = std::function<std::unique_ptr<SchedulerBase>()>;
                static const std::unordered_map<size_t, FactoryFunc> factories = {
                    {2, [&]() { return std::make_unique<BlockQueueFCFSScheduler<4>>(
                        total_nodes, job_data, backfill_policy, runtime_mode); }},
                    {3, [&]() { return std::make_unique<BlockQueueFCFSScheduler<8>>(
                        total_nodes, job_data, backfill_policy, runtime_mode); }},
                    {4, [&]() { return std::make_unique<BlockQueueFCFSScheduler<16>>(
                        total_nodes, job_data, backfill_policy, runtime_mode); }},
                    {5, [&]() { return std::make_unique<BlockQueueFCFSScheduler<32>>(
                        total_nodes, job_data, backfill_policy, runtime_mode); }},
                    {6, [&]() { return std::make_unique<BlockQueueFCFSScheduler<64>>(
                        total_nodes, job_data, backfill_policy, runtime_mode); }},
                    {7, [&]() { return std::make_unique<BlockQueueFCFSScheduler<128>>(
                        total_nodes, job_data, backfill_policy, runtime_mode); }},
                    {8, [&]() { return std::make_unique<BlockQueueFCFSScheduler<256>>(
                        total_nodes, job_data, backfill_policy, runtime_mode); }},
                };

                auto it = factories.find(log2_size);
                if (it == factories.end()) {
                    std::cerr << "Error: block_size " << block_size
                              << " not supported. Use 4, 8, 16, 32, 64, 128, or 256.\n";
                    std::exit(1);
                }

                return it->second();
            } else if (queue_impl == QueueImplementation::MULTIMAP) {
                return std::make_unique<FCFSAltScheduler>(
                    total_nodes, job_data, backfill_policy, runtime_mode);
            } else {
                return std::make_unique<FCFSScheduler>(
                    total_nodes, job_data, backfill_policy, runtime_mode);
            }

        case PriorityPolicy::FCFS_ALT:
            // FCFS_ALT always uses multimap
            if (queue_impl == QueueImplementation::BLOCK) {
                std::cerr << "Warning: Block queue not supported for FCFS_ALT, using multimap\n";
            }
            return std::make_unique<FCFSAltScheduler>(
                total_nodes, job_data, backfill_policy, runtime_mode);

        case PriorityPolicy::SJF:
            // SJF only uses multimap (already efficient)
            if (queue_impl != QueueImplementation::DEQUE) {
                std::cerr << "Warning: queue_impl '"
                         << (queue_impl == QueueImplementation::BLOCK ? "block" : "multimap")
                         << "' not supported for SJF, using default multimap\n";
            }
            return std::make_unique<SJFScheduler>(
                total_nodes, job_data, backfill_policy, runtime_mode);

        case PriorityPolicy::LJF:
            // LJF only uses multimap (already efficient)
            if (queue_impl != QueueImplementation::DEQUE) {
                std::cerr << "Warning: queue_impl '"
                         << (queue_impl == QueueImplementation::BLOCK ? "block" : "multimap")
                         << "' not supported for LJF, using default multimap\n";
            }
            return std::make_unique<LJFScheduler>(
                total_nodes, job_data, backfill_policy, runtime_mode);

        default:
            // Default to FCFS with deque
            return std::make_unique<FCFSScheduler>(
                total_nodes, job_data, backfill_policy, runtime_mode);
    }
}

} // namespace dr_evt
