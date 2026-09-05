/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include "sim/sim.hpp"
#include "trace/job_io.hpp"
#include "trace/parse_utils.hpp"
#include "trace/epoch.hpp"
#include <algorithm>
#include <fstream>
#include <queue>
#include <sstream>
#include <iomanip>
#include "sim/block_wait_queue.hpp"

namespace dr_evt {

namespace {
// Format a sim_time_t for trace/resource-trace output. Default (msec=false)
// truncates to whole seconds as a plain integer, matching all existing
// traces and tests, which only ever use integer-second submit times.
// When msec=true, formats with millisecond precision (3 decimal places)
// instead, for traces that need sub-second timing.
std::string format_sim_time(sim_time_t t, bool msec)
{
    std::ostringstream oss;
    if (msec) {
        oss << std::fixed << std::setprecision(3) << t;
    } else {
        oss << static_cast<int64_t>(t);
    }
    return oss.str();
}
} // anonymous namespace

Simulation::Simulation(const Sim_Params& params)
  : m_params(params),
    m_trace(params.m_infile, params.m_trace_format,
            params.m_timestamp_format, params.m_timezone),
    m_scheduler(create_scheduler(
        params.m_total_nodes,
        m_trace.data(),
        params.m_backfill_policy,
        params.m_priority_policy,
        params.m_queue_impl,
        params.m_block_size,
        params.m_circular_capacity,
        params.m_circular_overflow)),
    m_current_time(0.0),
    m_jobs_completed(0),
    m_jobs_submitted(0),
    m_rng(params.m_seed),
    m_replay_ctx(m_trace.create_context()),
    m_queue_length_sum(0),
    m_queue_length_samples(0),
    m_queue_length_peak(0)
{
}

void Simulation::run()
{
    if (m_params.m_verbose) {
        std::cout << "Starting simulation..." << std::endl;
    }

    // Initialize: load jobs and determine durations
    initialize_trace();

    if (m_params.m_verbose) {
        std::cout << "Loaded " + std::to_string(m_trace.data().size()) + " jobs from trace\n";
        std::cout << "Running simulation with " + std::to_string(m_params.m_total_nodes) + " nodes\n";
    }

    // Record initial resource state (all nodes free)
    m_resource_history.emplace_back(m_current_time, m_params.m_total_nodes, 0);

    // Batch mode: Submit all jobs upfront, then advance to infinity
    // This uses the streaming API internally
    for (num_jobs_t i = 0; i < m_trace.data().size(); ++i) {
        const auto& job = m_trace.data()[i];
        sim_time_t submit_time = convert_epoch<sim_time_t>(job.get_submit_time());
        submit_job(i, submit_time);
    }

    // Batch mode: advance to infinity to process all jobs
    // The loop will exit when both wait_queue and event_queue are empty
    advance_to(std::numeric_limits<sim_time_t>::max());

    // Count actual completions from trace data
    m_jobs_completed = 0;
    for (const auto& job : m_trace.data()) {
        if (job.is_scheduled()) {
            m_jobs_completed++;
        }
    }

    if (m_params.m_verbose) {
        std::cout << "Simulation complete\n" +
                     std::string("Jobs submitted: ") + std::to_string(m_jobs_submitted) + "\n" +
                     std::string("Jobs completed: ") + std::to_string(m_jobs_completed) + "\n";
    }
}

void Simulation::print_stats(std::ostream& os) const
{
    os << "=== Simulation Statistics ===" << std::endl;
    os << "Total jobs: " << m_trace.data().size() << std::endl;
    os << "Jobs submitted: " << m_jobs_submitted << std::endl;
    os << "Jobs completed: " << m_jobs_completed << std::endl;
    os << "Current time: " << format_sim_time(m_current_time, m_params.m_msec_output) << std::endl;
    os << "Total nodes: " << m_params.m_total_nodes << std::endl;

    // Calculate metrics
    if (m_jobs_completed > 0) {
        tdiff_t total_wait = 0.0;
        tdiff_t total_turnaround = 0.0;
        sim_time_t makespan = 0.0;

        for (const auto& job : m_trace.data()) {
            // Job_Record::is_scheduled() (backed by a dedicated max-value
            // sentinel, not end_time == 0) is the correct check here: a
            // job legitimately starting at simulation time 0 previously
            // got excluded by an end_time/begin_time == 0 check, since 0
            // is also a real, valid timestamp - not a reliable "never
            // ran" marker. This also keeps this loop's sum and
            // m_jobs_completed (the denominator below) using the exact
            // same criterion, since m_jobs_completed above is now also
            // counted via is_scheduled().
            if (!job.is_scheduled()) continue;  // Job never completed

            tdiff_t wait = job.get_wait_time();
            total_wait += wait;

            tdiff_t turnaround = wait + job.get_actual_run_time();
            total_turnaround += turnaround;

            makespan = std::max(makespan,
                convert_epoch<sim_time_t>(job.get_begin_time()) + job.get_actual_run_time());
        }

        // Unlike Current time/Makespan above, these are computed averages
        // (division results), which commonly have a fractional part even
        // with integer-second input data (e.g. 220/3 = 73.333...) - that
        // precision is meaningful and was shown by default before
        // msec_output existed, so it's preserved here regardless of
        // msec_output's setting, rather than routed through
        // format_sim_time (whose integer-truncation default is for
        // matching existing trace-output files' conventions, not for
        // these summary statistics).
        os << "Average wait time: " << (total_wait / m_jobs_completed) << " sec" << std::endl;
        os << "Average turnaround time: " << (total_turnaround / m_jobs_completed) << " sec" << std::endl;
        os << "Makespan: " << format_sim_time(makespan, m_params.m_msec_output) << " sec" << std::endl;
    }

    // Queue length statistics
    if (m_queue_length_samples > 0) {
        double avg_queue_length = static_cast<double>(m_queue_length_sum) / m_queue_length_samples;
        os << "Average queue length: " << avg_queue_length << " jobs" << std::endl;
        os << "Peak queue length: " << m_queue_length_peak << " jobs" << std::endl;
    }
}

num_jobs_t Simulation::initialize_trace(num_jobs_t max_jobs)
{
    // Clear any previously-loaded data first, so this method is safe to
    // call more than once (directly, or via run() after an earlier
    // explicit call - run() calls this internally too). Without this,
    // Job_Io::load() only ever push_back()s and never clears the
    // underlying vector itself, so a second call would silently append
    // to, rather than replace, the first call's jobs - e.g. calling
    // initialize_trace() explicitly and then run() would silently double
    // every job's count.
    m_trace.data().clear();

    // Load trace data
    const auto max_num_jobs = (max_jobs > 0u) ? max_jobs :
                              (m_params.m_is_jobs_set ?
                               m_params.m_max_jobs :
                               static_cast<num_jobs_t>(0u));

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
        determine_job_run_time();
    }

