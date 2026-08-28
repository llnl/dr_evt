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
#include "trace/parse_utils.hpp"
#include <algorithm>
#include <fstream>
#include <queue>

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
    m_jobs_submitted(0),
    m_rng(params.m_seed),
    m_replay_ctx(m_trace.create_context())
{
}

void Simulation::run()
{
    if (m_params.m_verbose) {
        std::cout << "Starting simulation..." << std::endl;
    }

    // Initialize: load jobs and determine durations
    initialize();

    if (m_params.m_verbose) {
        std::cout << "Loaded " << m_trace.data().size() << " jobs from trace" << std::endl;
        std::cout << "Running simulation with " << m_params.m_total_nodes << " nodes" << std::endl;
    }

    // Batch mode: Submit all jobs upfront, then advance to infinity
    // This uses the streaming API internally
    for (num_jobs_t i = 0; i < m_trace.data().size(); ++i) {
        const auto& job = m_trace.data()[i];
        sim_time_t submit_time = static_cast<sim_time_t>(job.get_submit_time().first) +
                                 job.get_submit_time().second;
        submit_job(i, submit_time);
    }

    // Find the latest time in the trace (to know when to stop)
    sim_time_t max_time = 0.0;
    for (const auto& job : m_trace.data()) {
        sim_time_t submit = static_cast<sim_time_t>(job.get_submit_time().first) +
                           job.get_submit_time().second;
        sim_time_t duration = job.get_limit_time();
        max_time = std::max(max_time, submit + duration * 2);  // Conservative upper bound
    }

    // Advance simulation to process all jobs
    advance_to(max_time);

    // Count actual completions from trace data
    m_jobs_completed = 0;
    for (const auto& job : m_trace.data()) {
        if (job.get_end_time().first > 0 || job.get_end_time().second > 0) {
            m_jobs_completed++;
        }
    }

    if (m_params.m_verbose) {
        std::cout << "Simulation complete" << std::endl;
        std::cout << "Jobs submitted: " << m_jobs_submitted << std::endl;
        std::cout << "Jobs completed: " << m_jobs_completed << std::endl;
    }
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
            if (job.get_begin_time().first == 0) continue;  // Job didn't start

            tdiff_t wait = job.get_wait_time();
            total_wait += wait;

            tdiff_t turnaround = wait + job.get_exec_time();
            total_turnaround += turnaround;

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

    // Determine actual durations (simulation mode only)
    if (m_trace.dcols().get_trace_mode() == TraceMode::SIMULATION) {
        determine_job_durations();
    }

    m_current_time = 0.0;
    m_jobs_submitted = 0;
    m_jobs_completed = 0;
}

void Simulation::determine_job_durations()
{
    for (auto& job : m_trace.data()) {
        tdiff_t duration;

        switch (m_params.m_duration_mode) {
            case DurationMode::FROM_COLUMN:
                duration = job.get_actual_duration();
                break;

            case DurationMode::EXACT:
                duration = job.get_limit_time();
                job.set_actual_duration(duration);
                break;

            case DurationMode::DISTRIBUTION:
                duration = sample_duration(
                    job.get_limit_time(),
                    m_params.m_duration_distribution,
                    m_params.m_duration_scale,
                    m_params.m_duration_stddev
                );
                job.set_actual_duration(duration);
                break;
        }
    }
}

tdiff_t Simulation::sample_duration(
    tdiff_t time_limit,
    DistributionType dist,
    double scale,
    double stddev)
{
    if (time_limit <= 0.0) {
        return 0.0;
    }

    switch (dist) {
        case DistributionType::NORMAL: {
            double mean = time_limit * scale;
            double sd = time_limit * stddev;
            std::normal_distribution<double> normal_dist(mean, sd);
            double duration = normal_dist(m_rng);
            return std::max(0.0, duration);
        }

        case DistributionType::LOGNORMAL: {
            double mu = std::log(time_limit * scale);
            double sigma = stddev;
            std::lognormal_distribution<double> lognormal_dist(mu, sigma);
            return lognormal_dist(m_rng);
        }

        case DistributionType::UNIFORM: {
            double min_duration = time_limit * scale;
            double max_duration = time_limit * (scale + stddev);
            std::uniform_real_distribution<double> uniform_dist(min_duration, max_duration);
            return std::max(0.0, uniform_dist(m_rng));
        }

        default:
            return time_limit;
    }
}

