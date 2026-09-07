/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include <algorithm>
#include <fstream>
#include "trace/job_io.hpp"
#include "trace/parse_utils.hpp" // to_string(job_queue_t) - used by write_job_line()
#include "trace/trace.hpp"
#include "utils/system_memory.hpp" // get_available_memory_bytes() - check_memory_pressure()

namespace dr_evt {

Trace::Trace(const std::string& fname)
  : m_fname(fname),
    m_num_reclaimed(0), m_job_store_capacity(0), m_job_store_capacity_resolved(false),
    m_job_store_overflow(CircularOverflowPolicy::GROW),
    m_completed_count(0), m_wait_time_sum(0.0), m_turnaround_time_sum(0.0), m_makespan(0.0),
    m_default_timezone("+00:00"),
    m_resource_history_capacity(0), m_resource_history_capacity_resolved(false),
    m_resource_trace_total_nodes(static_cast<num_nodes_t>(0u)), m_resource_trace_msec(false),
    m_simulated_trace_msec(false)
{
    if (!m_dcols.check_header(fname)) {
        std::string err = "Failed to initialize data columns";
        throw std::runtime_error {err.c_str()};
    }
}

Trace::Trace(const std::string& fname, const std::string& format)
  : m_fname(fname), m_dcols(format),
    m_num_reclaimed(0), m_job_store_capacity(0), m_job_store_capacity_resolved(false),
    m_job_store_overflow(CircularOverflowPolicy::GROW),
    m_completed_count(0), m_wait_time_sum(0.0), m_turnaround_time_sum(0.0), m_makespan(0.0),
    m_default_timezone("+00:00"),
    m_resource_history_capacity(0), m_resource_history_capacity_resolved(false),
    m_resource_trace_total_nodes(static_cast<num_nodes_t>(0u)), m_resource_trace_msec(false),
    m_simulated_trace_msec(false)
{
    if (!m_dcols.check_header(fname)) {
        std::string err = "Failed to initialize data columns";
        throw std::runtime_error {err.c_str()};
    }
}

Trace::Trace(const std::string& fname, const std::string& format,
             const std::string& timestamp_format, const std::string& timezone)
  : m_fname(fname), m_dcols(format, timestamp_format, timezone),
    m_num_reclaimed(0), m_job_store_capacity(0), m_job_store_capacity_resolved(false),
    m_job_store_overflow(CircularOverflowPolicy::GROW),
    m_completed_count(0), m_wait_time_sum(0.0), m_turnaround_time_sum(0.0), m_makespan(0.0),
    m_default_timezone("+00:00"),  // Default to UTC
    m_resource_history_capacity(0), m_resource_history_capacity_resolved(false),
    m_resource_trace_total_nodes(static_cast<num_nodes_t>(0u)), m_resource_trace_msec(false),
    m_simulated_trace_msec(false)
{
    if (!m_dcols.check_header(fname)) {
        std::string err = "Failed to initialize data columns";
        throw std::runtime_error {err.c_str()};
    }
}

void Trace::resolve_job_store_capacity(num_jobs_t hint)
{
    if (m_job_store_capacity_resolved) {
        return;
    }
    size_t cap = m_job_store_capacity;
    if (cap == 0) {
        // Auto-size: one slot per job, large enough that reclaiming is
        // never needed purely to make room for a fully-preloaded batch
        // run - matches the "0 = size of job trace" convention the wait
        // queue and resource-history both use. Floored at 4096 for the
        // same reason resource-history is: hint may still be small (or
        // 0) early in a genuinely streaming session.
        cap = std::max<size_t>(static_cast<size_t>(hint), 4096ul);
    }
    m_data.set_capacity(cap);
    m_job_store_capacity_resolved = true;
}

int Trace::load_data(num_jobs_t n_lines_to_read)
{
    // load() expects a std::vector - load into one, sort it (as before),
    // then transfer into m_data (the circular buffer). Nothing's
    // reclaimable yet at this point (no processing has happened), so if
    // an explicit --job_store_capacity is smaller than the job count,
    // m_job_store_overflow's abort/grow fallback applies here exactly
    // as it would mid-run.
    std::vector<Job_Record> loaded;
    int rc = load(m_fname, m_dcols, loaded, n_lines_to_read);

  #if LIMIT_VS_EXEC_TIME_ONLY
    print_limit_vs_exec_time(m_dcols.get_cols_to_read(), loaded);
    return rc;
  #endif

    // Order job records by the submit time
    // IMPORTANT: FCFS policy implementation in sim/scheduler.cpp depends on this
    // sorting to avoid re-sorting on every schedule() call
    std::stable_sort(loaded.begin(), loaded.end());

    resolve_job_store_capacity(static_cast<num_jobs_t>(loaded.size()));
    for (auto& job : loaded) {
        if (m_data.full()) {
            if (m_job_store_overflow == CircularOverflowPolicy::ABORT) {
                throw std::runtime_error(
                    "Trace: job store capacity (" + std::to_string(m_data.capacity()) +
                    ") exceeded while loading " + std::to_string(loaded.size()) +
                    " jobs; use --job_store_overflow grow or a larger --job_store_capacity");
            }
            m_data.set_capacity(std::max<size_t>(m_data.capacity() * 2, 1));
        }
        m_data.push_back(std::move(job));
    }

    return rc;
}

Trace::Context::Context() :
  #if MARK_DAT_PERIOD
    m_pAll_cnt(static_cast<num_jobs_t>(0u)), // On-going pAll job
    m_dat_start(epoch_t{}),
    m_dat_end(epoch_t{}),
    m_dat_span(0.0),
    m_prev_job_q(pUnknown),
  #endif
    m_n_nodes_in_use(static_cast<num_nodes_t>(0u))
{}

std::string Trace::Context::to_string() const
{
    std::string msg;
    msg = "";
    return msg;
}

void Trace::process_events_until(const epoch_t& t_sub)
{
    if (m_ctx.m_evtq.empty()) {
      #if MARK_DAT_PERIOD
        if (m_ctx.m_dat_span > 0.0) {
            m_reserved.emplace_back(m_ctx.m_dat_start, m_ctx.m_dat_end);
        }
      #endif
        return;
    }

    auto it = m_ctx.m_evtq.begin();
    while (it != m_ctx.m_evtq.end()) {
        auto cur = it ++;
        // Time of the earliest event in the queue
        auto& t = cur->get_time();

        if (t_sub < t) {
            // Process events upto the current submission time
            break;
        }
        const auto& job_of_evt = job_at(cur->get_job_idx());
      #if MARK_DAT_PERIOD
        const auto job_q = job_of_evt.get_queue();
      #endif

        if (cur->is_arrival()) {
          #if MARK_DAT_PERIOD
            if (job_q == pAll) {
                if ((m_ctx.m_prev_job_q != pAll) &&
                    (m_ctx.m_pAll_cnt == static_cast<num_jobs_t>(0u))) {
                    // No other DAT job is running and
                    // the last job seen was not a DAT job
                    if (m_ctx.m_dat_span > 0.0) {
                        m_reserved.emplace_back(m_ctx.m_dat_start, m_ctx.m_dat_end);
                    }

                    m_ctx.m_dat_start = cur->get_time();
                    m_ctx.m_dat_span = 0.0;
                }
                job_at(cur->get_job_idx()).set_busy_nodes(total_nodes, true);
                m_ctx.m_pAll_cnt ++;
            } else {
                m_ctx.m_n_nodes_in_use += job_of_evt.get_num_nodes();
            }
          #else
            m_ctx.m_n_nodes_in_use += job_of_evt.get_num_nodes();
          #endif
        } else {
          #if MARK_DAT_PERIOD
            if (job_q == pAll) {
              #if !EVENT_TIME_ORDER
                if (m_ctx.m_pAll_cnt == static_cast<num_jobs_t>(0u)) {
                    std::string err = "Inconsistent event times with job "
                                    + to_string(cur->get_job_idx());
                    throw std::runtime_error {err.c_str()};
                }
              #endif
                m_ctx.m_pAll_cnt --;
                if (m_ctx.m_pAll_cnt == static_cast<num_jobs_t>(0u)) {
                    m_ctx.m_dat_span = cur->get_time() - m_ctx.m_dat_start;
                    m_ctx.m_dat_end = cur->get_time();
                }
            } else {
                m_ctx.m_n_nodes_in_use -= job_of_evt.get_num_nodes();
            }
          #else
            m_ctx.m_n_nodes_in_use -= job_of_evt.get_num_nodes();
          #endif
        }
        const bool was_departure = !cur->is_arrival();
        const epoch_t event_time = t; // copy: erase() below invalidates t
        m_ctx.m_evtq.erase(cur); // Remove processed event from the queue
        record_resource_sample(event_time, m_ctx.m_n_nodes_in_use);
      #if MARK_DAT_PERIOD
        m_ctx.m_prev_job_q = job_q;
      #endif
        if (was_departure) {
            // Same reasoning as process_single_event(): only a departure
            // can newly unblock the front.
            sim_time_t current_time = static_cast<sim_time_t>(event_time.first) +
                                       event_time.second;
            reclaim_front_jobs(current_time);
        }
    }
}

void Trace::run_job_trace(const std::string& resource_trace_file, num_nodes_t total_nodes)
{
    if (m_data.empty()) {
        return;
    }

    if (m_dcols.get_trace_mode() != TraceMode::REPLAY) {
        // This function only replays begin_time/end_time that's already
        // present in the input - it never schedules anything itself. If
        // the header lacks those columns (simulation-format input:
        // submit_time/time_limit only), every job's begin_time/end_time
        // is still at Job_Record::unscheduled_sentinel() from load time
        // (see job_record.cpp) - reject here, before any of it reaches
        // the event queue or any output-generating code, rather than let
        // an unresolvable state flow downstream. Mirrors submit_job()'s
        // own upfront rejection in sim.cpp for the analogous case there
        // (a job that can never be scheduled).
        throw std::runtime_error(
            "run_job_trace() requires replay-format input (begin_time/"
            "end_time columns present) - this trace has neither, so "
            "every job's begin_time/end_time would stay unresolved the "
            "whole run. This looks like simulation-format input "
            "(submit_time/time_limit only): run it through simulator "
            "first, and feed tracer *its* output - simulator's "
            "write_simulated_trace() - instead.");
    }

    start_resource_trace(resource_trace_file, total_nodes);

    for (num_jobs_t i = static_cast<num_jobs_t>(0u); i < m_data.size(); ++i) {
        const auto& job = m_data[i]; // A new job submission
        auto t_sub = job.get_submit_time();
        process_events_until(t_sub);

      #if MARK_DAT_PERIOD
        m_data[i].set_busy_nodes(m_ctx.m_n_nodes_in_use, (m_ctx.m_pAll_cnt > static_cast<num_jobs_t>(0u)));
      #else
        m_data[i].set_busy_nodes(m_ctx.m_n_nodes_in_use);
      #endif
        // Add the events created by this submission
        m_ctx.m_evtq.emplace(i, job.get_begin_time(), arrival);
        m_ctx.m_evtq.emplace(i, job.get_end_time(), departure);
    }
    // Process all the remaiing events. Use any time later than any timestamp
    // in the trace for flushing.
    process_events_until(convert_time(max_tstamp));

    write_resource_trace(resource_trace_file, total_nodes);
}

job_no_t Trace::append_job(sim_time_t current_time, const epoch_t& submit_time,
                           num_nodes_t num_nodes, job_queue_t queue,
                           timeout_t limit_time)
{
    // In case this is called before load_data() ever runs (genuine
    // streaming, no batch preload at all) - resolve_job_store_capacity()
    // is idempotent (guarded by m_job_store_capacity_resolved), so this
    // is a no-op if load_data() already resolved it.
    resolve_job_store_capacity(static_cast<num_jobs_t>(m_data.size()));

    // Point-of-need order established in OUT_TRACE_STREAMING.md: try
    // reclaiming first, only grow if that wasn't enough.
    // reclaim_front_jobs() no-ops internally if there's already at
    // least 1 free slot (its min_free default), so no external full()
    // guard is needed here.
    reclaim_front_jobs(current_time);
    if (m_data.full()) {
        if (m_job_store_overflow == CircularOverflowPolicy::ABORT) {
            throw std::runtime_error(
                "Trace: job store capacity (" + std::to_string(m_data.capacity()) +
                ") exceeded and the front job isn't safe to reclaim yet; "
                "use --job_store_overflow grow or a larger --job_store_capacity");
        }
        m_data.set_capacity(std::max<size_t>(m_data.capacity() * 2, 1));
    }

    m_data.push_back(Job_Record(submit_time, num_nodes, queue, limit_time));
    return static_cast<job_no_t>(m_num_reclaimed + m_data.size() - 1);
}

void Trace::check_memory_pressure(size_t batch_size) const
{
    const size_t available = get_available_memory_bytes();
    if (available == 0) {
        // Unknown (non-Linux, or /proc/meminfo unreadable/lacks
        // MemAvailable) - nothing to enforce against; see this
        // function's own doc comment in trace.hpp.
        return;
    }

    const size_t unflushed = m_data.size();
    const size_t old_cap = m_data.capacity();
    const size_t needed = unflushed + batch_size;

    // Mirror ensure_batch_capacity()'s own doubling grow loop exactly -
    // the actual target capacity growing would allocate, not a fixed
    // multiplier guess, however many doublings a large batch_size needs.
    size_t new_cap = old_cap;
    while (new_cap < needed) {
        new_cap = std::max<size_t>(new_cap * 2, 1);
    }

    // If a grow would happen, boost::circular_buffer::set_capacity()
    // allocates the new buffer and copies every existing entry into it
    // before freeing the old one - both are resident simultaneously
    // mid-copy. Either way (grow or not), load_next_file()'s own
    // temporary std::vector<Job_Record> (this batch's rows, read from
    // file before this call, not yet moved into m_data) is resident at
    // the exact moment this check runs - append_jobs() has no such
    // vector, but counting it uniformly for both callers only makes
    // the check more conservative for append_jobs(), never less.
    const size_t buffer_peak = (new_cap > old_cap) ? (old_cap + new_cap) : old_cap;
    const size_t projected_jobs = buffer_peak + batch_size;

    const size_t limit_jobs = static_cast<size_t>(
        m_memory_pressure_fraction * static_cast<double>(available) /
        static_cast<double>(sizeof(Job_Record)));

    if (projected_jobs > limit_jobs) {
        throw std::runtime_error(
            "Trace: refusing to load a batch of " + std::to_string(batch_size) +
            " jobs under --check_memory_pressure - projected peak job-store usage (" +
            std::to_string(buffer_peak) + " buffer" +
            (new_cap > old_cap ? " (old " + std::to_string(old_cap) + " + new " +
                                  std::to_string(new_cap) + " during grow)" : "") +
            " + " + std::to_string(batch_size) + " in-flight = " +
            std::to_string(projected_jobs) + " job records) exceeds " +
            std::to_string(static_cast<int>(m_memory_pressure_fraction * 100)) + "% of " +
            "available memory (" + std::to_string(available / (1024 * 1024)) +
            " MB available, " + std::to_string(limit_jobs) + " job records' worth); "
            "split the input into smaller files, free up memory, or raise/disable "
            "--check_memory_pressure");
    }
}

void Trace::ensure_batch_capacity(sim_time_t current_time, size_t batch_size)
{
    // Same idempotent, load_data()-optional resolution as append_job().
    resolve_job_store_capacity(static_cast<num_jobs_t>(m_data.size()));

    // Batch-aware capacity resolution: does the buffer already hold
    // room for the *whole* batch? Point-of-need, generalized from
    // append_job()'s full() (no room for even one more) to "no room for
    // this many more" - reclaim_front_jobs() no-ops internally if
    // there's already enough (its min_free parameter, passed here as
    // the batch size instead of the default 1), so no external guard is
    // needed before calling it. This also means the batch is either
    // fully accommodated here before anything is added, or none of it
    // is - unlike a per-request check-full() loop, which can only
    // discover exhaustion mid-loop, after some requests are already
    // appended.
    reclaim_front_jobs(current_time, static_cast<num_jobs_t>(batch_size),
                        /* handle_overflow= */ false);

    // Independent of --job_store_capacity/--job_store_overflow below:
    // this checks against actual system memory, not a buffer size the
    // caller chose, and applies (when enabled) regardless of which
    // overflow policy is set - growing to fit a batch that would
    // exceed real available memory is exactly what this exists to
    // catch before it happens, not something --job_store_overflow=grow
    // should be able to bypass.
    if (m_memory_pressure_fraction > 0.0) {
        check_memory_pressure(batch_size);
    }

    size_t needed = m_data.size() + batch_size;
    if (needed > m_data.capacity()) {
        if (m_job_store_overflow == CircularOverflowPolicy::ABORT) {
            throw std::runtime_error(
                "Trace: job store capacity (" + std::to_string(m_data.capacity()) +
                ") can't fit this batch of " + std::to_string(batch_size) +
                " requests even after reclaiming; "
                "use --job_store_overflow grow or a larger --job_store_capacity");
        }
        // Grow once, directly to a size that fits the whole batch -
        // same doubling convention append_job()/load_data() use
        // per-request, just resolved in one step instead of
        // resizing (and copying/reallocating) the buffer once per
        // request in a batch of possibly many.
        size_t new_cap = m_data.capacity();
        while (new_cap < needed) {
            new_cap = std::max<size_t>(new_cap * 2, 1);
        }
        m_data.set_capacity(new_cap);
    }
}

std::vector<job_no_t> Trace::append_jobs(sim_time_t current_time,
                                          const std::vector<Job_Append_Request>& requests)
{
    // Input validation is all-or-nothing: check the whole batch's
    // ordering before touching m_data at all.
    for (size_t i = 1; i < requests.size(); ++i) {
        if (requests[i].submit_time < requests[i - 1].submit_time) {
            throw std::runtime_error(
                "Trace::append_jobs(): requests must be sorted by submit_time "
                "(non-decreasing) - request " + std::to_string(i) +
                " has an earlier submit_time than request " + std::to_string(i - 1));
        }
    }

    ensure_batch_capacity(current_time, requests.size());

    // Capacity is already sufficient for the whole batch at this point -
    // copy every request in, with no further full()/reclaim/grow checks
    // interleaved between them.
    std::vector<job_no_t> job_nos;
    job_nos.reserve(requests.size());
    for (const auto& req : requests) {
        m_data.push_back(Job_Record(req.submit_time, req.num_nodes, req.queue, req.limit_time));
        job_nos.push_back(static_cast<job_no_t>(m_num_reclaimed + m_data.size() - 1));
    }
    return job_nos;
}

std::vector<job_no_t> Trace::load_next_file(sim_time_t current_time, const std::string& fname)
{
    std::vector<Job_Record> loaded;
    int rc = load(fname, m_dcols, loaded);
    if (rc != EXIT_SUCCESS) {
        throw std::runtime_error(
            "Trace::load_next_file(): failed to load '" + fname + "'");
    }
    if (loaded.empty()) {
        return {};
    }

    // This file's own rows must already be sorted by submit_time -
    // load_data() tolerates unsorted input (it sorts everything it
    // read in memory before inserting), but this can't do that: it
    // never holds more than one file's rows at a time, so there's
    // nothing broader to sort against. Same check append_jobs() makes
    // on its own batch, applied here to load()'s output instead.
    for (size_t i = 1; i < loaded.size(); ++i) {
        if (loaded[i].get_submit_time() < loaded[i - 1].get_submit_time()) {
            throw std::runtime_error(
                "Trace::load_next_file(): '" + fname + "' is not sorted by "
                "submit_time (non-decreasing) - row " + std::to_string(i) +
                " has an earlier submit_time than row " + std::to_string(i - 1));
        }
    }

    // Continuity across the file sequence: this file's earliest
    // submit_time must be >= the previous file's latest - first call
    // is exempt (m_has_loaded_a_file false), nothing to compare against
    // yet.
    if (m_has_loaded_a_file &&
        loaded.front().get_submit_time() < m_last_loaded_submit_time) {
        throw std::runtime_error(
            "Trace::load_next_file(): '" + fname + "'s earliest submit_time "
            "is earlier than the previously loaded file's latest submit_time "
            "- files must be loaded in non-decreasing submit_time order");
    }

    ensure_batch_capacity(current_time, loaded.size());

    std::vector<job_no_t> job_nos;
    job_nos.reserve(loaded.size());
    for (auto& job : loaded) {
        m_data.push_back(std::move(job));
        job_nos.push_back(static_cast<job_no_t>(m_num_reclaimed + m_data.size() - 1));
    }

    m_last_loaded_submit_time = loaded.back().get_submit_time();
    m_has_loaded_a_file = true;

    return job_nos;
}

void Trace::insert_job(job_no_t job_idx, sim_time_t start_time)
{
    auto& job = job_at(job_idx);

    // Ensure job has actual_run_time set
    // In streaming mode, this would be determined here
    // For now, it should already be set by determine_job_run_time()
    if (job.get_actual_run_time() <= 0.0) {
        // Fallback: use time limit if duration not set
        job.set_actual_run_time(job.get_limit_time());
    }

    // Convert sim_time_t to epoch_t
    time_t start_sec = static_cast<time_t>(start_time);
    float start_frac = start_time - start_sec;
    epoch_t start_epoch = {start_sec, start_frac};

    // Calculate end time using actual duration
    tdiff_t duration = job.get_actual_run_time();
    sim_time_t end_time = start_time + duration;
    time_t end_sec = static_cast<time_t>(end_time);
    float end_frac = end_time - end_sec;
    epoch_t end_epoch = {end_sec, end_frac};

    // Insert events into queue (will be automatically sorted by event_q_t)
    m_ctx.m_evtq.emplace(job_idx, start_epoch, arrival);
    m_ctx.m_evtq.emplace(job_idx, end_epoch, departure);

    // Update job record with computed times (for output)
    job_at(job_idx).set_begin_time(start_epoch);
    job_at(job_idx).compute_end_time();
}

void Trace::run_until_exclusive(sim_time_t target_time)
{
    // Convert sim_time_t to epoch_t for comparison
    time_t target_sec = static_cast<time_t>(target_time);
    float target_frac = target_time - target_sec;
    epoch_t target_epoch = {target_sec, target_frac};

    // Process events until we reach target_time (exclusive)
    process_events_until(target_epoch);
}

void Trace::run_until_inclusive(sim_time_t target_time)
{
    // Process events at and before target_time
    while (!m_ctx.m_evtq.empty()) {
        const auto& event = *m_ctx.m_evtq.begin();
        sim_time_t event_time = static_cast<sim_time_t>(event.get_time().first) +
                               event.get_time().second;

        if (event_time > target_time) {
            break;  // Stop after processing all events <= target_time
        }

        // Process this event by running slightly past it
        process_events_until(event.get_time());
    }
}

bool Trace::process_single_event()
{
    if (m_ctx.m_evtq.empty()) {
        return false;
    }

    // Get and remove the earliest event
    auto it = m_ctx.m_evtq.begin();
    auto event = *it;  // Copy before erase
    m_ctx.m_evtq.erase(it);

    // Process this event using replay engine's accounting logic
    const auto& job = job_at(event.get_job_idx());

    if (event.is_arrival()) {
        // START event: allocate nodes (same logic as process_events_until)
        m_ctx.m_n_nodes_in_use += job.get_num_nodes();
    } else {
        // END event: free nodes (same logic as process_events_until)
        m_ctx.m_n_nodes_in_use -= job.get_num_nodes();
    }
    record_resource_sample(event.get_time(), m_ctx.m_n_nodes_in_use);

    if (!event.is_arrival()) {
        // A job just finished - its slot (or one ahead of it, by job_no)
        // may now be safe to reclaim. Only departures can possibly unblock
        // the front; checking on arrivals too would just be wasted work.
        sim_time_t current_time = static_cast<sim_time_t>(event.get_time().first) +
                                   event.get_time().second;
        reclaim_front_jobs(current_time);
    }

    return true;
}

void Trace::start_resource_trace(const std::string& filename,
                                  num_nodes_t total_nodes, bool msec)
{
    if (filename.empty()) {
        return;
    }
    if (m_resource_trace_ofs.is_open()) {
        // Already started - e.g. Simulation::run() started it for real
        // with a real filename, and run_job_trace()'s own internal call
        // (with empty defaults, in Simulation's replay-mode branch) must
        // not clobber that. Ignore this call rather than reopening.
        return;
    }
    m_resource_trace_ofs.open(filename);
    if (!m_resource_trace_ofs) {
        std::cerr << "Failed to open resource trace file: " << filename << std::endl;
        return;
    }
    m_resource_trace_total_nodes = total_nodes;
    m_resource_trace_msec = msec;

    std::string header = "time,free_nodes,allocated_nodes\n";
    m_resource_trace_ofs << header;

    // Baseline row: all nodes free at time 0, matching the convention
    // used elsewhere for this file format.
    std::string baseline = format_sim_time(0.0, msec) + "," +
                            std::to_string(total_nodes) + ",0\n";
    m_resource_trace_ofs << baseline;
}

void Trace::resolve_resource_history_capacity()
{
    if (m_resource_history_capacity_resolved) {
        return;
    }
    size_t cap = m_resource_history_capacity;
    if (cap == 0) {
        // Auto-size: every job contributes at most 2 events (start,
        // end), each producing one resource-history sample - large
        // enough that reclaiming is never needed purely to make room,
        // matching the "0 = size of job trace" convention the wait
        // queue and job store both use.
        // 4096 floor: m_data.size() may still be tiny (or 0, in a
        // streaming session where jobs arrive one at a time) at the
        // moment the very first sample is recorded, well before most
        // jobs have actually arrived - sizing purely off what's loaded
        // so far would cause needless reclaiming thrashing right from the
        // start of a long-running session.
        cap = std::max<size_t>(m_data.size() * 2, 4096ul);
    }
    m_ctx.m_resource_history.set_capacity(cap);
    m_resource_history_capacity_resolved = true;
}

void Trace::flush_resource_history()
{
    if (!m_resource_trace_ofs.is_open() || m_ctx.m_resource_history.empty()) {
        // No file open to receive these - discard. Bounded memory still
        // applies either way; nobody asked for this output.
        m_ctx.m_resource_history.clear();
        return;
    }

    const size_t blk_sz = 65536ul;
    std::string buf;
    buf.reserve(blk_sz + 4096);

    for (const auto& [time, allocated] : m_ctx.m_resource_history) {
        buf += format_sim_time(convert_epoch<sim_time_t>(time), m_resource_trace_msec) + "," +
               std::to_string(m_resource_trace_total_nodes - allocated) + "," +
               std::to_string(allocated) + "\n";
        if (buf.size() >= blk_sz) {
            m_resource_trace_ofs << buf;
            buf.clear();
        }
    }
    if (!buf.empty()) {
        m_resource_trace_ofs << buf;
    }
    m_ctx.m_resource_history.clear();
}

void Trace::record_resource_sample(const epoch_t& time, num_nodes_t allocated)
{
    resolve_resource_history_capacity();
    if (m_ctx.m_resource_history.full()) {
        // Every entry here is always safe to reclaim (see m_resource_history's
        // own comment in trace.hpp) - flush the whole buffer to make room
        // in one batch, rather than reclaiming one at a time.
        flush_resource_history();
    }
    m_ctx.m_resource_history.push_back(std::make_pair(time, allocated));
}

Job_Record& Trace::job_at(job_no_t job_no)
{
    if (job_no < m_num_reclaimed) {
        throw std::runtime_error(
            "Trace::job_at(): job_no=" + std::to_string(job_no) +
            " was already reclaimed (m_num_reclaimed=" + std::to_string(m_num_reclaimed) + ")");
    }
    const size_t idx = static_cast<size_t>(job_no - m_num_reclaimed);
    if (idx >= m_data.size()) {
        throw std::runtime_error(
            "Trace::job_at(): job_no=" + std::to_string(job_no) +
            " does not exist yet (only " + std::to_string(m_data.size()) +
            " jobs currently present)");
    }
    return m_data[idx];
}

const Job_Record& Trace::job_at(job_no_t job_no) const
{
    return const_cast<Trace*>(this)->job_at(job_no);
}

bool Trace::is_front_reclaimable(sim_time_t current_time) const
{
    if (m_data.empty()) {
        return false;
    }
    const auto& job = m_data.front();
    // Rejected (submit_time == unscheduled_sentinel()): will never
    // resolve, skip immediately rather than block the sweep forever.
    if (job.get_submit_time() == Job_Record::unscheduled_sentinel()) {
        return true;
    }
    // Genuinely finished.
    return convert_epoch<sim_time_t>(job.get_end_time()) <= current_time;
}

void Trace::reclaim_front_jobs(sim_time_t current_time, num_jobs_t min_free,
                                bool handle_overflow)
{
    // Lazy reclaim: only when actually needed, not proactively the
    // moment a job finishes. Same "only when short of min_free" gating
    // as resource-history's record_resource_sample(); without it, every
    // completion would reclaim immediately even with plenty of room
    // left, discarding data (write_simulated_trace(), stats) that
    // hasn't been read yet. min_free=1 (the default) reduces to the
    // original "only when full" gate exactly.
    //
    // Written as capacity() < size() + min_free rather than
    // capacity() - size() < min_free: both are equivalent given
    // circular_buffer's own size() <= capacity() invariant (so the
    // subtraction never actually underflows today), but comparing via
    // addition never relies on that invariant holding to stay safe -
    // unsigned subtraction that happens to not underflow is still worth
    // avoiding on general principle.
    if (m_data.capacity() >= m_data.size() + min_free) {
        return;
    }
    while (m_data.capacity() < m_data.size() + min_free && is_front_reclaimable(current_time)) {
        write_job_line(m_data.front());
        m_data.pop_front();
        ++m_num_reclaimed;
    }
    // No compaction: if still short of min_free and the front isn't
    // reclaimable, that's exactly the case m_job_store_overflow exists
    // for - same fallback the wait queue already uses.
    //
    // handle_overflow lets a batch caller (append_jobs(), min_free > 1)
    // opt out of this fallback entirely and always decide grow-vs-abort
    // itself instead - not because the condition here needs to differ
    // (m_data.full() vs. comparing against min_free was tried and
    // reverted: for a batch, some-but-not-enough reclaimed leaves
    // m_data not full(), so full() alone already stays out of the way
    // correctly in that case) but because full() alone still fires when
    // reclaiming frees nothing at all (m_data stays exactly at
    // capacity) - a case a batch can also hit, where only the caller
    // knows the batch's true size and can produce an accurate message
    // and a single correctly-sized grow, rather than this generic,
    // single-job-shaped one firing first.
    if (handle_overflow && m_data.full()) {
        if (m_job_store_overflow == CircularOverflowPolicy::ABORT) {
            throw std::runtime_error(
                "Trace: job store capacity (" + std::to_string(m_data.capacity()) +
                ") exceeded and the front job isn't safe to reclaim yet; "
                "use --job_store_overflow grow or a larger --job_store_capacity");
        }
        m_data.set_capacity(std::max<size_t>(m_data.capacity() * 2, 1));
    }
}

void Trace::write_job_line(const Job_Record& job)
{
    if (!job.is_scheduled()) {
        // Rejected-at-submit-time or otherwise never-run: excluded from
        // both the output file and every running stat below - same
        // filter write_simulated_trace() always used.
        return;
    }
    ++m_completed_count;
    const tdiff_t wait = job.get_wait_time();
    m_wait_time_sum += wait;
    m_turnaround_time_sum += wait + job.get_actual_run_time();
    m_makespan = std::max(m_makespan,
        convert_epoch<sim_time_t>(job.get_begin_time()) + job.get_actual_run_time());

    if (!m_simulated_trace_ofs.is_open()) {
        return; // nobody asked for the output file, but stats above still count
    }
    std::string line =
        format_sim_time(convert_epoch<sim_time_t>(job.get_submit_time()), m_simulated_trace_msec) + "," +
        format_sim_time(convert_epoch<sim_time_t>(job.get_begin_time()), m_simulated_trace_msec) + "," +
        format_sim_time(convert_epoch<sim_time_t>(job.get_end_time()), m_simulated_trace_msec) + "," +
        std::to_string(job.get_num_nodes()) + "," +
        "0," +
        dr_evt::to_string(job.get_queue()) + "," +
        format_sim_time(job.get_limit_time(), m_simulated_trace_msec) + "\n";
    m_simulated_trace_ofs << line;
}

void Trace::start_simulated_trace(const std::string& filename, bool msec)
{
    if (filename.empty()) {
        return;
    }
    if (m_simulated_trace_ofs.is_open()) {
        return; // already started - see start_resource_trace()'s own comment
    }
    m_simulated_trace_ofs.open(filename);
    if (!m_simulated_trace_ofs) {
        std::cerr << "Failed to open output file: " << filename << std::endl;
        return;
    }
    m_simulated_trace_msec = msec;
    std::string header = "job_submit_time,begin_time,end_time,num_nodes,exit_status,queue,time_limit\n";
    m_simulated_trace_ofs << header;
}

void Trace::write_simulated_trace(const std::string& filename, bool msec)
{
    if (filename.empty()) {
        return;
    }
    if (!m_simulated_trace_ofs.is_open()) {
        // start_simulated_trace() was never called - open fresh here and
        // write m_data's current contents in one shot (the original,
        // pre-streaming behavior). Anything already reclaimed before this
        // call is already gone.
        start_simulated_trace(filename, msec);
    }
    for (const auto& job : m_data) {
        write_job_line(job);
    }
    m_simulated_trace_ofs.close();
}

void Trace::write_resource_trace(const std::string& filename,
                                  num_nodes_t total_nodes, bool msec)
{
    if (filename.empty()) {
        return;
    }
    if (!m_resource_trace_ofs.is_open()) {
        // start_resource_trace() was never called (or was called with a
        // different/empty filename) - open fresh here. Note: anything
        // already reclaimed before this point (silently discarded, since no
        // file was open yet to receive it) is already gone; callers that
        // want everything preserved should call start_resource_trace()
        // before any processing begins instead.
        start_resource_trace(filename, total_nodes, msec);
    }
    flush_resource_history();
    m_resource_trace_ofs.close();
}

std::ostream& Trace::print(std::ostream& os) const
{
    dr_evt::print(os, m_data);
    return os;
}

std::ostream& Trace::print_span(std::ostream& os) const
{
    if (m_data.empty()) {
        return os;
    }

    const auto t_beg = m_data.at(0).get_submit_time();
    const auto t_last_submit = m_data.back().get_submit_time();
    auto t_last = t_last_submit; // the last end time of all the jobs

    auto rit = m_data.rbegin();
    const auto rit_end = m_data.rend();

    // considering jobs that may start as much as three days later than the
    // submission. (i.e., waiting for a weekend DAT to finish)
    const auto max_diff = max_batch_job_time + 3*24*60*60;

    for (; rit != rit_end; rit ++) {
        const auto t_sub = rit->get_submit_time() ;
        if (t_last_submit - t_sub > max_diff) {
            break;
        }
        const auto t_end = rit->get_end_time();
        t_last = std::max(t_end, t_last);
    }

    std::string str = "from " + to_string(t_beg) + ' '
                    + week_day_str[weekday(t_beg)]
                    + " to " + to_string(t_last) + ' '
                    + week_day_str[weekday(t_last)] + '\n';

    os << str;
    return os;
}

#if MARK_DAT_PERIOD
std::ostream& Trace::print_DAT(std::ostream& os)
{
    for (const auto& dat: m_reserved) {
        os << "DAT: started at " + dr_evt::to_string(dat.first)
            + " until " + dr_evt::to_string(dat.second) << std::endl;
    }
    return os;
}
#endif

} // end of namespace dr_evt
