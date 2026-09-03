/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include "sim/scheduler_block_fcfs.hpp"
#include <algorithm>

namespace dr_evt {

template<size_t BlockSize>
void BlockQueueFCFSScheduler<BlockSize>::insert_job(job_no_t job_id, sim_time_t submit_time,
                                         tdiff_t run_time_estimate, num_nodes_t nodes_requested) {
    m_wait_queue.insert_job(job_id, submit_time, run_time_estimate, nodes_requested);
    m_job_order.emplace_back(job_id, submit_time);

    if (submit_time <= m_current_tracked_time) {
        m_eligible_end_idx = m_job_order.size();
    }
}

template<size_t BlockSize>
void BlockQueueFCFSScheduler<BlockSize>::sync_to(sim_time_t current_time) {
    if (current_time <= m_current_tracked_time) {
        return;
    }

    while (m_eligible_end_idx < m_job_order.size() &&
           m_job_order[m_eligible_end_idx].submit_time <= current_time) {
        ++m_eligible_end_idx;
    }
    m_current_tracked_time = current_time;
}

template<size_t BlockSize>
sim_time_t BlockQueueFCFSScheduler<BlockSize>::get_next_arrival_time() {
    for (size_t i = m_eligible_end_idx; i < m_job_order.size(); ++i) {
        if (!m_job_order[i].removed) {
            return m_job_order[i].submit_time;
        }
    }
    return std::numeric_limits<sim_time_t>::max();
}

template<size_t BlockSize>
std::vector<job_no_t> BlockQueueFCFSScheduler<BlockSize>::schedule(
    num_nodes_t free_nodes,
    const std::map<job_no_t, sim_time_t>& running_jobs,
    sim_time_t current_time)
{
    sync_to(current_time);

    if (!has_eligible_jobs()) {
        return {};
    }

    std::vector<job_no_t> jobs_to_run;
    num_nodes_t available_nodes = free_nodes;

    // Step 1: Start FCFS head(s)
    while (m_eligible_end_idx > 0) {
        job_no_t head = static_cast<job_no_t>(-1);

        for (size_t i = 0; i < m_eligible_end_idx; ++i) {
            if (!m_job_order[i].removed) {
                head = m_job_order[i].job_id;
                break;
            }
        }

        if (head == static_cast<job_no_t>(-1)) {
            break;
        }

        const auto& job = (*m_job_data_ptr)[head];
        num_nodes_t nodes_needed = job.get_num_nodes();

        if (nodes_needed <= available_nodes) {
            jobs_to_run.push_back(head);
            available_nodes -= nodes_needed;
            m_wait_queue.remove(head);

            for (size_t i = 0; i < m_eligible_end_idx; ++i) {
                if (m_job_order[i].job_id == head && !m_job_order[i].removed) {
                    m_job_order[i].removed = true;
                    ++m_removed_count;
                    break;
                }
            }
        } else {
            break;
        }
    }

    if (active_job_count() == 0 || m_backfill_policy == BackfillPolicy::NONE) {
        return jobs_to_run;
    }

    // Step 2: Calculate reservation for FCFS head
    job_no_t fcfs_head = static_cast<job_no_t>(-1);
    for (size_t i = 0; i < m_eligible_end_idx; ++i) {
        if (!m_job_order[i].removed) {
            fcfs_head = m_job_order[i].job_id;
            break;
        }
    }

    if (fcfs_head == static_cast<job_no_t>(-1)) {
        return jobs_to_run;
    }

    const auto& head_job = (*m_job_data_ptr)[fcfs_head];
    num_nodes_t head_nodes = head_job.get_num_nodes();

    std::map<job_no_t, sim_time_t> effective_running_jobs = running_jobs;
    for (job_no_t job_id : jobs_to_run) {
        effective_running_jobs[job_id] = current_time;
    }

    m_fcfs_reservation_time = calculate_fcfs_reservation(
        head_nodes, available_nodes, effective_running_jobs, current_time);

    // Step 3: Backfill
    while (available_nodes > 0) {
        auto backfill_candidate = m_wait_queue.find_and_remove_backfill_candidate(
            available_nodes, current_time, m_fcfs_reservation_time);

        if (!backfill_candidate.has_value()) {
            break;
        }

        job_no_t bf_job = backfill_candidate.value();
        const auto& bf_job_rec = (*m_job_data_ptr)[bf_job];
        num_nodes_t bf_nodes = bf_job_rec.get_num_nodes();

        jobs_to_run.push_back(bf_job);
        available_nodes -= bf_nodes;
        // Job already removed by find_and_remove_backfill_candidate!

        for (size_t i = 0; i < m_job_order.size(); ++i) {
            if (m_job_order[i].job_id == bf_job && !m_job_order[i].removed) {
                m_job_order[i].removed = true;
                if (i < m_eligible_end_idx) {
                    ++m_removed_count;
                }
                break;
            }
        }
    }

    return jobs_to_run;
}

// Explicit template instantiations
template class BlockQueueFCFSScheduler<4>;
template class BlockQueueFCFSScheduler<8>;
template class BlockQueueFCFSScheduler<16>;
template class BlockQueueFCFSScheduler<32>;
template class BlockQueueFCFSScheduler<64>;
template class BlockQueueFCFSScheduler<128>;
template class BlockQueueFCFSScheduler<256>;

} // namespace dr_evt