void Simulation::write_simulated_trace() const
{
    std::string outfile = m_params.get_outfile();
    if (outfile.empty()) {
        return;
    }

    std::ofstream ofs(outfile);
    if (!ofs) {
        std::cerr << "Failed to open output file: " << outfile << std::endl;
        return;
    }

    // Write header
    ofs << "job_submit_time,begin_time,end_time,num_nodes,exit_status,queue,time_limit\n";

    // Write job records
    for (const auto& job : m_trace.data()) {
        ofs << job.get_submit_time().first << ","
            << job.get_begin_time().first << ","
            << job.get_end_time().first << ","
            << job.get_num_nodes() << ","
            << "0,"
            << dr_evt::to_string(job.get_queue()) << ","
            << job.get_limit_time() << "\n";
    }

    if (m_params.m_verbose) {
        std::cout << "Simulated trace written to: " << outfile << std::endl;
    }
}

void Simulation::write_resource_trace(const std::string& filename) const
{
    if (filename.empty()) {
        return;
    }

    std::ofstream ofs(filename);
    if (!ofs) {
        std::cerr << "Failed to open resource trace file: " << filename << std::endl;
        return;
    }

    // Write header
    ofs << "time,free_nodes,allocated_nodes\n";

    // Write resource state history
    for (const auto& entry : m_resource_history) {
        ofs << std::get<0>(entry) << ","
            << std::get<1>(entry) << ","
            << std::get<2>(entry) << "\n";
    }

    if (m_params.m_verbose) {
        std::cout << "Resource trace written to: " << filename << std::endl;
    }
}

// Public API methods for online/streaming simulation mode
// Allow external code (e.g., gRPC server) to feed jobs and control simulation

void Simulation::submit_job(job_no_t job_idx, sim_time_t submit_time)
{
    // Validate preconditions
    if (submit_time < m_current_time) {
        throw std::runtime_error("Cannot submit job with submit_time < current_time. "
                                "Job " + std::to_string(job_idx) + " has submit_time=" +
                                std::to_string(submit_time) + " but current_time=" +
                                std::to_string(m_current_time));
    }

    if (job_idx >= m_trace.data().size()) {
        throw std::runtime_error("Invalid job_idx: " + std::to_string(job_idx));
    }

    // Add to waiting queue
    m_wait_queue.insert(job_idx);
}

