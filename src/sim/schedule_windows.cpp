/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <algorithm>
#include <stdexcept>
#include "sim/schedule_windows.hpp"

namespace dr_evt {

ScheduleWindows::ScheduleWindows(num_nodes_t total_nodes,
                           const Trace::trace_data_t& job_data)
  : m_total_nodes(total_nodes),
    m_job_data_ptr(&job_data)
{
}

std::vector<Window> ScheduleWindows::get_windows(sim_time_t start_time,
                                         tdiff_t length,
                                         num_nodes_t nodes) const
{
    std::vector<Window> result;
    for (const auto& gap : m_windows) {
        // Gap must:
        // 1. End after start_time
        // 2. Have enough duration for the job
        // 3. Have enough nodes
        if (gap.end > start_time &&
            gap.end - std::max(start_time, gap.start) >= length &&
            gap.available_nodes >= nodes) {
            result.push_back(gap);
        }
    }
    return result;
}

sim_time_t ScheduleWindows::fit_at_the_end(sim_time_t start_time,
                                          num_nodes_t nodes) const
{
    if (m_windows.empty()) {
        return start_time;
    }

    std::vector<Window> end_gaps = get_ending_windows(start_time);

    // Find gaps with enough space
    std::vector<Window> suitable_gaps;
    for (const auto& gap : end_gaps) {
        if (gap.available_nodes >= nodes) {
            suitable_gaps.push_back(gap);
        }
    }

    if (suitable_gaps.empty()) {
        // Return the latest end time of all gaps
        sim_time_t latest = start_time;
        for (const auto& gap : m_windows) {
            latest = std::max(latest, gap.end);
        }
        return latest;
    }

    // Return the earliest start time among suitable gaps
    sim_time_t earliest = suitable_gaps[0].start;
    for (const auto& gap : suitable_gaps) {
        earliest = std::min(earliest, gap.start);
    }
    return std::max(earliest, start_time);
}

void ScheduleWindows::add_reservation(job_no_t job_idx,
                                    sim_time_t start_time,
                                    tdiff_t request_walltime)
{
    if (m_reserved_jobs.find(job_idx) != m_reserved_jobs.end()) {
        throw std::runtime_error("Job already has a reservation");
    }

    const auto& job = (*m_job_data_ptr)[job_idx];
    sim_time_t end_time = start_time + request_walltime;
    num_nodes_t nodes = job.get_num_nodes();

    m_reserved_jobs[job_idx] = start_time;

    // Update gaps: operation -1 means adding a job (reducing available space)
    update_windows(job_idx, start_time, end_time, nodes, -1);
}

void ScheduleWindows::remove_reservation(job_no_t job_idx)
{
    auto it = m_reserved_jobs.find(job_idx);
    if (it == m_reserved_jobs.end()) {
        return; // Job not in reservation table
    }

    sim_time_t start_time = it->second;
    const auto& job = (*m_job_data_ptr)[job_idx];
    num_nodes_t nodes = job.get_num_nodes();

    // For removal, use actual walltime to determine the backfill space
    tdiff_t actual_runtime = job.get_exec_time();
    sim_time_t end_time = start_time + actual_runtime;

    m_reserved_jobs.erase(it);

    // Update gaps: operation +1 means removing a job (increasing available space)
    update_windows(job_idx, start_time, end_time, nodes, +1);
}

void ScheduleWindows::clear()
{
    m_windows.clear();
    m_reserved_jobs.clear();
}

void ScheduleWindows::trim(sim_time_t current_time)
{
    // Remove jobs that have ended
    std::vector<job_no_t> to_remove;
    for (const auto& pair : m_reserved_jobs) {
        job_no_t job_idx = pair.first;
        sim_time_t job_start = pair.second;
        const auto& job = (*m_job_data_ptr)[job_idx];
        tdiff_t runtime = job.get_exec_time();
        if (job_start + runtime < current_time) {
            to_remove.push_back(job_idx);
        }
    }

    for (job_no_t job_idx : to_remove) {
        m_reserved_jobs.erase(job_idx);
    }

    // Remove gaps that end before current time
    m_windows.erase(
        std::remove_if(m_windows.begin(), m_windows.end(),
                       [current_time](const Window& g) { return g.end < current_time; }),
        m_windows.end()
    );

    // If no reservations left, clear all gaps
    if (m_reserved_jobs.empty()) {
        m_windows.clear();
    }
}

bool ScheduleWindows::has_reservation(job_no_t job_idx) const
{
    return m_reserved_jobs.find(job_idx) != m_reserved_jobs.end();
}

sim_time_t ScheduleWindows::get_reservation_time(job_no_t job_idx) const
{
    auto it = m_reserved_jobs.find(job_idx);
    if (it != m_reserved_jobs.end()) {
        return it->second;
    }
    return static_cast<sim_time_t>(-1.0);
}

void ScheduleWindows::update_windows(job_no_t job_idx, sim_time_t start_time,
                                 sim_time_t end_time, num_nodes_t nodes, int op)
{
    if (start_time >= end_time) {
        return;
    }

    // Find gaps that are affected by this job
    // Affected = gap overlaps with [start_time, end_time)
    std::vector<size_t> affected_indices;
    for (size_t i = 0; i < m_windows.size(); ++i) {
        if (m_windows[i].start < end_time && m_windows[i].end > start_time) {
            affected_indices.push_back(i);
        }
    }

    std::vector<Window> new_gaps;

    if (affected_indices.empty()) {
        // No overlapping gaps - create a new gap
        if (op < 0) {
            // Adding a job: create gap with reduced capacity
            new_gaps.emplace_back(start_time, end_time, m_total_nodes - nodes);
        } else {
            // Removing a job: create gap with freed capacity
            new_gaps.emplace_back(start_time, end_time, nodes);
        }
    } else {
        // Process affected gaps
        for (size_t idx : affected_indices) {
            const Window& gap = m_windows[idx];

            // Create gaps for non-overlapping parts
            if (gap.start < start_time) {
                // Part before the job
                new_gaps.emplace_back(gap.start, start_time, gap.available_nodes);
            }

            if (gap.end > end_time) {
                // Part after the job
                new_gaps.emplace_back(end_time, gap.end, gap.available_nodes);
            }

            // Overlapping part
            sim_time_t overlap_start = std::max(gap.start, start_time);
            sim_time_t overlap_end = std::min(gap.end, end_time);
            if (overlap_start < overlap_end) {
                num_nodes_t new_capacity = gap.available_nodes + nodes * op;
                if (new_capacity > 0) {
                    new_gaps.emplace_back(overlap_start, overlap_end, new_capacity);
                }
            }
        }

        // Remove affected gaps
        std::sort(affected_indices.rbegin(), affected_indices.rend());
        for (size_t idx : affected_indices) {
            m_windows.erase(m_windows.begin() + idx);
        }
    }

    // Add new gaps
    m_windows.insert(m_windows.end(), new_gaps.begin(), new_gaps.end());

    // Consolidate overlapping gaps
    consolidate_windows();
}

void ScheduleWindows::consolidate_windows()
{
    if (m_windows.empty()) {
        return;
    }

    // Sort gaps by start time
    std::sort(m_windows.begin(), m_windows.end(),
              [](const Window& a, const Window& b) {
                  return a.start < b.start || (a.start == b.start && a.end < b.end);
              });

    // Merge overlapping gaps with same capacity
    std::vector<Window> consolidated;
    consolidated.push_back(m_windows[0]);

    for (size_t i = 1; i < m_windows.size(); ++i) {
        Window& last = consolidated.back();
        const Window& current = m_windows[i];

        // Check if gaps can be merged
        if (current.start <= last.end && current.available_nodes == last.available_nodes) {
            // Merge by extending the end time
            last.end = std::max(last.end, current.end);
        } else {
            consolidated.push_back(current);
        }
    }

    m_windows = std::move(consolidated);
}

std::vector<Window> ScheduleWindows::get_ending_windows(sim_time_t start_time) const
{
    // Get gaps that exist at or after the end of all currently scheduled jobs
    // This is used to find where a job can be placed after everything else

    if (m_windows.empty()) {
        return {};
    }

    // Find the maximum end time among all gaps
    sim_time_t max_end = start_time;
    for (const auto& gap : m_windows) {
        max_end = std::max(max_end, gap.end);
    }

    // Return gaps that extend to or near the maximum end time
    std::vector<Window> result;
    for (const auto& gap : m_windows) {
        if (gap.end >= start_time) {
            result.push_back(gap);
        }
    }

    return result;
}

} // end of namespace dr_evt