    m_current_time = 0.0;
    m_jobs_submitted = 0;
    m_jobs_completed = 0;

    return static_cast<num_jobs_t>(m_trace.data().size());
}

void Simulation::determine_job_run_time()
{
    for (auto& job : m_trace.data()) {
        // Scheduler uses time_limit as the best estimator for planning (realistic mode).
        // run_time_mode controls how the job's actual execution length is determined.

        tdiff_t run_time;

        switch (m_params.m_run_time_mode) {
            case RunTimeMode::ACTUAL:
                // Read actual_run_time from trace (most realistic)
                run_time = job.get_actual_run_time();
                break;

            case RunTimeMode::DISTRIBUTION:
                // Sample from distribution (realistic with variation)
                run_time = sample_run_time(
                    job.get_limit_time(),
                    m_params.m_run_time_distribution,
                    m_params.m_run_time_scale,
                    m_params.m_run_time_stddev
                );
                job.set_actual_run_time(run_time);
                break;

            case RunTimeMode::LIMIT:
                // Use time_limit in place of run_time (unrealistic, for debugging/testing)
                run_time = job.get_limit_time();
                job.set_actual_run_time(run_time);
                break;
        }
    }
}

tdiff_t Simulation::sample_run_time(
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
            double run_time = normal_dist(m_rng);
            // A real HPC scheduler kills a job at its stated time_limit -
            // it can never actually run longer than that. Cap here so a
            // wide-tailed sample can't silently let a job run past its own
            // limit, which would diverge from real system behavior.
            return std::min(time_limit, std::max(0.0, run_time));
        }

        case DistributionType::LOGNORMAL: {
            double mu = std::log(time_limit * scale);
            double sigma = stddev;
            std::lognormal_distribution<double> lognormal_dist(mu, sigma);
            // Always >= 0 by construction; still cap at time_limit for the
            // same reason as NORMAL above - a real job cannot run past it.
            return std::min(time_limit, lognormal_dist(m_rng));
        }

        case DistributionType::UNIFORM: {
            double min_run_time = time_limit * scale;
            double max_run_time = time_limit * (scale + stddev);
            std::uniform_real_distribution<double> uniform_dist(min_run_time, max_run_time);
            // Not capped: unlike NORMAL/LOGNORMAL's unbounded-above tails,
            // this distribution's upper bound is already an explicit,
            // direct function of the caller's own scale/stddev choice -
            // exceeding time_limit here only happens if the caller
            // deliberately set scale + stddev > 1.0.
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
        std::string line = format_sim_time(convert_epoch<sim_time_t>(job.get_submit_time()), m_params.m_msec_output) + "," +
                           format_sim_time(convert_epoch<sim_time_t>(job.get_begin_time()), m_params.m_msec_output) + "," +
                           format_sim_time(convert_epoch<sim_time_t>(job.get_end_time()), m_params.m_msec_output) + "," +
                           std::to_string(job.get_num_nodes()) + "," +
                           "0," +
                           dr_evt::to_string(job.get_queue()) + "," +
                           format_sim_time(job.get_limit_time(), m_params.m_msec_output) + "\n";
        ofs << line;
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
        ofs << format_sim_time(std::get<0>(entry), m_params.m_msec_output) << ","
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

    // Submit to scheduler (scheduler maintains internal wait queue)
    // Scheduler uses time_limit as the best estimator for planning
    const auto& job = m_trace.data()[job_idx];
    tdiff_t run_time_estimate = job.get_limit_time();
    num_nodes_t nodes = job.get_num_nodes();

    m_scheduler->insert_job(job_idx, submit_time, run_time_estimate, nodes);
}