void Simulation::advance_to(sim_time_t target_time)
{
    // Validate precondition
    if (target_time < m_current_time) {
        throw std::runtime_error("Cannot advance backwards in time. "
                                "target_time=" + std::to_string(target_time) +
                                " but current_time=" + std::to_string(m_current_time));
    }

    // Before entering the main loop: check if any jobs arrive at current_time (initially 0)
    // This handles the case where jobs submit at t=0
    if (!m_wait_queue.empty()) {
        bool has_arrivals_now = false;
        for (job_no_t job_idx : m_wait_queue) {
            const auto& job = m_trace.data()[job_idx];
            const auto& ts = job.get_submit_time();
            sim_time_t submit = static_cast<sim_time_t>(ts.first) + ts.second;
            if (submit == m_current_time) {
                has_arrivals_now = true;
                break;
            }
        }

        if (has_arrivals_now) {
            // Call scheduler to evaluate newly arriving jobs
            while (true) {
                num_nodes_t free_nodes = m_params.m_total_nodes - m_trace.get_nodes_in_use(m_replay_ctx);
                auto jobs_to_run = m_scheduler.schedule(m_wait_queue, free_nodes,
                                                        m_running_jobs, m_current_time);
                if (jobs_to_run.empty()) {
                    break;
                }

                for (job_no_t job : jobs_to_run) {
                    m_trace.insert_job(job, m_current_time, m_replay_ctx);
                    m_running_jobs[job] = m_current_time;
                    m_jobs_submitted++;
                }

                m_trace.run_until_inclusive(m_replay_ctx, m_current_time);
            }
        }
    }

    // Main event loop - process events and make scheduling decisions until target_time
    while (m_current_time < target_time || !m_wait_queue.empty()) {
        // Find next event time from waiting jobs and replay events
        sim_time_t next_arrival = std::numeric_limits<sim_time_t>::max();
        for (job_no_t job_idx : m_wait_queue) {
            const auto& job = m_trace.data()[job_idx];
            const auto& ts = job.get_submit_time();
            sim_time_t submit = static_cast<sim_time_t>(ts.first) + ts.second;
            if (submit > m_current_time && submit <= target_time) {
                next_arrival = std::min(next_arrival, submit);
            }
        }

        // Check if any waiting jobs are eligible now (submit_time <= current_time)
        bool have_eligible_jobs_now = false;
        if (next_arrival == std::numeric_limits<sim_time_t>::max() && !m_wait_queue.empty()) {
            for (job_no_t job_idx : m_wait_queue) {
                const auto& job = m_trace.data()[job_idx];
                const auto& ts = job.get_submit_time();
                sim_time_t submit = static_cast<sim_time_t>(ts.first) + ts.second;
                if (submit <= m_current_time) {
                    have_eligible_jobs_now = true;
                    break;
                }
            }
        }

        // Find next replay event time
        bool has_replay_event = !m_replay_ctx.m_evtq.empty();
        sim_time_t next_replay_time = std::numeric_limits<sim_time_t>::max();
        bool next_is_start = false;
        if (has_replay_event) {
            const auto& event = *m_replay_ctx.m_evtq.begin();
            next_replay_time = static_cast<sim_time_t>(event.get_time().first) +
                              event.get_time().second;
            next_is_start = event.is_arrival();
        }

        // Decide which event to process
        bool should_reschedule = false;

        if (has_replay_event && next_replay_time <= next_arrival && next_replay_time <= target_time) {
            // Process replay event
            if (next_is_start) {
                // START event: housekeeping only
                m_trace.run_until_inclusive(m_replay_ctx, next_replay_time);
                m_current_time = next_replay_time;
            } else {
                // END event: free resources and trigger rescheduling
                m_trace.run_until_inclusive(m_replay_ctx, next_replay_time);
                m_current_time = next_replay_time;

                // Update running_jobs and count completions
                auto it = m_running_jobs.begin();
                while (it != m_running_jobs.end()) {
                    const auto& job = m_trace.data()[it->first];
                    sim_time_t end_time = static_cast<sim_time_t>(job.get_end_time().first) +
                                          job.get_end_time().second;
                    if (end_time <= m_current_time) {
                        it = m_running_jobs.erase(it);
                        m_jobs_completed++;
                    } else {
                        ++it;
                    }
                }

                should_reschedule = true;
            }
        } else if (next_arrival <= target_time) {
            // Job arrival
            m_trace.run_until_exclusive(m_replay_ctx, next_arrival);
            m_current_time = next_arrival;
            should_reschedule = true;
        } else if (have_eligible_jobs_now) {
            // No events but have eligible jobs at current time
            should_reschedule = true;
        } else {
            // No more events before target_time
            // Fast-forward to target_time
            if (m_current_time < target_time) {
                m_trace.run_until_inclusive(m_replay_ctx, target_time);
                m_current_time = target_time;
            }
            break;
        }

        // Scheduling loop - let scheduler make decisions
        if (should_reschedule) {
            while (true) {
                num_nodes_t free_nodes = m_params.m_total_nodes - m_trace.get_nodes_in_use(m_replay_ctx);
                auto jobs_to_run = m_scheduler.schedule(m_wait_queue, free_nodes,
                                                        m_running_jobs, m_current_time);

                if (jobs_to_run.empty()) {
                    break;  // Scheduler decided nothing else can run
                }

                // Start the jobs that scheduler selected
                for (job_no_t job : jobs_to_run) {
                    m_trace.insert_job(job, m_current_time, m_replay_ctx);
                    m_running_jobs[job] = m_current_time;
                    m_jobs_submitted++;
                }

                // Process start events
                m_trace.run_until_inclusive(m_replay_ctx, m_current_time);
            }
        }
    }
}

num_nodes_t Simulation::get_nodes_in_use() const
{
    return m_trace.get_nodes_in_use(m_replay_ctx);
}

} // namespace dr_evt
