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
#include <fstream>
#include <map>
#include <boost/circular_buffer.hpp>

#include "common.hpp"
#include "trace/data_columns.hpp"
#include "trace/job_record.hpp"
#include "trace/dr_event.hpp"
#include "params/sim_params.hpp" // CircularOverflowPolicy

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

class Trace {
  public:
    /// Circular buffer, front-only eviction: a job's slot is reclaimed
    /// once safe (see is_evictable()), same shape as m_ctx.m_resource_history
    /// but with a real safety check instead of "always safe" - a job may
    /// still be running when its slot would otherwise be evicted. No
    /// compaction, no removed flag: out-of-order completions (backfilling)
    /// just sit in place until every job ahead of them (by job_no) has
    /// also been evicted - see OUT_TRACE_STREAMING.md.
    using trace_data_t = boost::circular_buffer<Job_Record>;
    using reserved_t = std::vector<period_t>;

  protected:
    const std::string m_fname; ///< Name of the input datafile
    Data_Columns m_dcols; ///< Header info and column filter
    trace_data_t m_data; ///< Job trace data

    /// job_no of the oldest job still physically present in m_data - the
    /// whole translation layer between the permanent, ever-increasing
    /// job_no identifiers callers use and m_data's own physical slots:
    /// job_at(job_no) is just m_data[job_no - m_num_evicted]. Valid only
    /// because eviction is front-only with no compaction - if it ever
    /// removed from the middle, this single global offset couldn't
    /// describe the shift (jobs before vs. after the removed one would
    /// need different corrections). Advances only via evict_front_jobs().
    size_t m_num_evicted;
    size_t m_job_store_capacity; ///< 0 = auto-size to the job trace once loaded
    bool m_job_store_capacity_resolved;
    CircularOverflowPolicy m_job_store_overflow; ///< Fallback when the front isn't safe to evict and the buffer's full

    /// Running totals over every is_scheduled() job written via
    /// write_job_line() - accumulated there (the single choke point for
    /// both eviction-time and final-flush writes) so an evicted job's
    /// contribution isn't lost the way iterating m_data after the fact
    /// would miss it. Sum+count, not the full per-job list - trivial to
    /// maintain incrementally, same idea as record_resource_sample().
    num_jobs_t m_completed_count;
    tdiff_t m_wait_time_sum;
    tdiff_t m_turnaround_time_sum;
    sim_time_t m_makespan;
  #if MARK_DAT_PERIOD
    /// Period where resources were unavailable for batch jobs
    reserved_t m_reserved;
  #endif

    /// Timezone metadata for human-readable output
    std::string m_default_timezone; ///< Default timezone offset (e.g., "+00:00" for UTC)
    std::map<std::string, std::string> m_queue_timezones; ///< Per-queue timezone overrides

  public:
    /// Tracing context, i.e., temporary data while running simulation -
    /// now owned by Trace itself (m_ctx below), not supplied externally.
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

        /// (time, allocated_nodes) sampled every time an event changes
        /// occupancy. free_nodes isn't stored here since total_nodes isn't
        /// known to Context - it's derived by write_resource_trace() below.
        /// A circular buffer bounds memory for long-running/streaming
        /// sessions: every entry here is immediately safe to evict (this
        /// is a strictly time-ordered append log, unlike the job-record
        /// store, which must wait for a job's end_time to pass) so, unlike
        /// a wait queue or job store, eviction here never needs an
        /// abort/grow fallback - it always succeeds. Capacity is resolved
        /// lazily (see Trace::start_resource_trace()) since the natural
        /// default depends on the job trace's size, known only once
        /// loaded, not at Context's own construction time.
        boost::circular_buffer<std::pair<epoch_t, num_nodes_t>> m_resource_history;

