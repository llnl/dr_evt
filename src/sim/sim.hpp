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
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_SIM_SIM_HPP
