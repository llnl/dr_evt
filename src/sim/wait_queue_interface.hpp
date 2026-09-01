/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_SIM_WAIT_QUEUE_INTERFACE_HPP
#define DR_EVT_SIM_WAIT_QUEUE_INTERFACE_HPP

#include <deque>
#include <vector>
#include <optional>
#include "common.hpp"

#ifdef USE_BLOCK_QUEUE
#include "sim/block_wait_queue.hpp"
#endif

namespace dr_evt {

/**
 * Unified interface for wait queue implementations.
 * Adapts both deque and BlockWaitQueue to a common interface.
 */
class WaitQueueInterface {
public:
    virtual ~WaitQueueInterface() = default;

    virtual void insert(job_no_t job_id, sim_time_t submit_time,
                       tdiff_t runtime_estimate, num_nodes_t nodes_requested) = 0;
    virtual void mark_removed(job_no_t job_id) = 0;
    virtual size_t size() const = 0;
    virtual size_t active_count() const = 0;
    virtual bool empty() const = 0;

    // For iteration (FCFS order)
    virtual void for_each_active(std::function<void(job_no_t)> func) const = 0;

    // For backfill search
    virtual std::optional<job_no_t> find_backfill_candidate(
        num_nodes_t available_nodes,
        sim_time_t current_time,
        sim_time_t reservation_time) = 0;
};

/**
 * Deque-based implementation (original, for baseline comparison)
 */
class DequeWaitQueue : public WaitQueueInterface {
private:
    std::deque<std::pair<job_no_t, bool>> m_queue;  // (job_id, removed_flag)
    const std::vector<Job_Record>* m_job_data;

public:
    explicit DequeWaitQueue(const std::vector<Job_Record>& job_data)
        : m_job_data(&job_data) {}

    void insert(job_no_t job_id, sim_time_t, tdiff_t, num_nodes_t) override {
        m_queue.push_back({job_id, false});
    }

    void mark_removed(job_no_t job_id) override {
        for (auto& entry : m_queue) {
            if (entry.first == job_id && !entry.second) {
                entry.second = true;
                return;
            }
        }
    }

    size_t size() const override { return m_queue.size(); }

    size_t active_count() const override {
        size_t count = 0;
        for (const auto& entry : m_queue) {
            if (!entry.second) count++;
        }
        return count;
    }

    bool empty() const override {
        return active_count() == 0;
    }

    void for_each_active(std::function<void(job_no_t)> func) const override {
        for (const auto& entry : m_queue) {
            if (!entry.second) {
                func(entry.first);
            }
        }
    }

    std::optional<job_no_t> find_backfill_candidate(
        num_nodes_t available_nodes,
        sim_time_t current_time,
        sim_time_t reservation_time) override;

    // Legacy interface for compatibility with existing scheduler
    std::deque<std::pair<job_no_t, bool>>& get_deque() { return m_queue; }
};

#ifdef USE_BLOCK_QUEUE
/**
 * Block-based implementation (optimized)
 */
class BlockBasedWaitQueue : public WaitQueueInterface {
private:
    BlockWaitQueue m_queue;

public:
    explicit BlockBasedWaitQueue(size_t block_size = 128, bool immediate_erase = false)
        : m_queue(block_size, immediate_erase) {}

    void insert(job_no_t job_id, sim_time_t submit_time,
               tdiff_t runtime_estimate, num_nodes_t nodes_requested) override {
        m_queue.insert_job(job_id, submit_time, runtime_estimate, nodes_requested);
    }

    void mark_removed(job_no_t job_id) override {
        m_queue.mark_removed(job_id);
    }

    size_t size() const override { return m_queue.size(); }
    size_t active_count() const override { return m_queue.active_count(); }
    bool empty() const override { return m_queue.empty(); }

    void for_each_active(std::function<void(job_no_t)> func) const override {
        m_queue.for_each_active(func);
    }

    std::optional<job_no_t> find_backfill_candidate(
        num_nodes_t available_nodes,
        sim_time_t current_time,
        sim_time_t reservation_time) override {
        return m_queue.find_backfill_candidate(available_nodes, current_time, reservation_time);
    }

    BlockWaitQueue::Stats get_stats() const { return m_queue.get_stats(); }
};
#endif

} // namespace dr_evt

#endif // DR_EVT_SIM_WAIT_QUEUE_INTERFACE_HPP
