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

Simulation::Simulation(const Sim_Params& params)
  : m_params(params),
    m_trace(params.m_infile, params.m_trace_format,
            params.m_timestamp_format, params.m_timezone),
    m_scheduler(create_scheduler(
        params.m_total_nodes,
        m_trace,
        params.m_backfill_policy,
        params.m_priority_policy,
        params.m_queue_impl,
        params.m_block_size,
        params.m_wait_queue_capacity,
        params.m_wait_queue_overflow)),
    m_current_time(0.0),
    m_jobs_completed(0),
    m_jobs_submitted(0),
    m_rng(params.m_seed),
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

    // Must happen before initialize_trace() (which calls load_data(),
    // which resolves m_data's capacity from whatever's set here) -
    // unlike resource-history's capacity, which is only needed once
    // recording starts, well after load.
    m_trace.set_job_store_capacity(m_params.m_job_store_capacity);
    m_trace.set_job_store_overflow(m_params.m_job_store_overflow);

    // Initialize: load jobs and determine durations
    initialize_trace();

    if (m_params.m_verbose) {
        std::cout << "Loaded " + std::to_string(m_trace.data().size()) + " jobs from trace\n";
        std::cout << "Running simulation with " + std::to_string(m_params.m_total_nodes) + " nodes\n";
    }

    // Open the resource-trace file early (if one was requested) so
    // reclaiming from the now-bounded circular buffer can flush to it
    // incrementally during the run, rather than only at the very end.
    m_trace.set_resource_history_capacity(m_params.m_resource_history_capacity);
    m_trace.start_resource_trace(m_params.get_resource_trace(), m_params.m_total_nodes,
                                  m_params.m_msec_output);

    // Same reasoning, for job records: m_data can now reclaim too, so the
    // output file needs to be open before that ever happens, not only
    // at the very end.
    m_trace.start_simulated_trace(m_params.get_outfile(), m_params.m_msec_output);

    if (m_trace.dcols().get_trace_mode() == TraceMode::REPLAY) {
        // Replay-format input (begin_time/end_time present): don't consult
        // the scheduler at all - reuse the same bypass logic the standalone
        // tracer binary uses, driven into Trace's own owned context so the rest
        // of this class (write_simulated_trace(), write_resource_trace())
        // sees the result exactly as if the scheduler had run.
        m_trace.run_job_trace();
    } else {
        // Batch mode: Submit all jobs upfront, then advance to infinity
        // This uses the streaming API internally
        for (num_jobs_t i = 0; i < m_trace.data().size(); ++i) {
            const auto& job = m_trace.job_at(i);
            sim_time_t submit_time = convert_epoch<sim_time_t>(job.get_submit_time());
            submit_job(i, submit_time);
        }

        // Batch mode: advance to infinity to process all jobs
        // The loop will exit when both wait_queue and event_queue are empty
        advance_to(std::numeric_limits<sim_time_t>::max());
    }

    // m_jobs_completed is tracked incrementally during the run itself
    // (see the event-processing loop above) - no need to recompute it
    // here, and doing so by iterating m_trace.data() directly would
    // now be wrong anyway, since reclaimed jobs are no longer there to
    // recount.
    if (m_params.m_verbose) {
        std::cout << "Simulation complete\n" +
                     std::string("Jobs submitted: ") + std::to_string(m_jobs_submitted) + "\n" +
                     std::string("Jobs completed: ") + std::to_string(m_jobs_completed) + "\n";
    }
}

