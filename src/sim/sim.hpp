/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SIM_HPP
#define DR_EVT_SIM_SIM_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#include <cmath>
#include <limits>
#include <unordered_map>
#include <memory> // unique_ptr
#include <iostream>
#include <random>

#include "common.hpp"
#include "params/sim_params.hpp"
#include "trace/trace.hpp"
#include "trace/dr_event.hpp"
#include "sim/scheduler.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_sim
 *  @{ */

/**
 * Main simulation class that orchestrates job scheduling simulation
 * with backfilling using discrete event simulation
 */
class Simulation {
  protected:
    /// Simulation parameters
    const Sim_Params& m_params;

    /// Job trace data
    Trace m_trace;

    /// Job scheduler
    Scheduler m_scheduler;

    /// Event queue (submit, start, end events)
    event_q_t m_event_queue;

    /// Current simulation time
    sim_time_t m_current_time;

    /// Counters
    num_jobs_t m_jobs_completed;
    num_jobs_t m_jobs_submitted;

    /// Random number generator for duration sampling
    std::mt19937 m_rng;

    /// Replay context for event processing
    Trace::Context m_replay_ctx;

    /// Waiting queue for streaming mode (jobs submitted but not yet started)
    std::set<job_no_t> m_wait_queue;

    /// Running jobs for streaming mode (job_idx -> start_time)
    std::map<job_no_t, sim_time_t> m_running_jobs;

    /// Resource state history: (time, free_nodes, allocated_nodes) for verification
    mutable std::vector<std::tuple<sim_time_t, num_nodes_t, num_nodes_t>> m_resource_history;

  public:
    /**
     * Constructor
     * @param params Simulation parameters
     */
    Simulation(const Sim_Params& params);

    /**
     * Run the simulation
     * Processes all events in the trace
     */
    void run();

    /**
     * Print simulation statistics
     */
    void print_stats(std::ostream& os) const;

    /**
     * Write simulated job trace to CSV file
     */
    void write_simulated_trace() const;

    /**
     * Get resource state history for verification
     * Returns: vector of (time, free_nodes, allocated_nodes)
     */
    const std::vector<std::tuple<sim_time_t, num_nodes_t, num_nodes_t>>& get_resource_history() const {
        return m_resource_history;
    }

    /**
     * Write resource state trace to file
     */
    void write_resource_trace(const std::string& filename) const;

    /**
     * Submit a job to the scheduler's waiting queue (streaming mode)
     * The internal scheduler will decide when to start the job based on
     * resources and backfilling policy.
     *
     * @param job_idx Job index to submit
     * @param submit_time When the job is submitted (must be >= current_time)
     *
     * NOTE: This only adds the job to the waiting queue. Call advance_to()
     * to let the scheduler make decisions and advance simulation time.
     */
    void submit_job(job_no_t job_idx, sim_time_t submit_time);

    /**
     * Advance simulation to target time (streaming mode)
     * Processes all events up to target_time and lets the scheduler make
     * decisions about which jobs to start.
     *
     * @param target_time Time to advance to (must be >= current_time)
     *
     * PRECONDITION: Caller guarantees no jobs will be submitted with
     * submit_time < target_time. This means either:
     * - All jobs have already been submitted, OR
     * - External tool knows the next job arrival is at >= target_time
     *
     * POSTCONDITION: m_current_time == target_time, and all scheduling
     * decisions have been made up to that time.
     */
    void advance_to(sim_time_t target_time);

    /**
     * Get number of nodes currently in use (for monitoring)
     * @return Number of allocated nodes
     */
    num_nodes_t get_nodes_in_use() const;

    /**
     * Get current simulation time (for monitoring)
     * @return Current simulation time
     */
    sim_time_t get_current_time() const { return m_current_time; }

    /**
     * Get trace data (for external access in streaming mode)
     * @return Reference to trace object
     */
    Trace& get_trace() { return m_trace; }
    const Trace& get_trace() const { return m_trace; }

  protected:
    /**
     * Initialize simulation
     * Loads trace data and creates initial submit events
     */
    void initialize();

    /**
     * Process a job submission event
     * @param job_idx Index of job being submitted
     */
    void process_submit_event(job_no_t job_idx);

    /**
     * Process a job start event
     * @param job_idx Index of job starting
     */
    void process_start_event(job_no_t job_idx);

    /**
     * Process a job end event
     * @param job_idx Index of job completing
     */
    void process_end_event(job_no_t job_idx);

    /**
     * Advance to the next event in the queue
     * @return True if an event was processed, false if queue is empty
     */
    bool advance_to_next_event();

    /**
     * Schedule start events for jobs that can run now
     * @param jobs List of job indices that can start
     */
    void schedule_start_events(const std::vector<job_no_t>& jobs);

    /**
     * Schedule end event for a job
     * @param job_idx Job index
     * @param start_time When the job started
     */
    void schedule_end_event(job_no_t job_idx, sim_time_t start_time);

    /**
     * Determine actual durations for all jobs (simulation mode only)
     * Called during initialization before simulation starts
     */
    void determine_job_durations();

    /**
     * Sample job duration from distribution
     * @param time_limit User-provided time limit
     * @param dist Distribution type
     * @param scale Scale factor
     * @param stddev Standard deviation factor
     * @return Sampled duration
     */
    tdiff_t sample_duration(tdiff_t time_limit,
                            DistributionType dist,
                            double scale,
                            double stddev);
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_SIM_SIM_HPP
