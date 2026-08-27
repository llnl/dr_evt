/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include "sim/sim.hpp"
#include "trace/job_io.hpp"
#include <algorithm>

namespace dr_evt {

Simulation::Simulation(const Sim_Params& params)
  : m_params(params),
    m_trace(params.m_infile, params.m_trace_format,
            params.m_timestamp_format, params.m_timezone),
    m_scheduler(params.m_total_nodes,
                m_trace.data(),
                params.m_backfill_policy,
                params.m_priority_policy,
                params.m_runtime_mode),
    m_current_time(0.0),
    m_jobs_completed(0),
    m_jobs_submitted(0)
{
}

void Simulation::run()
{
    std::cout << "Starting simulation..." << std::endl;

    // Initialize: load trace and create submit events
    initialize();

    std::cout << "Loaded " << m_trace.data().size() << " jobs from trace" << std::endl;
    std::cout << "Running simulation with " << m_params.m_total_nodes << " nodes" << std::endl;

    // Main event loop
    while (advance_to_next_event()) {
        // Event processing happens inside advance_to_next_event()
    }

    std::cout << "Simulation complete" << std::endl;
    std::cout << "Jobs submitted: " << m_jobs_submitted << std::endl;
    std::cout << "Jobs completed: " << m_jobs_completed << std::endl;
}

void Simulation::print_stats(std::ostream& os) const
{
    os << "=== Simulation Statistics ===" << std::endl;
    os << "Total jobs: " << m_trace.data().size() << std::endl;
    os << "Jobs submitted: " << m_jobs_submitted << std::endl;
    os << "Jobs completed: " << m_jobs_completed << std::endl;
    os << "Current time: " << m_current_time << std::endl;
    os << "Total nodes: " << m_params.m_total_nodes << std::endl;

    // Calculate metrics
    if (m_jobs_completed > 0) {
        tdiff_t total_wait = 0.0;
        tdiff_t total_turnaround = 0.0;
        sim_time_t makespan = 0.0;

        for (const auto& job : m_trace.data()) {
            // Wait time = start - submit
            tdiff_t wait = job.get_wait_time();
            total_wait += wait;

            // Turnaround = end - submit
            tdiff_t turnaround = wait + job.get_exec_time();
            total_turnaround += turnaround;

            // Makespan = max end time
            makespan = std::max(makespan, static_cast<sim_time_t>(
                job.get_begin_time().first + job.get_exec_time()));
        }

        os << "Average wait time: " << (total_wait / m_jobs_completed) << " sec" << std::endl;
        os << "Average turnaround time: " << (total_turnaround / m_jobs_completed) << " sec" << std::endl;
        os << "Makespan: " << makespan << " sec" << std::endl;
    }
}

void Simulation::initialize()
{
    // Load trace data
    const auto max_num_jobs = m_params.m_is_jobs_set ?
                              m_params.m_max_jobs :
                              static_cast<num_jobs_t>(0u);

    if (max_num_jobs == 0u) {
        m_trace.data().reserve(1467542u);
    } else {
        m_trace.data().reserve(max_num_jobs);
    }

    int rc = m_trace.load_data(max_num_jobs);
    if (rc != EXIT_SUCCESS) {
        throw std::runtime_error("Failed to load trace data");
    }

    // Sort jobs by submission time
    std::stable_sort(m_trace.data().begin(), m_trace.data().end());

    // Create submit events for all jobs
    for (num_jobs_t i = 0; i < m_trace.data().size(); ++i) {
        const auto& job = m_trace.data()[i];
        epoch_t submit_time = job.get_submit_time();

        // Create submit event
        // Use a special event type or marker for submit events
        // Since DR_Event expects arrival/departure, we'll use arrival=true for submit
        m_event_queue.emplace(i, submit_time, true);
    }

    m_current_time = 0.0;
    m_jobs_submitted = 0;
    m_jobs_completed = 0;
}

void Simulation::process_submit_event(job_no_t job_idx)
{
    const auto& job = m_trace.data()[job_idx];
    std::cout << "Job " << job_idx << " submitted at " << m_current_time
              << " (" << job.get_num_nodes() << " nodes)\n";

    std::vector<job_no_t> jobs_to_submit = {job_idx};

    // Submit to scheduler
    std::vector<job_no_t> jobs_to_start = m_scheduler.submit_jobs(jobs_to_submit, m_current_time);

    m_jobs_submitted++;

    // Schedule start events for jobs that can run immediately
    if (!jobs_to_start.empty()) {
        schedule_start_events(jobs_to_start);
    }
}

void Simulation::process_start_event(job_no_t job_idx)
{
    std::vector<job_no_t> jobs_to_start = {job_idx};

    // Start the job in the scheduler
    std::vector<job_no_t> started_jobs = m_scheduler.start_jobs(jobs_to_start, m_current_time);

    // Schedule end events for all started jobs
    for (job_no_t started_idx : started_jobs) {
        const auto& job = m_trace.data()[started_idx];
        std::cout << "Job " << started_idx << " started at " << m_current_time
                  << " (" << job.get_num_nodes() << " nodes)\n";
        schedule_end_event(started_idx, m_current_time);
    }
}

void Simulation::process_end_event(job_no_t job_idx)
{
    std::cout << "Job " << job_idx << " ended at " << m_current_time << "\n";

    std::vector<job_no_t> jobs_to_end = {job_idx};

    // End the job in the scheduler
    std::vector<job_no_t> jobs_to_start = m_scheduler.end_jobs(jobs_to_end, m_current_time);

    m_jobs_completed++;

    // Schedule start events for jobs that can now run
    if (!jobs_to_start.empty()) {
        schedule_start_events(jobs_to_start);
    }
}

bool Simulation::advance_to_next_event()
{
    if (m_event_queue.empty()) {
        return false;
    }

    // Get the next event
    auto event = *m_event_queue.begin();
    m_event_queue.erase(m_event_queue.begin());

    // Update current time
    const epoch_t& event_time = event.get_time();
    m_current_time = static_cast<sim_time_t>(event_time.first) + event_time.second;

    job_no_t job_idx = event.get_job_idx();

    // Determine event type by checking job state
    // If job is not yet submitted to scheduler, this is a submit event
    if (!m_scheduler.is_waiting(job_idx) &&
        !m_scheduler.is_scheduled(job_idx) &&
        !m_scheduler.is_running(job_idx)) {
        // Submit event
        process_submit_event(job_idx);
    }
    // If job is scheduled and it's time to start
    else if (m_scheduler.is_scheduled(job_idx)) {
        sim_time_t reservation = m_scheduler.get_reservation_time(job_idx);
        if (std::abs(reservation - m_current_time) < 0.001) {
            // Start event
            process_start_event(job_idx);
        }
    }
    // If job is running, this is an end event
    else if (m_scheduler.is_running(job_idx)) {
        // End event
        process_end_event(job_idx);
    }

    return true;
}

void Simulation::schedule_start_events(const std::vector<job_no_t>& jobs)
{
    for (job_no_t job_idx : jobs) {
        sim_time_t start_time = m_scheduler.get_reservation_time(job_idx);

        // Convert to epoch_t
        time_t seconds = static_cast<time_t>(start_time);
        float fraction = start_time - seconds;
        epoch_t start_epoch = {seconds, fraction};

        // Create start event (use arrival=true as marker)
        m_event_queue.emplace(job_idx, start_epoch, true);
    }
}

void Simulation::schedule_end_event(job_no_t job_idx, sim_time_t start_time)
{
    const auto& job = m_trace.data()[job_idx];

    // Jobs always complete at actual execution time
    tdiff_t exec_time = job.get_exec_time();
    sim_time_t end_time = start_time + exec_time;

    // Convert to epoch_t
    time_t seconds = static_cast<time_t>(end_time);
    float fraction = end_time - seconds;
    epoch_t end_epoch = {seconds, fraction};

    // Create end event (use departure=false as marker)
    m_event_queue.emplace(job_idx, end_epoch, false);
}

} // end of namespace dr_evt
