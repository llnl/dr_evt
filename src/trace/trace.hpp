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

/**
 * @brief One job's data for Trace::append_jobs() - the same fields
 * Trace::append_job() takes individually, grouped so a caller can pass
 * several new jobs (never before seen by this Trace) in a single call.
 * Deliberately not a Job_Record itself: append_jobs() only needs a new
 * job's externally-supplied attributes, not Job_Record's full internal
 * state (scheduling sentinels, simulated-vs-real flags, etc.), which
 * Job_Record's own constructor already initializes correctly.
 */
struct Job_Append_Request {
    epoch_t submit_time;
    num_nodes_t num_nodes;
    job_queue_t queue;
    timeout_t limit_time;
};

class Trace {
  public:
    /// Circular buffer, front-only reclaim: a job's slot becomes reusable
    /// once safe (see is_front_reclaimable()), same shape as
    /// m_ctx.m_resource_history but with a real safety check instead of
    /// "always safe" - a job may still be running when its slot would
    /// otherwise be reclaimed. No compaction, no removed flag:
    /// out-of-order completions (backfilling) just sit in place until
    /// every job ahead of them (by job_no) has also been reclaimed - see
    /// OUT_TRACE_STREAMING.md.
    ///
    /// Reclaiming a slot makes it reusable within this buffer's fixed,
    /// already-allocated capacity - it does not free memory back to the
    /// OS. In batch mode (the only mode today - see
    /// OUT_TRACE_STREAMING.md), load_data() sizes capacity to the whole
    /// trace before the run starts, so this essentially fires at most
    /// once per run: size() only shrinks afterward (nothing new is ever
    /// inserted once loading finishes), so the buffer stops being full()
    /// after the very first reclaim.
    using trace_data_t = boost::circular_buffer<Job_Record>;
    using reserved_t = std::vector<period_t>;

  protected:
    const std::string m_fname; ///< Name of the input datafile
    Data_Columns m_dcols; ///< Header info and column filter
    trace_data_t m_data; ///< Job trace data

    /// job_no of the oldest job still physically present in m_data - the
    /// whole translation layer between the permanent, ever-increasing
    /// job_no identifiers callers use and m_data's own physical slots:
    /// job_at(job_no) is just m_data[job_no - m_num_reclaimed]. Valid only
    /// because reclaiming is front-only with no compaction - if it ever
    /// removed from the middle, this single global offset couldn't
    /// describe the shift (jobs before vs. after the removed one would
    /// need different corrections). Advances only via reclaim_front_jobs().
    size_t m_num_reclaimed;
    size_t m_job_store_capacity; ///< 0 = auto-size to the job trace once loaded
    bool m_job_store_capacity_resolved;
    CircularOverflowPolicy m_job_store_overflow; ///< Fallback when the front isn't safe to reclaim and the buffer's full
    double m_memory_pressure_fraction = 0.0; ///< 0.0 = disabled; see set_memory_pressure_fraction()

    /// Tracks continuity across load_next_file() calls: the latest
    /// submit_time seen across every file loaded so far this way, so
    /// the next file's earliest submit_time can be checked against it
    /// (>=). m_has_loaded_a_file distinguishes "no previous file yet"
    /// (first call, nothing to check against) from a real value -
    /// simpler and clearer than reserving a sentinel epoch_t for the
    /// same purpose.
    bool m_has_loaded_a_file = false;
    epoch_t m_last_loaded_submit_time;

    /// Running totals over every is_scheduled() job written via
    /// write_job_line() - accumulated there (the single choke point for
    /// both reclaim-time and final-flush writes) so a reclaimed job's
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
        /// sessions: every entry here is immediately safe to reclaim (this
        /// is a strictly time-ordered append log, unlike the job-record
        /// store, which must wait for a job's end_time to pass) so, unlike
        /// a wait queue or job store, reclaiming here never needs an
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
    /// reclaim-driven flushes incrementally during the run instead of only at
    /// the very end.
    std::ofstream m_resource_trace_ofs;
    num_nodes_t m_resource_trace_total_nodes;
    bool m_resource_trace_msec;

