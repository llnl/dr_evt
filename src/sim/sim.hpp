/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SIM_HPP
#define DR_EVT_SIM_SIM_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#include <cmath>
#include <deque>
#include <limits>
#include <unordered_map>
#include <vector>
#include <memory> // unique_ptr
#include <iostream>
#include <random>

#include "common.hpp"
#include "params/sim_params.hpp"
#include "trace/trace.hpp"
#include "trace/dr_event.hpp"
#include "sim/scheduler_base.hpp"

namespace dr_evt {

// Forward declarations
template<size_t BlockSize> class BlockWaitQueue;
class SchedulerBase;

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

    /// Job scheduler (polymorphic - FCFS/SJF/LJF)
    std::unique_ptr<SchedulerBase> m_scheduler;

    /// Event queue (submit, start, end events)
    event_q_t m_event_queue;

    /// Current simulation time
    sim_time_t m_current_time;

    /// Counters
    num_jobs_t m_jobs_completed;
    num_jobs_t m_jobs_submitted;

    /// Random number generator for duration sampling
    std::mt19937 m_rng;

    // NOTE: Wait queue now owned by scheduler (m_scheduler maintains internal queue)

    /// Running jobs for streaming mode (job_idx -> start_time)
    std::map<job_no_t, sim_time_t> m_running_jobs;

    /// Queue length statistics for performance analysis
    mutable size_t m_queue_length_sum;
    mutable size_t m_queue_length_samples;
    mutable size_t m_queue_length_peak;

  public:
    /**
     * Constructor
     * @param[in] params Immutable simulation configuration.
     */
    Simulation(const Sim_Params& params);

    /**
     * @brief Run a complete batch simulation for the configured trace.
     * @details
     * Loads and prepares the input trace when necessary, submits every job,
     * and drains the event queue. For externally fed work, use append_job()
     * or append_jobs() followed by advance_to() instead.
     */
    void run();

    /**
     * @brief Write a human-readable statistics report.
     * @param[in,out] os Destination stream receiving the report.
     */
    void print_stats(std::ostream& os) const;

    /**
     * @brief Write the simulated job trace to the configured output file.
     * @details
     * The destination is Sim_Params::get_outfile(). This does not advance
     * simulation time or otherwise change scheduling state.
     */
    void write_simulated_trace();

    /**
     * @brief Write resource-allocation history to a CSV file.
     * @param[in] filename Destination CSV path.
     */
    void write_resource_trace(const std::string& filename);

    /**
     * One job's data for append_jobs() - the same fields append_job()
     * takes individually (submit_time here using the same external,
     * pre-epoch-conversion sim_time_t append_job() takes, not Trace's
     * internal epoch_t), grouped so several new jobs can be passed in a
     * single call.
     */
    struct Job_Append_Request {
        sim_time_t submit_time;
        num_nodes_t num_nodes;
        std::string queue;
        tdiff_t limit_time;
    };

    /**
     * @brief Add a new job record and enqueue it for scheduling.
     *
     * Append a genuinely new job - the streaming counterpart to
     * load_data(), for a job the trace has never seen before (as
     * opposed to the internal submit_job() helper, which enqueues a job
     * already present in m_data). Adds the job to the store and immediately
     * enqueues it for scheduling.
     *
     * @param[in] submit_time Arrival time, which must be >= current time.
     * @param[in] num_nodes Number of nodes the job requests.
     * @param[in] queue Queue name (for example, "pbatch").
     * @param[in] limit_time User-estimated time limit in seconds.
     * @return New Trace job identifier as job_no_t.
     * @see submit_job()
     * @see SchedulerBase::insert_job()
     */
    job_no_t append_job(sim_time_t submit_time, num_nodes_t num_nodes,
                         const std::string& queue, tdiff_t limit_time);

    /**
     * @brief Add several new job records and enqueue all of them.
     *
     * Append several genuinely new jobs in one call - the batch
     * counterpart to append_job(), for the same never-seen-before case.
     * Validates every request's submit_time >= current_time before any
     * of them are appended (same precondition append_job() enforces per
     * job) - combined with Trace::append_jobs()'s own validation
     * (ordering, capacity for the whole batch), this call is fully
     * all-or-nothing: nothing is appended unless the whole batch can be.
     * See Trace::append_jobs()'s own doc comment for the rest.
     *
     * @param[in] requests New jobs' data in submit_time order (see
     *        Trace::append_jobs() for why - this function forwards
     *        requests as-is, so pass them there already sorted).
     * @return New Trace job identifiers in the same order as requests.
     * @see append_job()
     * @see submit_job()
     */
    std::vector<job_no_t> append_jobs(const std::vector<Job_Append_Request>& requests);