void Simulation::advance_to(sim_time_t target_time)
{
    // Validate precondition
    if (target_time < m_current_time) {
        throw std::runtime_error("Cannot advance backwards in time. "
                                "target_time=" + std::to_string(target_time) +
                                " but current_time=" + std::to_string(m_current_time));
    }

    // Before entering the main loop: check if any jobs are eligible at current_time (initially 0)
    // This handles the case where jobs submit at t=0
    if (m_scheduler->has_eligible_jobs()) {
        // Call scheduler to evaluate newly arriving jobs
        while (true) {
            num_nodes_t free_nodes = m_params.m_total_nodes - m_trace.get_nodes_in_use(m_replay_ctx);
            auto jobs_to_run = m_scheduler->schedule(free_nodes, m_running_jobs, m_current_time);

            if (jobs_to_run.empty()) {
                break;
            }

            // Process ALL jobs returned by scheduler (backfilling can return multiple)
            // Record resource state after EACH job starts
            for (job_no_t job : jobs_to_run) {
                m_trace.insert_job(job, m_current_time, m_replay_ctx);
                m_running_jobs[job] = m_current_time;
                m_jobs_submitted++;

                m_trace.run_until_inclusive(m_replay_ctx, m_current_time);

                // Record resource state after starting this job
                num_nodes_t allocated = m_trace.get_nodes_in_use(m_replay_ctx);
                m_resource_history.emplace_back(m_current_time,
                                                m_params.m_total_nodes - allocated,
                                                allocated);
            }
        }
    }

    // Main event loop - process events and make scheduling decisions until complete
    // Compute loop state variables once before entering loop
    size_t active_count = m_scheduler->active_job_count();
    sim_time_t next_arrival = m_scheduler->get_next_arrival_time();

    // Sample queue length for statistics
    m_queue_length_sum += active_count;
    m_queue_length_samples++;
    m_queue_length_peak = std::max(m_queue_length_peak, active_count);

    // Continue while: (1) jobs waiting to be scheduled, OR (2) events pending (jobs running), OR (3) future job arrivals
    while (active_count > 0 || !m_replay_ctx.m_evtq.empty() || next_arrival < std::numeric_limits<sim_time_t>::max()) {
        if (m_params.m_verbose) {
            std::cout << "Loop iter: active=" << active_count
                      << " events=" << m_replay_ctx.m_evtq.size()
                      << " next_arrival=" << next_arrival
                      << " time=" << m_current_time << std::endl;
        }
        // next_arrival already computed above

        // Find next replay event time
        bool has_replay_event = !m_replay_ctx.m_evtq.empty();
        sim_time_t next_replay_time = std::numeric_limits<sim_time_t>::max();
        [[maybe_unused]] bool next_is_start = false;
        if (has_replay_event) {
            const auto& event = *m_replay_ctx.m_evtq.begin();
            next_replay_time = convert_epoch<sim_time_t>(event.get_time());
            next_is_start = event.is_arrival();
        }

        // Decide which event to process
        bool should_schedule = false;

        if (has_replay_event && next_replay_time <= next_arrival && next_replay_time <= target_time) {
            // Process replay events at this time
            // Advance time FIRST
            m_current_time = next_replay_time;
            // Explicit sync: schedule() below only runs if an END event
            // freed resources (should_schedule = processed_end_event).
            // Without this call, a replay step that only processes START
            // events would leave the scheduler's eligibility tracking
            // stale relative to m_current_time, since nothing else
            // would sync it before the queries below.
            m_scheduler->sync_to(m_current_time);

            // Process ALL events at current_time before calling scheduler
            // This ensures END events are processed before START events created by scheduler
            bool processed_end_event = false;

            while (!m_replay_ctx.m_evtq.empty()) {
                const auto& event = *m_replay_ctx.m_evtq.begin();
                sim_time_t event_time = convert_epoch<sim_time_t>(event.get_time());

                if (event_time != m_current_time) {
                    break;  // No more events at current_time
                }

                bool is_end = !event.is_arrival();
                job_no_t event_job_idx = event.get_job_idx();

                // Process this event (END or START)
                m_trace.process_single_event(m_replay_ctx);

                // Record resource state after event
                num_nodes_t allocated = m_trace.get_nodes_in_use(m_replay_ctx);
                m_resource_history.emplace_back(m_current_time,
                                                m_params.m_total_nodes - allocated,
                                                allocated);

                // If END event: remove from running_jobs
                if (is_end) {
                    processed_end_event = true;
                    m_running_jobs.erase(event_job_idx);
                    m_jobs_completed++;
                }
            }

            // Only call scheduler if we processed END events (resources freed)
            should_schedule = processed_end_event;

        } else if (next_arrival < std::numeric_limits<sim_time_t>::max() && next_arrival <= target_time) {
            // Job arrival - advance time FIRST
            // Note: Check next_arrival < infinity to avoid infinite loop
            // If no jobs arriving, scheduler should pick from waiting queue instead
            m_current_time = next_arrival;
            // Explicit sync, matching the replay-event branch above for
            // symmetry - should_schedule is always true in this branch
            // (set unconditionally below), so schedule()'s own internal
            // sync_to() call would already cover this in practice, but
            // this doesn't rely on that.
            m_scheduler->sync_to(m_current_time);

            // jobs_at_next_arrival already collected during wait_queue scan
            // TODO: Pass jobs_at_next_arrival to scheduler for efficient evaluation
            // For now, just set flag to schedule
            should_schedule = true;
        } else {
            // No arrivals and no replay events before target_time
            if (m_params.m_verbose) {
                std::cout << "ELSE block: active=" << m_scheduler->active_job_count()
                          << " events=" << m_replay_ctx.m_evtq.size()
                          << " time=" << m_current_time << std::endl;
            }
            // No events to process - exit loop
            break;
        }

        // Scheduling loop - let scheduler make decisions after processing END events
        if (should_schedule) {
            // Keep calling scheduler until it can't start any more jobs
            while (true) {
                num_nodes_t free_nodes = m_params.m_total_nodes - m_trace.get_nodes_in_use(m_replay_ctx);

                auto jobs_to_run = m_scheduler->schedule(free_nodes, m_running_jobs, m_current_time);

                if (jobs_to_run.empty()) {
                    break;  // Scheduler can't start anything else
                }

                // Process ALL jobs returned by scheduler (backfilling can return multiple)
                // Record resource state after EACH job starts
                for (job_no_t job : jobs_to_run) {
                    m_trace.insert_job(job, m_current_time, m_replay_ctx);
                    m_running_jobs[job] = m_current_time;
                    m_jobs_submitted++;

                    // Process this START event
                    while (!m_replay_ctx.m_evtq.empty()) {
                        const auto& event = *m_replay_ctx.m_evtq.begin();
                        sim_time_t event_time = convert_epoch<sim_time_t>(event.get_time());

                        // Only process START events at current_time for this job
                        if (event_time != m_current_time) break;
                        if (!event.is_arrival()) break;  // Hit an END event, stop (shouldn't happen)

                        m_trace.process_single_event(m_replay_ctx);
                        break;  // Process only one START event per job
                    }

                    // Record resource state after this job starts
                    num_nodes_t allocated = m_trace.get_nodes_in_use(m_replay_ctx);
                    m_resource_history.emplace_back(m_current_time,
                                                    m_params.m_total_nodes - allocated,
                                                    allocated);
                }
            }
        }

        // Update loop state variables at end of iteration
        active_count = m_scheduler->active_job_count();
        next_arrival = m_scheduler->get_next_arrival_time();

        // Sample queue length for statistics
        m_queue_length_sum += active_count;
        m_queue_length_samples++;
        m_queue_length_peak = std::max(m_queue_length_peak, active_count);
    }

    // Loop exited - log final state for debugging
    if (m_params.m_verbose) {
        std::cout << "Loop exited: active=" << active_count
                  << " events=" << m_replay_ctx.m_evtq.size()
                  << " time=" << m_current_time << std::endl;
    }

    // Don't record spurious final state - last event already recorded the final state
}

