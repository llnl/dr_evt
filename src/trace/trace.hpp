/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_TRACE_TRACE_HPP
#define DR_EVT_TRACE_TRACE_HPP

#include <cstdlib>
#include <vector>
#include <string>
#include <iostream>
#include <map>

#include "common.hpp"
#include "trace/data_columns.hpp"
#include "trace/job_record.hpp"
#include "trace/dr_event.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

class Trace {
  public:
    using trace_data_t = std::vector<Job_Record>;
    using reserved_t = std::vector<period_t>;

  protected:
    const std::string m_fname; ///< Name of the input datafile
    Data_Columns m_dcols; ///< Header info and column filter
    trace_data_t m_data; ///< Job trace data
  #if MARK_DAT_PERIOD
    /// Period where resources were unavailable for batch jobs
    reserved_t m_reserved;
  #endif

    /// Timezone metadata for human-readable output
    std::string m_default_timezone; ///< Default timezone offset (e.g., "+00:00" for UTC)
    std::map<std::string, std::string> m_queue_timezones; ///< Per-queue timezone overrides

  public:
    /// Tracing context, i.e., temporary data while running simulation
    /// Made public to allow external simulation controllers (e.g., gRPC) to manage context
    struct Context {
      #if MARK_DAT_PERIOD
        num_jobs_t m_pAll_cnt; // On-going pAll job
        epoch_t m_dat_start;
        epoch_t m_dat_end;
        tdiff_t m_dat_span;
        job_queue_t m_prev_job_q;
      #endif
        num_nodes_t m_n_nodes_in_use;
        event_q_t m_evtq;

        Context();
        std::string to_string() const;
    };
    Trace(const std::string& fname);
    Trace(const std::string& fname, const std::string& format);
    Trace(const std::string& fname, const std::string& format,
          const std::string& timestamp_format, const std::string& timezone);

    /// Allow access to the header info and column filter
    const Data_Columns& dcols() const { return m_dcols; }

    /// Load job trace data from a file
    int load_data(num_jobs_t n_lines_to_read = static_cast<num_jobs_t>(0u));

    /// Allow write access to the job trace data
    trace_data_t& data() { return m_data; }
    /// Allow read-only access to the job trace data
    const trace_data_t& data() const { return m_data; }

    /**
     *  Run the trace from the begining to the end. i.e., run the simulation
     *  of 3 job events--submit, start, and end--in order to find out how many
     *  nodes were in use at the time of each job submission.
     */
    void run_job_trace();

    /**
     * NEW SIMULATION API: Insert a job into the event queue
     * Creates start and end events for the job at specified times
     * @param job_idx Index of job in m_data
     * @param start_time When the job should start
     * @param ctx Simulation context (event queue and resource state)
     */
    void insert_job(job_no_t job_idx, sim_time_t start_time, Context& ctx);

    /**
     * NEW SIMULATION API: Run simulation until (but not including) target time
     * Processes all events with time < target_time
     * @param ctx Simulation context
     * @param target_time Time to run until (exclusive)
     */
    void run_until_exclusive(Context& ctx, sim_time_t target_time);

    /**
     * NEW SIMULATION API: Run simulation until and including target time
     * Processes all events with time <= target_time
     * @param ctx Simulation context
     * @param target_time Time to run until (inclusive)
     */
    void run_until_inclusive(Context& ctx, sim_time_t target_time);

    /**
     * NEW SIMULATION API: Process exactly one event from the replay queue
     * Processes the earliest event in the queue, regardless of its time
     * @param ctx Simulation context
     * @return true if an event was processed, false if queue was empty
     */
    bool process_single_event(Context& ctx);

    /**
     * NEW SIMULATION API: Create a new context for simulation
     * @return Fresh context with empty event queue
     */
    Context create_context() { return Context(); }

    /**
     * NEW SIMULATION API: Get current number of nodes in use
     * @param ctx Simulation context
     * @return Number of nodes currently allocated
     */
    num_nodes_t get_nodes_in_use(const Context& ctx) const {
        return ctx.m_n_nodes_in_use;
    }

    /**
     *  Print out the job trace with extra information obtained from simulation.
     */
    std::ostream& print(std::ostream& os) const;

    /// Print out the total span of time of the trace
    std::ostream& print_span(std::ostream& os) const;

  #if MARK_DAT_PERIOD
    std::ostream& print_DAT(std::ostream& os);
  #endif

  #if MARK_DAT_PERIOD
    const reserved_t & get_reserved() const { return m_reserved; }
  #endif

    /**
     * @brief Set default timezone for the trace
     * @param tz_offset Timezone offset string (e.g., "-08:00", "+00:00")
     */
    void set_default_timezone(const std::string& tz_offset) {
        m_default_timezone = tz_offset;
    }

    /**
     * @brief Set timezone for a specific queue
     * @param queue Queue name (e.g., "pbatch")
     * @param tz_offset Timezone offset string
     */
    void set_queue_timezone(const std::string& queue, const std::string& tz_offset) {
        m_queue_timezones[queue] = tz_offset;
    }

    /**
     * @brief Get timezone for a queue (returns default if not overridden)
     * @param queue Queue name
     * @return Timezone offset string
     */
    std::string get_queue_timezone(const std::string& queue) const {
        auto it = m_queue_timezones.find(queue);
        return (it != m_queue_timezones.end()) ? it->second : m_default_timezone;
    }

  protected:
    void process_events_until(Context& ctx, const epoch_t& t_sub);
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_TRACE_HPP