        Context();
        std::string to_string() const;
    };

  protected:
    /// This Trace's own simulation context - owned here so that a Trace
    /// handed to any caller (Simulation, or a future streaming driver)
    /// always carries its own state together, rather than a caller having
    /// to keep a separate Context in sync with the right Trace.
    Context m_ctx;

    /// Requested capacity for m_ctx.m_resource_history (0 = auto-size to
    /// the job trace once loaded - see resolve_resource_history_capacity()).
    /// Set via set_resource_history_capacity()/start_resource_trace(),
    /// resolved lazily the first time a sample is recorded.
    size_t m_resource_history_capacity;
    bool m_resource_history_capacity_resolved;

    /// Set once start_resource_trace() opens a real output file - lets
    /// eviction flush incrementally during the run instead of only at
    /// the very end.
    std::ofstream m_resource_trace_ofs;
    num_nodes_t m_resource_trace_total_nodes;
    bool m_resource_trace_msec;

    /// Set once start_simulated_trace() opens a real output file - lets
    /// evict_front_jobs() write each job's line at eviction time, same
    /// reasoning as m_resource_trace_ofs above: a job's slot may be
    /// reused before write_simulated_trace()'s old one-pass-at-the-end
    /// write would ever see it.
    std::ofstream m_simulated_trace_ofs;
    bool m_simulated_trace_msec;

  public:
    Trace(const std::string& fname);
    Trace(const std::string& fname, const std::string& format);
    Trace(const std::string& fname, const std::string& format,
          const std::string& timestamp_format, const std::string& timezone);

    /// Allow access to the header info and column filter
    const Data_Columns& dcols() const { return m_dcols; }

    /// Load job trace data from a file
    int load_data(num_jobs_t n_lines_to_read = static_cast<num_jobs_t>(0u));

    /// Allow write access to the job trace data - for range-based
    /// iteration only (write_simulated_trace(), print(), etc.). Indexing
    /// this directly by job_no is wrong once anything's been evicted -
    /// use job_at(job_no) instead, which translates correctly.
    trace_data_t& data() { return m_data; }
    /// Allow read-only access to the job trace data - see data() above.
    const trace_data_t& data() const { return m_data; }

    /**
     * @brief Look up a job by its permanent job_no (not a raw physical
     * index - those shift as eviction advances). The single choke point
     * for every job_no-based lookup; throws clearly rather than reading
     * garbage if job_no was already evicted or was never inserted.
     */
    Job_Record& job_at(job_no_t job_no);
    const Job_Record& job_at(job_no_t job_no) const;

    /// Jobs evicted so far - add to data().size() for the true total
    /// ever loaded (data().size() alone undercounts once anything's
    /// been evicted).
    size_t num_evicted() const { return m_num_evicted; }

    /// Running stats over every is_scheduled() job seen so far via
    /// write_job_line() (both eviction-time and the final flush) -
    /// correct even once some jobs have been evicted from m_data, unlike
    /// re-deriving these by iterating data() directly.
    num_jobs_t completed_count() const { return m_completed_count; }
    tdiff_t wait_time_sum() const { return m_wait_time_sum; }
    tdiff_t turnaround_time_sum() const { return m_turnaround_time_sum; }
    sim_time_t makespan() const { return m_makespan; }

    /**
     * @brief Set the initial capacity for the job-record circular buffer.
     * Call before any processing begins - a no-op once the first job has
     * already been loaded (capacity is resolved lazily then, from the
     * job count if this was never called or was called with 0).
     */
    void set_job_store_capacity(size_t capacity) {
        m_job_store_capacity = capacity;
    }

    /// What to do if the buffer's full and the front job still isn't
    /// safe to evict (still running) - same convention as the wait queue.
    void set_job_store_overflow(CircularOverflowPolicy policy) {
        m_job_store_overflow = policy;
    }

    /**
     *  Run the trace from the begining to the end. i.e., run the simulation
     *  of 3 job events--submit, start, and end--in order to find out how many
     *  nodes were in use at the time of each job submission. This never
     *  consults a scheduler - begin_time/end_time are taken directly from
     *  the trace.
     *  @param resource_trace_file Optional path to also write a
     *         time,free_nodes,allocated_nodes resource-occupancy trace; no
     *         such file is written if left empty.
     *  @param total_nodes Pool size used only to derive free_nodes above.
     */
    void run_job_trace(const std::string& resource_trace_file = std::string(),
                        num_nodes_t total_nodes = static_cast<num_nodes_t>(0u));

    /**
     * NEW SIMULATION API: Insert a job into the event queue
     * Creates start and end events for the job at specified times
     * @param job_idx Index of job in m_data
     * @param start_time When the job should start
     */
    void insert_job(job_no_t job_idx, sim_time_t start_time);

    /**
     * NEW SIMULATION API: Run simulation until (but not including) target time
     * Processes all events with time < target_time
     * @param target_time Time to run until (exclusive)
     */
    void run_until_exclusive(sim_time_t target_time);

    /**
     * NEW SIMULATION API: Run simulation until and including target time
     * Processes all events with time <= target_time
     * @param target_time Time to run until (inclusive)
     */
    void run_until_inclusive(sim_time_t target_time);

    /**
     * NEW SIMULATION API: Process exactly one event from the replay queue
     * Processes the earliest event in the queue, regardless of its time
     * @return true if an event was processed, false if queue was empty
     */
    bool process_single_event();

    /**
     * NEW SIMULATION API: Get current number of nodes in use
     * @return Number of nodes currently allocated
     */
    num_nodes_t get_nodes_in_use() const {
        return m_ctx.m_n_nodes_in_use;
    }

    /// Read-only access to the pending-completion event queue, for a
    /// caller (Simulation) driving its own event loop against this Trace.
    const event_q_t& pending_events() const { return m_ctx.m_evtq; }

    /**
     *  Print out the job trace with extra information obtained from simulation.
     */
    std::ostream& print(std::ostream& os) const;

    /// Print out the total span of time of the trace
    std::ostream& print_span(std::ostream& os) const;

    /**
     * @brief Set the initial capacity for the resource-history circular
     * buffer. Call before any processing begins - a no-op once the first
     * sample has already been recorded (capacity is resolved lazily then,
     * from m_data's size if this was never called or was called with 0).
     * @param capacity Entry count; 0 = auto-size to the job trace, large
     *        enough that no entry needs evicting purely to make room.
     */
    void set_resource_history_capacity(size_t capacity) {
        m_resource_history_capacity = capacity;
    }

    /**
     * @brief Open filename early so resource-history samples evicted from
     * the circular buffer during the run get flushed to it incrementally,
     * instead of only being available at the very end via
     * write_resource_trace() below. Call once, before any processing
     * begins (e.g. before submit_job()/advance_to(), or run_job_trace()).
     * @param filename Output path; a no-op if empty - samples evicted
     *        before write_resource_trace() is later called are then
     *        simply discarded, which is fine since nobody asked for them.
     * @param total_nodes Pool size, used to derive free_nodes at write time.
     * @param msec Format timestamps with millisecond precision instead of
     *        truncating to whole seconds (matches Sim_Params::m_msec_output;
     *        tracer has no equivalent option, so it always leaves this false)
     */
    void start_resource_trace(const std::string& filename,
                               num_nodes_t total_nodes,
                               bool msec = false);

    /**
     * @brief Write this Trace's recorded resource-occupancy history to a
     * CSV file (same "time,free_nodes,allocated_nodes" format used by the
     * simulator). Shared by the standalone tracer and the scheduling
     * simulator, since both populate this history through the same
     * process_events_until()/process_single_event() code path.
     *
     * If start_resource_trace() was already called with the same filename,
     * this only flushes whatever's left buffered and closes the file -
     * everything evicted mid-run was already written incrementally. If
     * start_resource_trace() was never called, this opens filename fresh
     * and writes the buffer's current contents in one shot - the original,
     * pre-streaming behavior - but note anything evicted before this call
     * (from a capacity smaller than the trace) is then already gone.
     * @param filename Output path; no-op if empty
     * @param total_nodes Pool size, used to derive free_nodes at write time
     * @param msec Format timestamps with millisecond precision instead of
     *        truncating to whole seconds (matches Sim_Params::m_msec_output;
     *        tracer has no equivalent option, so it always leaves this false)
     */
    void write_resource_trace(const std::string& filename,
                               num_nodes_t total_nodes,
                               bool msec = false);

    /**
     * @brief Open filename early so a job's line gets written the moment
     * it's evicted from m_data, rather than only at the very end - same
     * reasoning as start_resource_trace() above. Call once, before any
     * processing begins.
     * @param filename Output path; a no-op if empty - jobs evicted
     *        before write_simulated_trace() is later called are then
     *        simply discarded (lost), which defeats the whole point of
     *        this call, so this should always be paired with it once
     *        m_data can actually evict.
     * @param msec Format timestamps with millisecond precision instead of
     *        truncating to whole seconds (matches Sim_Params::m_msec_output)
     */
    void start_simulated_trace(const std::string& filename, bool msec = false);

    /**
     * @brief Write this Trace's job records to a CSV file (same format
     * write_simulated_trace() always used). If start_simulated_trace()
     * was already called with the same filename, this only writes
     * whatever jobs are still currently in m_data and closes the file -
     * everything evicted mid-run was already written at eviction time.
     * If start_simulated_trace() was never called, opens filename fresh
     * and writes m_data's current contents in one shot - the original,
     * pre-streaming behavior.
     * @param filename Output path; no-op if empty
     * @param msec Format timestamps with millisecond precision instead of
     *        truncating to whole seconds
     */
    void write_simulated_trace(const std::string& filename, bool msec = false);

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
    void process_events_until(const epoch_t& t_sub);

    /// Resolve m_resource_history_capacity (0 -> sized from m_data) and
    /// call m_ctx.m_resource_history.set_capacity() - once, the first time
    /// any sample is recorded. A no-op on every call after the first.
    void resolve_resource_history_capacity();

    /// Write every entry currently in m_ctx.m_resource_history to
    /// m_resource_trace_ofs (if open) in one batch, then clear the buffer.
    /// Used both for incremental eviction-driven flushes during a run and
    /// for the final flush in write_resource_trace().
    void flush_resource_history();

    /// Record one (time, allocated) resource-occupancy sample - the single
    /// choke point process_events_until() and process_single_event() both
    /// go through, so eviction-on-full only needs implementing once. If the
    /// buffer is full, this flushes+clears it first (every entry is always
    /// safe to evict here - see m_resource_history's own comment - so this
    /// always succeeds, unlike a wait queue or job store, which may need an
    /// abort/grow fallback when nothing is currently evictable).
    void record_resource_sample(const epoch_t& time, num_nodes_t allocated);

    /// Resolve m_job_store_capacity (0 -> sized from the job count) and
    /// call m_data.set_capacity() - once, the first time load_data() or
    /// insert_job() needs room. A no-op on every call after the first.
    void resolve_job_store_capacity(num_jobs_t hint);

    /// True if the front-most job's slot can be reclaimed right now:
    /// rejected (submit_time == unscheduled_sentinel(), will never
    /// resolve - skip immediately) or genuinely finished
    /// (end_time <= current_time). False (still running or still
    /// waiting) means the sweep must stop here - no compaction, so
    /// nothing later in the buffer can be reclaimed either.
    bool is_front_evictable(sim_time_t current_time) const;

    /// Evict from the front while is_front_evictable() holds, advancing
    /// m_num_evicted. Called before every insert (load_data(),
    /// insert_job()) that might need room. If the buffer is still full
    /// afterward (front not yet safe), applies m_job_store_overflow -
    /// same fallback the wait queue already uses for the analogous case.
    void evict_front_jobs(sim_time_t current_time);

    /// Write one job's line to m_simulated_trace_ofs (if open and the
    /// job is_scheduled() - unscheduled/rejected jobs were never written
    /// by the original write_simulated_trace() either). The single
    /// choke point both evict_front_jobs() and write_simulated_trace()
    /// go through.
    void write_job_line(const Job_Record& job);
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_TRACE_HPP