  protected:
    /**
     * @brief Validate an existing job and enqueue it in the scheduler.
     *
     * Submit an already-known job to the scheduler's waiting queue.
     * This is an implementation detail used by append_job(), append_jobs(),
     * and batch loading; external streaming callers use append_job().
     * The internal scheduler will decide when to start the job based on
     * resources and backfilling policy.
     *
     * @param[in] job_idx Existing Trace job identifier to submit.
     * @param[in] submit_time Arrival time, which must be >= current time.
     *
     * This does not add a job record to Trace: job_idx must already identify
     * one. It validates the arrival, records arrival-time accounting, then
     * calls SchedulerBase::insert_job() to enqueue the scheduling data.
     * Call advance_to() to let the scheduler select a job to start; that
     * path calls Trace::insert_job() to record the start and end events.
     *
     * @see append_job()
     * @see SchedulerBase::insert_job()
     * @see Trace::insert_job()
     */
    void submit_job(job_no_t job_idx, sim_time_t submit_time);

  public:
    /**
     * @brief Advance simulation time and start eligible queued jobs.
     *
     * Advance simulation to target time (streaming mode)
     * Processes all events up to target_time and lets the scheduler make
     * decisions about which jobs to start.
     *
     * @param[in] target_time Time to advance to, which must be >= current time.
     *
     * PRECONDITION: Caller guarantees no jobs will be submitted with
     * submit_time < target_time. This means either:
     * - All jobs have already been submitted, OR
     * - External tool knows the next job arrival is at >= target_time
     *
     * POSTCONDITION: m_current_time == target_time, and all scheduling
     * decisions have been made up to that time.
     *
     * Jobs selected by the scheduler are recorded through Trace::insert_job().
     * @see submit_job()
     * @see Trace::insert_job()
     */
    void advance_to(sim_time_t target_time);

    /**
     * Get number of nodes currently in use (for monitoring)
     * @return Allocated-node count as num_nodes_t.
     */
    num_nodes_t get_nodes_in_use() const;

    /**
     * Get current simulation time (for monitoring)
     * @return Current simulation time as sim_time_t.
     */
    sim_time_t get_current_time() const { return m_current_time; }

    /**
     * Get trace data (for external access in streaming mode)
     * @return Mutable reference to the simulation Trace.
     */
    Trace& get_trace() { return m_trace; }
    const Trace& get_trace() const { return m_trace; }

    // ========================================================================
    // Monitoring and Statistics API for Python/External Tools
    // ========================================================================

    /**
     * Get number of available (free) nodes
     * @return Unallocated-node count as num_nodes_t.
     */
    num_nodes_t get_available_nodes() const {
        return m_params.m_total_nodes - get_nodes_in_use();
    }

    /**
     * Get count of jobs currently active (arrived but not yet scheduled).
     *
     * Uses SchedulerBase::active_job_count(), which is correct for all four
     * scheduler implementations - see SchedulerBase::sync_to() for the
     * full explanation of how eligibility tracking stays fresh without
     * this method needing to pass or check a time itself. Safe to call
     * with no time argument because nothing can call this while
     * advance_to() is mid-execution (single-threaded, no reentrancy) -
     * by the time this runs, the scheduler is already synced to
     * m_current_time from the last completed advance_to() call (or from
     * construction, if none has run yet).
     *
     * @return Waiting-job count as size_t.
     */
    size_t get_active_job_count() const {
        return m_scheduler->active_job_count();
    }

    /**
     * Get estimated time for FCFS head to start
     * Returns the shadow time (earliest time head of queue can start)
     * @return Estimated start time as sim_time_t, or -1 when the queue is empty.
     */
    sim_time_t get_fcfs_head_shadow_time() const {
        if (m_scheduler->active_job_count() == 0) {
            return -1.0;
        }
        return m_scheduler->get_fcfs_reservation_time();
    }

    /**
     * A point-in-time FCFS/EASY reservation snapshot. Release events use the
     * time-limit estimates used by SchedulerBase::calculate_fcfs_reservation,
     * rather than actual runtimes, so the projection and shadow time agree.
     */
    struct Backfill_Window {
        struct Resource_Release {
            sim_time_t time;
            num_nodes_t nodes_released;
        };

        sim_time_t current_time;
        num_nodes_t available_nodes;
        sim_time_t shadow_time;  // -1 when no FCFS head is waiting
        std::vector<Resource_Release> releases;
    };

    /**
     * @brief Return the current EASY-backfilling reservation projection.
     * @details
     * The result is a snapshot: available nodes and release times reflect
     * current scheduler state, while release times are based on the same
     * time-limit estimates used for the FCFS reservation.
     * @return Backfill_Window value for the current simulation time.
     */
    Backfill_Window get_backfill_window() const;

    /**
     * Get detailed scheduling statistics
     * @return Structure with wait times, turnaround, utilization
     */
    struct Statistics {
        num_jobs_t jobs_submitted;
        num_jobs_t jobs_completed;
        num_jobs_t jobs_running;
        num_jobs_t jobs_waiting;
        sim_time_t current_time;
        num_nodes_t total_nodes;
        num_nodes_t nodes_in_use;      // instantaneous snapshot at current_time
        num_nodes_t nodes_available;   // instantaneous snapshot at current_time
        double utilization;  // time-averaged over [0, makespan]: total
                              // node-seconds consumed by completed jobs,
                              // divided by (total_nodes * makespan) - NOT
                              // an instantaneous snapshot, since callers
                              // computing "overall" utilization after a
                              // run typically do so once the cluster has
                              // gone idle again, where an instantaneous
                              // reading would always show 0
        tdiff_t avg_wait_time;
        tdiff_t avg_turnaround_time;
        sim_time_t makespan;
    };

