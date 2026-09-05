/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULER_CIRCULAR_FCFS_HPP
#define DR_EVT_SIM_SCHEDULER_CIRCULAR_FCFS_HPP

#include <stdexcept>
#include <algorithm>
#include <boost/circular_buffer.hpp>
#include "sim/scheduler_base.hpp"

namespace dr_evt {

/**
 * FCFS scheduler using boost::circular_buffer instead of std::deque.
 *
 * Identical in every other respect to FCFSScheduler (same JobEntry
 * layout, same lazy-mark-and-compact removal, same pop_front() head
 * consumption, same indexed backfill scan) - only the underlying
 * container differs. boost::circular_buffer stores its elements in one
 * contiguous block, unlike std::deque's fixed-size chunks, so indexed
 * access (operator[], used throughout the backfill scan) is a direct
 * offset rather than a chunk lookup followed by an offset within it.
 *
 * boost::circular_buffer has a fixed capacity, unlike std::deque, which
 * grows automatically - a push_back() on a full buffer overwrites the
 * oldest element rather than growing. initial_capacity sets that
 * capacity explicitly (0 defaults to job_data.size(), large enough that
 * it can never overflow, since insert_job() is called at most once per
 * entry in job_data over the scheduler's lifetime - see
 * Simulation::submit_job()). A caller may instead choose a smaller
 * capacity and let overflow_policy decide what happens if it's
 * exceeded: ABORT throws, ending the simulation; GROW reallocates to a
 * larger capacity via set_capacity(), which copies all existing
 * entries over.
 */
class CircularBufferFCFSScheduler : public SchedulerBase {
private:
    struct JobEntry {
        job_no_t job_id;
        sim_time_t submit_time;
        tdiff_t run_time_estimate;
        num_nodes_t nodes_requested;
        bool removed;

        JobEntry(job_no_t id, sim_time_t submit, tdiff_t run_time, num_nodes_t nodes)
            : job_id(id), submit_time(submit), run_time_estimate(run_time),
              nodes_requested(nodes), removed(false) {}
    };

    boost::circular_buffer<JobEntry> m_wait_queue;
    CircularOverflowPolicy m_overflow_policy;
    size_t m_eligible_end_idx;  // Index of first job NOT eligible yet
    sim_time_t m_current_tracked_time;
    size_t m_removed_count;  // Track garbage for collection

public:
    CircularBufferFCFSScheduler(num_nodes_t total_nodes,
                                const std::vector<Job_Record>& job_data,
                                BackfillPolicy bf_policy,
                                size_t initial_capacity = 0,
                                CircularOverflowPolicy overflow_policy = CircularOverflowPolicy::GROW)
        : SchedulerBase(total_nodes, job_data, bf_policy)
        , m_wait_queue(initial_capacity != 0 ? initial_capacity : job_data.size())
        , m_overflow_policy(overflow_policy)
        , m_eligible_end_idx(0)
        , m_current_tracked_time(0.0)
        , m_removed_count(0)
    {}

    void insert_job(job_no_t job_id, sim_time_t submit_time,
                   tdiff_t run_time_estimate, num_nodes_t nodes_requested) override {
        if (m_wait_queue.full()) {
            if (m_overflow_policy == CircularOverflowPolicy::ABORT) {
                throw std::runtime_error(
                    "CircularBufferFCFSScheduler: wait queue capacity (" +
                    std::to_string(m_wait_queue.capacity()) + ") exceeded; "
                    "use --circular_overflow grow or a larger --circular_capacity");
            }
            // GROW: doubling matches std::vector's amortized-growth
            // strategy. set_capacity() copies all existing entries over
            // (confirmed: it only drops elements when shrinking below
            // the current size, which never applies here).
            m_wait_queue.set_capacity(std::max<size_t>(m_wait_queue.capacity() * 2, 1));
        }

        m_wait_queue.push_back(JobEntry(job_id, submit_time, run_time_estimate, nodes_requested));

        // If this job is already eligible, advance index. Can jump by more
        // than 1 in a single call: if this new job's submit_time is
        // already <= current time, the sorted-submit-time invariant means
        // every entry already in the buffer becomes eligible too.
        if (submit_time <= m_current_tracked_time) {
            m_eligible_end_idx = m_wait_queue.size();
        }
    }

    std::vector<job_no_t> schedule(
        num_nodes_t free_nodes,
        const std::map<job_no_t, sim_time_t>& running_jobs,
        sim_time_t current_time) override;

    void sync_to(sim_time_t current_time) override;

    size_t active_job_count() override {
        return m_eligible_end_idx - m_removed_count;
    }

    sim_time_t get_next_arrival_time() override {
        for (size_t i = m_eligible_end_idx; i < m_wait_queue.size(); ++i) {
            if (!m_wait_queue[i].removed) {
                return m_wait_queue[i].submit_time;
            }
        }
        return std::numeric_limits<sim_time_t>::max();
    }

    bool has_eligible_jobs() override {
        return active_job_count() > 0;
    }

protected:
    size_t wait_queue_size() const override {
        return m_wait_queue.size();
    }

private:
    void mark_removed(job_no_t job_id);
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_CIRCULAR_FCFS_HPP