    /// Set once start_simulated_trace() opens a real output file - lets
    /// reclaim_front_jobs() write each job's line at reclaim time, same
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
    /// this directly by job_no is wrong once anything's been reclaimed -
    /// use job_at(job_no) instead, which translates correctly.
    trace_data_t& data() { return m_data; }
    /// Allow read-only access to the job trace data - see data() above.
    const trace_data_t& data() const { return m_data; }

    /**
     * @brief Look up a job by its permanent job_no (not a raw physical
     * index - those shift as reclaiming advances). The single choke point
     * for every job_no-based lookup; throws clearly rather than reading
     * garbage if job_no was already reclaimed or was never inserted.
     */
    Job_Record& job_at(job_no_t job_no);
    const Job_Record& job_at(job_no_t job_no) const;

    /// Jobs reclaimed so far - add to data().size() for the true total
    /// ever loaded (data().size() alone undercounts once anything's
    /// been reclaimed).
    size_t num_reclaimed() const { return m_num_reclaimed; }

    /// Running stats over every is_scheduled() job seen so far via
    /// write_job_line() (both reclaim-time and the final flush) -
    /// correct even once some jobs have been reclaimed from m_data, unlike
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
    /// safe to reclaim (still running) - same convention as the wait queue.
    void set_job_store_overflow(CircularOverflowPolicy policy) {
        m_job_store_overflow = policy;
    }

