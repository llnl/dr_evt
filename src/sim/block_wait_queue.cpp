/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include "sim/block_wait_queue.hpp"
#include <algorithm>
#include <limits>

namespace dr_evt {

BlockWaitQueue::BlockWaitQueue(size_t block_size, bool immediate_erase)
    : m_block_size(block_size)
    , m_immediate_erase(immediate_erase)
    , m_current_block_idx(0)
    , m_needs_resort(false)
    , m_total_jobs(0)
    , m_active_count(0)
    , m_stats{}
{
}

void BlockWaitQueue::insert_job(job_no_t job_id,
                                sim_time_t submit_time,
                                tdiff_t runtime_estimate,
                                num_nodes_t nodes_requested)
{
    // Create new block if needed
    if (m_blocks.empty() || m_blocks[m_current_block_idx].block.size() >= m_block_size) {
        m_blocks.emplace_back();
        m_current_block_idx = m_blocks.size() - 1;
    }

    auto& current = m_blocks[m_current_block_idx];

    // Insert into block (push_back to sequential index)
    current.block.push_back({job_id, submit_time, runtime_estimate, nodes_requested, false});
    current.active_count++;
    current.metadata_dirty = true;
    m_needs_resort = true;

    // Track globally for O(1) removal
    m_job_to_block[job_id] = m_current_block_idx;

    m_total_jobs++;
    m_active_count++;
}

void BlockWaitQueue::mark_removed(job_no_t job_id)
{
    // O(1) lookup of which block contains this job
    auto block_it = m_job_to_block.find(job_id);
    if (block_it == m_job_to_block.end()) {
        return;  // Job not found (already removed or never existed)
    }

    size_t block_idx = block_it->second;
    auto& block_info = m_blocks[block_idx];

    // O(1) lookup within block by job_id
    auto& id_idx = block_info.block.get<by_job_id>();
    auto it = id_idx.find(job_id);

    if (it != id_idx.end()) {
        if (m_immediate_erase) {
            // Immediate deletion: erase from multi_index right away
            id_idx.erase(it);
        } else {
            // Lazy deletion: mark as removed, erase later during compaction
            id_idx.modify(it, [](JobEntry& entry) {
                entry.removed = true;
            });
        }

        block_info.active_count--;
        block_info.metadata_dirty = true;
        m_needs_resort = true;

        m_active_count--;

        // Remove from global map
        m_job_to_block.erase(block_it);

        // Delete block if all jobs are removed
        if (block_info.active_count == 0) {
            m_blocks.erase(m_blocks.begin() + block_idx);
            // Update m_current_block_idx if needed
            if (block_idx <= m_current_block_idx && m_current_block_idx > 0) {
                m_current_block_idx--;
            }
            // Update job_to_block mappings for blocks after the deleted one
            for (auto& mapping : m_job_to_block) {
                if (mapping.second > block_idx) {
                    mapping.second--;
                }
            }
        }
        // Compact block if mostly empty (< 25% active) - only needed for lazy deletion
        else if (!m_immediate_erase && block_info.active_count < m_block_size / 4) {
            compact_block(block_info);
        }
    }
}

std::optional<job_no_t> BlockWaitQueue::find_backfill_candidate(
    num_nodes_t available_nodes,
    sim_time_t current_time,
    sim_time_t reservation_time)
{
    // Sort blocks by min_runtime (only if queue modified since last search)
    if (m_needs_resort) {
        std::sort(m_blocks.begin(), m_blocks.end(),
            [](const BlockInfo& a, const BlockInfo& b) {
                return a.min_runtime < b.min_runtime;
            });
        m_needs_resort = false;
    }

    // Scan blocks in min_runtime order
    for (auto& block_info : m_blocks) {
        m_stats.blocks_checked++;

        // PRE-CHECK 1: Time constraint (block level)
        // If shortest job in this block doesn't fit window,
        // no remaining blocks will fit (sorted by min_runtime)
        tdiff_t min_runtime = block_info.get_min_runtime();
        if (current_time + min_runtime >= reservation_time) {
            m_stats.blocks_skipped_time++;
            break;  // Early termination!
        }

        // PRE-CHECK 2: Resource constraint (block level)
        // If smallest job in this block needs more resources than available,
        // no job in this block can run
        num_nodes_t min_nodes = block_info.get_min_nodes();
        if (min_nodes > available_nodes) {
            m_stats.blocks_skipped_resource++;
            continue;  // Skip this entire block
        }

        // Block passed pre-checks: scan sequentially in FCFS order
        auto& seq = block_info.block.get<0>();  // Sequential index
        for (const auto& job : seq) {
            m_stats.jobs_scanned++;

            if (job.removed) continue;

            // Check this specific job's resource constraint
            if (job.nodes_requested > available_nodes) continue;

            // Check this specific job's time constraint (backfill window)
            if (current_time + job.runtime_estimate < reservation_time) {
                return job.job_id;  // Found backfill candidate!
            }
        }
    }

    return std::nullopt;  // No candidate found
}

void BlockWaitQueue::BlockInfo::update_metadata()
{
    if (!metadata_dirty) return;

    auto& runtime_idx = block.get<by_runtime>();
    auto& nodes_idx = block.get<by_nodes>();

    // Find min runtime (first non-removed in runtime-sorted index)
    min_runtime = std::numeric_limits<tdiff_t>::max();
    for (const auto& job : runtime_idx) {
        if (!job.removed) {
            min_runtime = job.runtime_estimate;
            break;  // First non-removed is the minimum
        }
    }

    // Find min and max nodes (scan nodes-sorted index)
    min_nodes = std::numeric_limits<num_nodes_t>::max();
    max_nodes = 0;
    for (const auto& job : nodes_idx) {
        if (!job.removed) {
            if (min_nodes == std::numeric_limits<num_nodes_t>::max()) {
                // First non-removed is the minimum
                min_nodes = job.nodes_requested;
            }
            // Keep scanning to find max
            max_nodes = std::max(max_nodes, job.nodes_requested);
        }
    }

    // Handle empty block
    if (active_count == 0) {
        min_runtime = std::numeric_limits<tdiff_t>::max();
        min_nodes = std::numeric_limits<num_nodes_t>::max();
        max_nodes = 0;
    }

    metadata_dirty = false;
}

tdiff_t BlockWaitQueue::BlockInfo::get_min_runtime()
{
    if (metadata_dirty) update_metadata();
    return min_runtime;
}

num_nodes_t BlockWaitQueue::BlockInfo::get_min_nodes()
{
    if (metadata_dirty) update_metadata();
    return min_nodes;
}

void BlockWaitQueue::compact_block(BlockInfo& block_info)
{
    // Remove all jobs marked as removed
    auto& seq = block_info.block.get<0>();
    for (auto it = seq.begin(); it != seq.end(); ) {
        if (it->removed) {
            it = seq.erase(it);
        } else {
            ++it;
        }
    }

    block_info.metadata_dirty = true;
}

} // namespace dr_evt
