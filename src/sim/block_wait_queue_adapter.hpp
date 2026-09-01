/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_BLOCK_WAIT_QUEUE_ADAPTER_HPP
#define DR_EVT_SIM_BLOCK_WAIT_QUEUE_ADAPTER_HPP

#include <deque>
#include "common.hpp"
#include "sim/block_wait_queue.hpp"
#include "trace/job_record.hpp"

namespace dr_evt {

/**
 * Adapter that provides deque interface but uses BlockWaitQueue internally.
 * This allows drop-in replacement without changing scheduler code.
 */
class BlockWaitQueueAdapter {
private:
    BlockWaitQueue m_queue;
    const std::vector<Job_Record>* m_job_data;

    // Cached deque view for iteration (regenerated when needed)
    mutable std::deque<std::pair<job_no_t, bool>> m_deque_view;
    mutable bool m_view_dirty;

    void rebuild_view() const {
        if (!m_view_dirty) return;

        m_deque_view.clear();
        m_queue.for_each_active([this](job_no_t job_id) {
            m_deque_view.push_back({job_id, false});
        });
        m_view_dirty = false;
    }

public:
    explicit BlockWaitQueueAdapter(const std::vector<Job_Record>& job_data,
                                   size_t block_size = 128,
                                   bool immediate_erase = false)
        : m_queue(block_size, immediate_erase)
        , m_job_data(&job_data)
        , m_view_dirty(false)
    {}

    // Deque-like interface
    void push_back(const std::pair<job_no_t, bool>& entry) {
        job_no_t job_id = entry.first;
        const auto& job = (*m_job_data)[job_id];

        // Extract job info
        const auto& ts = job.get_submit_time();
        sim_time_t submit_time = static_cast<sim_time_t>(ts.first) + ts.second;
        tdiff_t runtime = job.get_time_limit();
        num_nodes_t nodes = job.get_num_nodes();

        m_queue.insert_job(job_id, submit_time, runtime, nodes);
        m_view_dirty = true;
    }

    size_t size() const {
        return m_queue.size();
    }

    bool empty() const {
        return m_queue.empty();
    }

    // For scheduler iteration
    std::deque<std::pair<job_no_t, bool>>& get_deque() {
        rebuild_view();
        return m_deque_view;
    }

    // Mark job as removed (updates both queue and view)
    void mark_job_removed(job_no_t job_id) {
        m_queue.mark_removed(job_id);

        // Update view if it exists
        if (!m_view_dirty) {
            for (auto& entry : m_deque_view) {
                if (entry.first == job_id && !entry.second) {
                    entry.second = true;
                    break;
                }
            }
        }
    }

    BlockWaitQueue::Stats get_stats() const {
        return m_queue.get_stats();
    }
};

} // namespace dr_evt

#endif // DR_EVT_SIM_BLOCK_WAIT_QUEUE_ADAPTER_HPP