    /// Enable/disable (and tune) the memory-pressure check that
    /// ensure_batch_capacity() (used by both append_jobs() and
    /// load_next_file()) runs before growing the job store for a new
    /// batch - see check_memory_pressure()'s own doc comment for the
    /// formula and rationale. fraction is the ceiling, as a fraction of
    /// actual available system memory, that projected job-store usage
    /// must stay under; 0.0 (the default) disables the check entirely.
    /// Values are trusted as given - no range validation here, matching
    /// set_job_store_overflow() above; the CLI (-m) and protobuf config
    /// (memory_pressure_fraction) layers validate before calling this.
    /// Off by default: unlike --job_store_capacity/--job_store_overflow
    /// (which bound a buffer size the caller chose), this queries actual
    /// system memory, which isn't something every caller wants tied to
    /// (e.g. containerized or memory-cgroup'd environments where
    /// /proc/meminfo may not reflect the effective limit).
    void set_memory_pressure_fraction(double fraction) {
        m_memory_pressure_fraction = fraction;
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
     * @brief Append a genuinely new job to m_data - the real streaming
     * insertion point (unlike insert_job()/submit_job(), which both
     * operate on a job already sitting in a preloaded m_data via
     * job_at()). Follows the same order established for this in
     * OUT_TRACE_STREAMING.md: check full(), try reclaim_front_jobs()
     * first, and only fall back to growing if nothing was reclaimable.
     * @param current_time Current simulated time, for the reclaim
     *        attempt's is_front_reclaimable() check - not the new job's
     *        own submit_time (see below), though the two are typically
     *        equal for a genuinely live arrival.
     * @param submit_time The new job's own submit_time attribute.
     * @param num_nodes Number of nodes the job requests.
     * @param queue Which queue the job was submitted to.
     * @param limit_time User-estimated time limit.
     * @return The new job's job_no - pass this to submit_job() next to
     *         actually enqueue it with the scheduler; append_job() only
     *         adds the record to the store, it doesn't submit it.
     */
    job_no_t append_job(sim_time_t current_time, const epoch_t& submit_time,
                        num_nodes_t num_nodes, job_queue_t queue,
                        timeout_t limit_time);

    /**
     * @brief Append several genuinely new jobs to m_data in one call -
     * the batch counterpart to append_job(), for the same never-seen-
     * before case (not a batch-preload; see load_data() for that).
     *
     * Originally designed with a future chunked-loading reader in mind
     * (a chunk as a std::vector<Job_Append_Request>, appended here once
     * per chunk) - that didn't end up how progressive/multi-file
     * loading (--infile_list) was actually built: Job_Append_Request's
     * narrower, network-facing 4 fields can't carry actual_run_time,
     * which load()'s output (what a real file read produces) can -
     * routing that through this struct would silently drop it. See
     * Trace::load_next_file() instead, which takes Job_Record directly;
     * see OUT_TRACE_STREAMING.md for the full reasoning. This function
     * still exists for genuine streaming, where a Job_Record doesn't
     * exist yet - only the caller's raw values do.
     *
     * Fully all-or-nothing: nothing in this batch is appended unless
     * all of it can be. Two things are checked before m_data is
     * touched at all:
     *   - every request's ordering - requests must already be sorted
     *     by submit_time, non-decreasing - m_data's own sort invariant
     *     (see job_at()'s doc comment), otherwise silently and
     *     implicitly assumed across separate append_job() calls; a
     *     batch call is where this is easy to check explicitly, so it
     *     is. (This function does not itself check submit_time >=
     *     current_time - same as append_job(), that precondition is
     *     enforced one level up, by
     *     Simulation::append_job()/append_jobs().)
     *   - capacity for the *whole* batch - unlike append_job()'s
     *     per-job check-full()-then-reclaim-then-grow, which can only
     *     discover exhaustion one job at a time, this resolves
     *     reclaim_front_jobs()/growing against the batch's whole size
     *     up front (see reclaim_front_jobs()'s min_free parameter).
     *     A --job_store_overflow=abort failure is therefore also
     *     atomic: it happens before the batch's first push_back(), not
     *     mid-loop, so m_data is left exactly as it was, never
     *     partially filled.
     *
     * Once both checks pass, every request is copied into m_data with
     * no further capacity checks interleaved between them - the same
     * batch-then-copy shape load_data() already uses for its own bulk
     * insertion, just with reclaim resolved against the batch's size
     * too (load_data() never needs that: nothing is ever reclaimable at
     * load time). Deliberately not a per-request loop re-checking
     * full()/reclaim/grow on every iteration - resizing (and
     * reallocating/copying) the circular buffer once for the whole
     * batch, rather than up to once per request, is the reason this
     * exists as a batch call at all rather than a loop over
     * append_job() the caller could already write themselves.
     *
     * @param current_time Current simulated time, for the batch-wide
     *        reclaim attempt's is_front_reclaimable() check.
     * @param requests The new jobs' own data, in submit_time order.
     * @return Each new job's job_no, in the same order as requests -
     *         pass each to submit_job() next, same as append_job().
     */
    std::vector<job_no_t> append_jobs(sim_time_t current_time,
                                       const std::vector<Job_Append_Request>& requests);

    /**
     * @brief Load one file's worth of jobs directly as Job_Record
     * objects (via the same load() free function load_data() uses),
     * for progressive/multi-file loading - a sequence of separate,
     * pre-sorted, pre-split trace files instead of one big one, so a
     * bounded --job_store_capacity can actually be honored (unlike
     * load_data(), which always grows to fit its one file whole).
     *
     * Deliberately does not go through append_jobs()/Job_Append_Request:
     * that struct only carries the 4 fields a genuine streaming caller
     * (no Job_Record in hand yet) can supply - routing an
     * already-parsed Job_Record through it would silently drop
     * actual_run_time back to 0.0, wrong for run_time_mode=actual/
     * distribution. This takes load()'s output directly instead,
     * losing nothing.
     *
     * Two checks, both all-or-nothing before m_data is touched at all
     * (same shape as append_jobs()): this file's own rows must already
     * be sorted by submit_time (load_data() tolerates unsorted input by
     * sorting it in memory - this can't, since it never holds more than
     * one file's rows at a time), and its first job's submit_time must
     * be >= the previous call's last job's submit_time (continuity
     * across the file sequence - first call is exempt, nothing to
     * compare against yet).
     *
     * @param current_time Current simulated time, for the batch-wide
     *        reclaim attempt's is_front_reclaimable() check.
     * @param fname Path to this file - same trace format as any other
     *        input file (--infile), not the network-facing
     *        Job_Append_Request shape.
     * @return Each loaded job's job_no, in submit_time order - pass
     *         each to submit_job() next, same as append_jobs().
     */
    std::vector<job_no_t> load_next_file(sim_time_t current_time, const std::string& fname);

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

    /// True if a pAll (exclusive-access) job is currently running -
    /// same condition load_data()'s own submission loop uses to decide
    /// whether a newly-submitted job's set_busy_nodes() call should
    /// record total_nodes (the whole machine, since a pAll job
    /// monopolizes it) rather than get_nodes_in_use() (other jobs'
    /// actual current occupancy). Exposed so submit_job() - which
    /// submits one job at a time, outside load_data()'s own loop - can
    /// apply the exact same rule. Always false if MARK_DAT_PERIOD is
    /// off (m_pAll_cnt doesn't exist in that build - no DAT tracking
    /// means never in a DAT period, by definition).
    bool in_dat_period() const {
      #if MARK_DAT_PERIOD
        return m_ctx.m_pAll_cnt > static_cast<num_jobs_t>(0u);
      #else
        return false;
      #endif
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
     *        enough that no entry needs reclaiming purely to make room.
     */
    void set_resource_history_capacity(size_t capacity) {
        m_resource_history_capacity = capacity;
    }

    /**
     * @brief Open filename early so resource-history samples reclaimed from
     * the circular buffer during the run get flushed to it incrementally,
     * instead of only being available at the very end via
     * write_resource_trace() below. Call once, before any processing
     * begins (e.g. before submit_job()/advance_to(), or run_job_trace()).
     * @param filename Output path; a no-op if empty - samples reclaimed
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
     * everything reclaimed mid-run was already written incrementally. If
     * start_resource_trace() was never called, this opens filename fresh
     * and writes the buffer's current contents in one shot - the original,
     * pre-streaming behavior - but note anything reclaimed before this call
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
     * it's reclaimed from m_data, rather than only at the very end - same
     * reasoning as start_resource_trace() above. Call once, before any
     * processing begins.
     * @param filename Output path; a no-op if empty - jobs reclaimed
     *        before write_simulated_trace() is later called are then
     *        simply discarded (lost), which defeats the whole point of
     *        this call, so this should always be paired with it once
     *        m_data can actually reclaim.
     * @param msec Format timestamps with millisecond precision instead of
     *        truncating to whole seconds (matches Sim_Params::m_msec_output)
     */
    void start_simulated_trace(const std::string& filename, bool msec = false);

    /**
     * @brief Write this Trace's job records to a CSV file (same format
     * write_simulated_trace() always used). If start_simulated_trace()
     * was already called with the same filename, this only writes
     * whatever jobs are still currently in m_data and closes the file -
     * everything reclaimed mid-run was already written at reclaim time.
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
    /// Used both for incremental reclaim-driven flushes during a run and
    /// for the final flush in write_resource_trace().
    void flush_resource_history();

    /// Record one (time, allocated) resource-occupancy sample - the single
    /// choke point process_events_until() and process_single_event() both
    /// go through, so reclaim-on-full only needs implementing once. If the
    /// buffer is full, this flushes+clears it first (every entry is always
    /// safe to reclaim here - see m_resource_history's own comment - so this
    /// always succeeds, unlike a wait queue or job store, which may need an
    /// abort/grow fallback when nothing is currently reclaimable).
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
    bool is_front_reclaimable(sim_time_t current_time) const;

    /// Reclaim from the front while is_front_reclaimable() holds, advancing
    /// m_num_reclaimed, until at least min_free slots are free (capacity()
    /// - size() >= min_free) or nothing more is reclaimable. Guarded by
    /// that same condition on entry (not proactive - lazy reclaim, same
    /// as resource-history's record_resource_sample()): a no-op if
    /// already enough room. min_free defaults to 1, matching every
    /// existing single-job caller (append_job(), insert_job()) exactly;
    /// append_jobs() passes the whole batch's size, since "enough room
    /// for one more" isn't the right question when appending several at
    /// once.
    ///
    /// handle_overflow (default true) controls whether *this function*
    /// applies m_job_store_overflow (grow-or-abort) if still full()
    /// afterward - same fallback the wait queue already uses for the
    /// analogous case. append_job() (min_free=1) relies on this default,
    /// since it has no capacity check of its own afterward. append_jobs()
    /// passes false: it has its own batch-aware capacity check right
    /// after calling this, comparing against the batch's actual size
    /// rather than just m_data.full() - letting this function's fallback
    /// fire too would risk an extra, unnecessarily-small intermediate
    /// grow (or, for abort, a misleading single-job-shaped message) in a
    /// case only the batch caller can size and word correctly.
    void reclaim_front_jobs(sim_time_t current_time, num_jobs_t min_free = 1,
                             bool handle_overflow = true);

    /// Shared by append_jobs() and load_next_file(): ensures
    /// m_data has room for batch_size more entries, reclaiming first
    /// and growing (or aborting, per m_job_store_overflow) only if
    /// still short after that - extracted out of append_jobs() so both
    /// callers get this exact logic once, not duplicated. Mutates
    /// m_data's capacity only, never inserts anything itself - callers
    /// still do their own push_back()ing once this returns.
    void ensure_batch_capacity(sim_time_t current_time, size_t batch_size);

    /// Called by ensure_batch_capacity() (after reclaiming, before
    /// deciding whether to grow) when m_memory_pressure_fraction > 0.0.
    /// Throws std::runtime_error if taking on batch_size more jobs
    /// would push projected peak job-store memory past that fraction of
    /// actual available system memory - a check against real memory
    /// pressure, independent of whatever --job_store_capacity happens
    /// to be set to (capacity bounds a buffer size the caller chose;
    /// this bounds against the machine's actual, current constraint
    /// instead).
    ///
    /// Formula, in units of sizeof(Job_Record): let old_cap =
    /// m_data.capacity() and new_cap = the same doubling computation
    /// ensure_batch_capacity()'s own grow step would use to reach
    /// old_cap + batch_size - not a fixed multiplier guess, the exact
    /// target capacity growing would actually allocate, however many
    /// doublings that takes for a large batch_size. If new_cap >
    /// old_cap (a grow would happen), projected usage is old_cap +
    /// new_cap + batch_size: boost::circular_buffer::set_capacity()
    /// allocates the new buffer and copies every existing entry into it
    /// before freeing the old one, so both are resident simultaneously
    /// mid-copy; the extra + batch_size accounts for load_next_file()'s
    /// own temporary std::vector<Job_Record> (the whole file's rows,
    /// read before this call, not yet moved into m_data) being resident
    /// at the exact moment this check runs - append_jobs() has no such
    /// vector (its Job_Append_Request is a different, smaller struct
    /// until pushed), so counting it uniformly for both callers only
    /// makes the check more conservative for append_jobs(), never less.
    /// If no grow would happen (new_cap == old_cap), projected usage is
    /// just old_cap + batch_size - the existing allocation, plus that
    /// same temporary vector.
    ///
    /// This is compared against m_memory_pressure_fraction * (available
    /// memory) / sizeof(Job_Record). The fraction leaves headroom for
    /// everything else the process holds (the rest of the trace
    /// machinery, the wait queue, resource history, and any headroom
    /// the OS/other processes need) - job records aren't the only
    /// consumer of memory here, just the one this check can reason
    /// about.
    ///
    /// If available memory can't be determined at all (see
    /// get_available_memory_bytes()'s own doc comment for when that
    /// happens), this is a no-op: there's nothing to enforce against,
    /// and refusing to proceed on missing information would be worse
    /// than not checking at all.
    void check_memory_pressure(size_t batch_size) const;

    /// Write one job's line to m_simulated_trace_ofs (if open and the
    /// job is_scheduled() - unscheduled/rejected jobs were never written
    /// by the original write_simulated_trace() either). The single
    /// choke point both reclaim_front_jobs() and write_simulated_trace()
    /// go through.
    void write_job_line(const Job_Record& job);
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_TRACE_HPP