num_nodes_t Simulation::get_nodes_in_use() const
{
    return m_trace.get_nodes_in_use(m_replay_ctx);
}

Simulation::Statistics Simulation::get_statistics() const
{
    Statistics stats;

    // Basic counters
    stats.jobs_submitted = m_jobs_submitted;
    stats.jobs_completed = m_jobs_completed;
    stats.jobs_running = m_running_jobs.size();
    stats.jobs_waiting = m_scheduler->active_job_count();
    stats.current_time = m_current_time;

    // Resource utilization
    stats.total_nodes = m_params.m_total_nodes;
    stats.nodes_in_use = get_nodes_in_use();
    stats.nodes_available = stats.total_nodes - stats.nodes_in_use;

    // Calculate wait times and turnaround times
    tdiff_t total_wait = 0.0;
    tdiff_t total_turnaround = 0.0;
    sim_time_t max_completion = 0.0;
    num_jobs_t completed_count = 0;
    tdiff_t total_node_seconds = 0.0;

    for (const auto& job : m_trace.data()) {
        // Only count jobs that actually completed. Job_Record::is_scheduled()
        // (backed by a dedicated max-value sentinel) is the correct check
        // here: a job legitimately starting at simulation time 0 has
        // begin_time/end_time == 0 under the old convention, which the
        // previous begin_time-based check incorrectly treated as "never
        // started," silently excluding it from these averages. This
        // matches the same convention now used for m_jobs_completed
        // above (see the end-of-run() completion count).
        if (job.is_scheduled()) {
            tdiff_t wait = job.get_wait_time();
            tdiff_t exec = job.get_actual_run_time();

            total_wait += wait;
            total_turnaround += (wait + exec);
            total_node_seconds += static_cast<tdiff_t>(job.get_num_nodes()) * exec;

            sim_time_t completion = convert_epoch<sim_time_t>(job.get_end_time());
            max_completion = std::max(max_completion, completion);
            completed_count++;
        }
    }

    stats.avg_wait_time = (completed_count > 0) ? total_wait / completed_count : 0.0;
    stats.avg_turnaround_time = (completed_count > 0) ? total_turnaround / completed_count : 0.0;
    stats.makespan = max_completion;

    // Time-averaged over [0, makespan], not an instantaneous snapshot - see
    // the field comment in sim.hpp for why.
    stats.utilization = (stats.total_nodes > 0 && stats.makespan > 0) ?
                       total_node_seconds / (static_cast<double>(stats.total_nodes) * stats.makespan) :
                       0.0;

    return stats;
}

} // namespace dr_evt