void Simulation::print_stats(std::ostream& os) const
{
    os << "=== Simulation Statistics ===" << std::endl;
    os << "Total jobs: " << (m_trace.data().size() + m_trace.num_reclaimed()) << std::endl;
    os << "Jobs submitted: " << m_jobs_submitted << std::endl;
    // m_trace.completed_count() (populated via write_job_line(), called
    // both at reclaim time and by write_simulated_trace()'s final
    // flush - already run by the time this is called, see sim.cpp's
    // caller) counts every completed job regardless of whether it's
    // since been reclaimed from m_data - m_jobs_completed only tracks
    // in-flight completions during the run itself and isn't used here.
    os << "Jobs completed: " << m_trace.completed_count() << std::endl;
    os << "Current time: " << format_sim_time(m_current_time, m_params.m_msec_output) << std::endl;
    os << "Total nodes: " << m_params.m_total_nodes << std::endl;

    // Calculate metrics - sum+count already accumulated incrementally in
    // Trace as each job was written out (see write_job_line()), so this
    // is correct even for jobs already reclaimed from m_data by now.
    if (m_trace.completed_count() > 0) {
        const auto completed = m_trace.completed_count();

        // Unlike Current time/Makespan above, these are computed averages
        // (division results), which commonly have a fractional part even
        // with integer-second input data (e.g. 220/3 = 73.333...) - that
        // precision is meaningful and was shown by default before
        // msec_output existed, so it's preserved here regardless of
        // msec_output's setting, rather than routed through
        // format_sim_time (whose integer-truncation default is for
        // matching existing trace-output files' conventions, not for
        // these summary statistics).
        os << "Average wait time: " << (m_trace.wait_time_sum() / completed) << " sec" << std::endl;
        os << "Average turnaround time: " << (m_trace.turnaround_time_sum() / completed) << " sec" << std::endl;
        os << "Makespan: " << format_sim_time(m_trace.makespan(), m_params.m_msec_output) << " sec" << std::endl;
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

    // No .reserve() here anymore - load_data() sizes m_data's capacity
    // itself (resolve_job_store_capacity()), same convention as the wait
    // queue and resource-history.
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

void Simulation::write_simulated_trace()
{
    m_trace.write_simulated_trace(m_params.get_outfile(), m_params.m_msec_output);
    if (m_params.m_verbose && !m_params.get_outfile().empty()) {
        std::cout << "Simulated trace written to: " << m_params.get_outfile() << std::endl;
    }
}

void Simulation::write_resource_trace(const std::string& filename)
{
    if (filename.empty()) {
        return;
    }

    // Trace's own context is populated identically regardless of trace
    // mode (both go through the same process_single_event()/
    // process_events_until() choke points), so this is unconditional now -
    // no more separate simulation-mode-only tracking to maintain here.
    m_trace.write_resource_trace(filename, m_params.m_total_nodes, m_params.m_msec_output);
    if (m_params.m_verbose) {
        std::cout << "Resource trace written to: " << filename << std::endl;
    }
}

// Public API methods for online/streaming simulation mode
// Allow external code (e.g., gRPC server) to feed jobs and control simulation

job_no_t Simulation::append_job(sim_time_t submit_time, num_nodes_t num_nodes,
                                 const std::string& queue, tdiff_t limit_time)
{
    if (submit_time < m_current_time) {
        throw std::runtime_error("Cannot append job with submit_time < current_time. "
                                "submit_time=" + std::to_string(submit_time) +
                                " but current_time=" + std::to_string(m_current_time));
    }

    time_t sec = static_cast<time_t>(submit_time);
    float frac = submit_time - sec;
    epoch_t submit_epoch = {sec, frac};

    job_queue_t q;
    set_by(q, queue);

    return m_trace.append_job(m_current_time, submit_epoch, num_nodes, q,
                               static_cast<timeout_t>(limit_time));
}

void Simulation::submit_job(job_no_t job_idx, sim_time_t submit_time)
{
    // Validate preconditions
    if (submit_time < m_current_time) {
        throw std::runtime_error("Cannot submit job with submit_time < current_time. "
                                "Job " + std::to_string(job_idx) + " has submit_time=" +
                                std::to_string(submit_time) + " but current_time=" +
                                std::to_string(m_current_time));
    }

    // Submit to scheduler (scheduler maintains internal wait queue)
    // Scheduler uses time_limit as the best estimator for planning
    // job_at() below throws its own clear, correctly-bounds-checked
    // error if job_idx is invalid - accounting for m_num_reclaimed,
    // unlike a manual "job_idx >= m_trace.data().size()" check would
    // (data().size() is m_data's *current* physical count, not the
    // total job count ever seen, once anything's been reclaimed).
    auto& job = m_trace.job_at(job_idx);
    tdiff_t run_time_estimate = job.get_limit_time();
    num_nodes_t nodes = job.get_num_nodes();

    if (nodes > m_params.m_total_nodes) {
        // This job can never be scheduled, regardless of how long the
        // simulation runs - total_nodes is fixed for the whole run, so
        // no future state ever frees up enough capacity. Reject it here,
        // before it ever enters the wait queue: leaving it there would
        // silently block every job behind it in FCFS order, and its
        // begin_time/end_time would stay at unscheduled_sentinel()
        // forever - exactly the "reaches output still unresolved" state
        // that shouldn't be possible.
        //
        // Also mark submit_time as the sentinel: m_data's front-reclaim
        // sweep treats that as "skip immediately, will never resolve" -
        // otherwise a rejected job at the front would stall the sweep
        // forever, since end_time never resolves for it either.
        job.set_submit_time(Job_Record::unscheduled_sentinel());
        std::cerr << "Job " << job_idx << " rejected: requests " << nodes
                  << " nodes, exceeds total_nodes (" << m_params.m_total_nodes
                  << "); this job can never be scheduled." << std::endl;
        return;
    }

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
            num_nodes_t free_nodes = m_params.m_total_nodes - m_trace.get_nodes_in_use();
            auto jobs_to_run = m_scheduler->schedule(free_nodes, m_running_jobs, m_current_time);

            if (jobs_to_run.empty()) {
                break;
            }

            // Process ALL jobs returned by scheduler (backfilling can return multiple)
            for (job_no_t job : jobs_to_run) {
                m_trace.insert_job(job, m_current_time);
                m_running_jobs[job] = m_current_time;
                m_jobs_submitted++;

                // Records a resource-history sample internally (Trace's own
                // Context, via process_events_until()) - no separate call needed.
                m_trace.run_until_inclusive(m_current_time);
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
    while (active_count > 0 || !m_trace.pending_events().empty() || next_arrival < std::numeric_limits<sim_time_t>::max()) {
        if (m_params.m_verbose) {
            std::cout << "Loop iter: active=" << active_count
                      << " events=" << m_trace.pending_events().size()
                      << " next_arrival=" << next_arrival
                      << " time=" << m_current_time << std::endl;
        }
        // next_arrival already computed above

        // Find next replay event time
        bool has_replay_event = !m_trace.pending_events().empty();
        sim_time_t next_replay_time = std::numeric_limits<sim_time_t>::max();
        [[maybe_unused]] bool next_is_start = false;
        if (has_replay_event) {
            const auto& event = *m_trace.pending_events().begin();
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

            while (!m_trace.pending_events().empty()) {
                const auto& event = *m_trace.pending_events().begin();
                sim_time_t event_time = convert_epoch<sim_time_t>(event.get_time());

                if (event_time != m_current_time) {
                    break;  // No more events at current_time
                }

                bool is_end = !event.is_arrival();
                job_no_t event_job_idx = event.get_job_idx();

                // Process this event (END or START) - records a
                // resource-history sample internally (Trace's own Context).
                m_trace.process_single_event();

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
                          << " events=" << m_trace.pending_events().size()
                          << " time=" << m_current_time << std::endl;
            }
            // No events to process - exit loop
            break;
        }

        // Scheduling loop - let scheduler make decisions after processing END events
        if (should_schedule) {
            // Keep calling scheduler until it can't start any more jobs
            while (true) {
                num_nodes_t free_nodes = m_params.m_total_nodes - m_trace.get_nodes_in_use();

                auto jobs_to_run = m_scheduler->schedule(free_nodes, m_running_jobs, m_current_time);

                if (jobs_to_run.empty()) {
                    break;  // Scheduler can't start anything else
                }

                // Process ALL jobs returned by scheduler (backfilling can return multiple)
                for (job_no_t job : jobs_to_run) {
                    m_trace.insert_job(job, m_current_time);
                    m_running_jobs[job] = m_current_time;
                    m_jobs_submitted++;

                    // Process this START event - records a resource-history
                    // sample internally (Trace's own Context).
                    while (!m_trace.pending_events().empty()) {
                        const auto& event = *m_trace.pending_events().begin();
                        sim_time_t event_time = convert_epoch<sim_time_t>(event.get_time());

                        // Only process START events at current_time for this job
                        if (event_time != m_current_time) break;
                        if (!event.is_arrival()) break;  // Hit an END event, stop (shouldn't happen)

                        m_trace.process_single_event();
                        break;  // Process only one START event per job
                    }
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
                  << " events=" << m_trace.pending_events().size()
                  << " time=" << m_current_time << std::endl;
    }

    // Don't record spurious final state - last event already recorded the final state

    // Honor the documented postcondition (m_current_time == target_time)
    // even when the loop above exited early because nothing was left to
    // process before target_time (an idle gap - e.g. all currently-known
    // jobs finished, and the next arrival, if any, is later than
    // target_time). Without this, m_current_time stays stuck at the last
    // real event, silently understating elapsed time to any caller of
    // get_current_time() and weakening submit_job()'s own precondition
    // check (submit_time < m_current_time) against a stale value. This
    // is pure bookkeeping - every scheduling decision above was already
    // made using real event times, never target_time, so this can't
    // change any of them.
    m_current_time = target_time;
}

num_nodes_t Simulation::get_nodes_in_use() const
{
    return m_trace.get_nodes_in_use();
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