    /**
     * @brief Calculate aggregate and point-in-time simulation statistics.
     * @return Statistics value derived from the trace and current scheduler state.
     */
    Statistics get_statistics() const;

    /**
     * Run simulation until just before target_time, excluding events at target_time
     * @param[in] target_time Exclusive upper time bound as sim_time_t.
     */
    void run_until_exclusive(sim_time_t target_time) {
        // Advance to just before target_time
        // Events at exactly target_time will not be processed
        if (target_time > m_current_time) {
            // Find the last event time < target_time
            sim_time_t advance_time = m_current_time;
            for (const auto& evt : m_event_queue) {
                const auto& ts = evt.get_time();
                sim_time_t evt_time = static_cast<sim_time_t>(ts.first) + ts.second;
                if (evt_time < target_time && evt_time > advance_time) {
                    advance_time = evt_time;
                }
            }
            if (advance_time > m_current_time) {
                advance_to(advance_time);
            }
        }
    }

    /**
     * Initialize simulation: load trace data, sort by submission time, and
     * determine job durations (simulation mode only). Resets simulation
     * state (m_current_time, job counters) for a fresh run.
     *
     * Public streaming callers (Python bindings, gRPC server) must call
     * this instead of calling get_trace().load_data() directly - that
     * skips the sort and duration-determination steps below, which was a
     * real, previously undetected bug affecting both the existing Python
     * bindings and an early gRPC server draft: jobs loaded that way get
     * silently wrong actual-duration data, which cascades into wrong
     * scheduling decisions and wrong avg_wait_time/avg_turnaround_time/
     * makespan statistics, with no error raised anywhere.
     *
     * @param[in] max_jobs Maximum jobs to load. 0 (the default)
     *                 falls back to m_params.m_max_jobs if that was set,
     *                 or no limit otherwise - this is what run() relies on
     *                 for batch mode. Streaming callers should pass an
     *                 explicit value here instead of relying on Sim_Params.
     * @return Number of jobs loaded as num_jobs_t.
     */
    num_jobs_t initialize_trace(num_jobs_t max_jobs = 0);

  protected:
    /**
     * @brief Process an arrival event for an existing trace job.
     * @param[in] job_idx Identifier of the arriving job.
     */
    void process_submit_event(job_no_t job_idx);

    /**
     * @brief Process a scheduler-selected job start event.
     * @param[in] job_idx Identifier of the job to start.
     */
    void process_start_event(job_no_t job_idx);

    /**
     * @brief Process a job completion event and release its resources.
     * @param[in] job_idx Identifier of the completing job.
     */
    void process_end_event(job_no_t job_idx);

    /**
     * @brief Process the chronologically next queued event.
     * @return true when an event was processed; false when the queue is empty.
     */
    bool advance_to_next_event();

    /**
     * @brief Queue start events for jobs selected by the scheduler.
     * @param[in] jobs Identifiers of jobs that can start at the current time.
     */
    void schedule_start_events(const std::vector<job_no_t>& jobs);

    /**
     * @brief Queue the completion event for a started job.
     * @param[in] job_idx Identifier of the running job.
     * @param[in] start_time Time at which the job started.
     */
    void schedule_end_event(job_no_t job_idx, sim_time_t start_time);

    /**
     * Determine actual durations for all jobs (simulation mode only)
     * Called during initialization before simulation starts
     */
    void determine_job_run_time();

    /**
     * Same as determine_job_run_time(), but only for the given jobs -
     * for progressive loading, where each file's jobs need this done
     * once they're loaded, not just once for the initial batch.
     */
    void determine_job_run_time(const std::vector<job_no_t>& job_nos);

    /**
     * @brief Determine the simulated duration of one job.
     * @param[in,out] job Trace record whose simulated duration is updated.
     */
    void determine_one_job_run_time(Job_Record& job);

    /**
     * @brief Run the configured sequence of trace files incrementally.
     * @details
     * Loads each file in turn, submits its jobs in submit-time order, then
     * advances to its last arrival before loading the next file. A final
     * advance drains outstanding work. REPLAY input is not supported.
     */
    void run_progressive();

    /**
     * Sample job duration from distribution
     * @param[in] time_limit User-provided time limit.
     * @param[in] dist Distribution type.
     * @param[in] scale Distribution scale factor.
     * @param[in] stddev Distribution standard-deviation factor.
     * @return Sampled duration as tdiff_t.
     */
    tdiff_t sample_run_time(tdiff_t time_limit,
                            DistributionType dist,
                            double scale,
                            double stddev);
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_SIM_SIM_HPP
