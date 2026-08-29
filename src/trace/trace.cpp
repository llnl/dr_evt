/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <algorithm>
#include <fstream>
#include "trace/job_io.hpp"
#include "trace/trace.hpp"

namespace dr_evt {

Trace::Trace(const std::string& fname)
  : m_fname(fname), m_default_timezone("+00:00")
{
    if (!m_dcols.check_header(fname)) {
        std::string err = "Failed to initialize data columns";
        throw std::runtime_error {err.c_str()};
    }
}

Trace::Trace(const std::string& fname, const std::string& format)
  : m_fname(fname), m_dcols(format), m_default_timezone("+00:00")
{
    if (!m_dcols.check_header(fname)) {
        std::string err = "Failed to initialize data columns";
        throw std::runtime_error {err.c_str()};
    }
}

Trace::Trace(const std::string& fname, const std::string& format,
             const std::string& timestamp_format, const std::string& timezone)
  : m_fname(fname), m_dcols(format, timestamp_format, timezone),
    m_default_timezone("+00:00")  // Default to UTC
{
    if (!m_dcols.check_header(fname)) {
        std::string err = "Failed to initialize data columns";
        throw std::runtime_error {err.c_str()};
    }
}

int Trace::load_data(num_jobs_t n_lines_to_read)
{
    int rc = load(m_fname, m_dcols, m_data, n_lines_to_read);

  #if LIMIT_VS_EXEC_TIME_ONLY
    print_limit_vs_exec_time(m_dcols.get_cols_to_read(), ctx.data());
    return rc;
  #endif

    // Order job records by the submit time
    // IMPORTANT: FCFS policy implementation in sim/scheduler.cpp depends on this
    // sorting to avoid re-sorting on every schedule() call
    std::stable_sort(m_data.begin(), m_data.end());

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

void Trace::process_events_until(Trace::Context& ctx, const epoch_t& t_sub)
{
    if (ctx.m_evtq.empty()) {
      #if MARK_DAT_PERIOD
        if (ctx.m_dat_span > 0.0) {
            m_reserved.emplace_back(ctx.m_dat_start, ctx.m_dat_end);
        }
      #endif
        return;
    }

    auto it = ctx.m_evtq.begin();
    while (it != ctx.m_evtq.end()) {
        auto cur = it ++;
        // Time of the earliest event in the queue
        auto& t = cur->get_time();

        if (t_sub < t) {
            // Process events upto the current submission time
            break;
        }
        const auto& job_of_evt = m_data[cur->get_job_idx()];
      #if MARK_DAT_PERIOD
        const auto job_q = job_of_evt.get_queue();
      #endif

        if (cur->is_arrival()) {
          #if MARK_DAT_PERIOD
            if (job_q == pAll) {
                if ((ctx.m_prev_job_q != pAll) &&
                    (ctx.m_pAll_cnt == static_cast<num_jobs_t>(0u))) {
                    // No other DAT job is running and
                    // the last job seen was not a DAT job
                    if (ctx.m_dat_span > 0.0) {
                        m_reserved.emplace_back(ctx.m_dat_start, ctx.m_dat_end);
                    }

                    ctx.m_dat_start = cur->get_time();
                    ctx.m_dat_span = 0.0;
                }
                m_data[cur->get_job_idx()].set_busy_nodes(total_nodes, true);
                ctx.m_pAll_cnt ++;
            } else {
                ctx.m_n_nodes_in_use += job_of_evt.get_num_nodes();
            }
          #else
            ctx.m_n_nodes_in_use += job_of_evt.get_num_nodes();
          #endif
        } else {
          #if MARK_DAT_PERIOD
            if (job_q == pAll) {
              #if !EVENT_TIME_ORDER
                if (ctx.m_pAll_cnt == static_cast<num_jobs_t>(0u)) {
                    std::string err = "Inconsistent event times with job "
                                    + to_string(cur->get_job_idx());
                    throw std::runtime_error {err.c_str()};
                }
              #endif
                ctx.m_pAll_cnt --;
                if (ctx.m_pAll_cnt == static_cast<num_jobs_t>(0u)) {
                    ctx.m_dat_span = cur->get_time() - ctx.m_dat_start;
                    ctx.m_dat_end = cur->get_time();
                }
            } else {
                ctx.m_n_nodes_in_use -= job_of_evt.get_num_nodes();
            }
          #else
            ctx.m_n_nodes_in_use -= job_of_evt.get_num_nodes();
          #endif
        }
        ctx.m_evtq.erase(cur); // Remove processed event from the queue
      #if MARK_DAT_PERIOD
        ctx.m_prev_job_q = job_q;
      #endif
    }
}

void Trace::run_job_trace()
{
    if (m_data.empty()) {
        return;
    }

    Context ctx;

    for (num_jobs_t i = static_cast<num_jobs_t>(0u); i < m_data.size(); ++i) {
        const auto& job = m_data[i]; // A new job submission
        auto t_sub = job.get_submit_time();
        process_events_until(ctx, t_sub);

      #if MARK_DAT_PERIOD
        m_data[i].set_busy_nodes(ctx.m_n_nodes_in_use, (ctx.m_pAll_cnt > static_cast<num_jobs_t>(0u)));
      #else
        m_data[i].set_busy_nodes(ctx.m_n_nodes_in_use);
      #endif
        // Add the events created by this submission
        ctx.m_evtq.emplace(i, job.get_begin_time(), arrival);
        ctx.m_evtq.emplace(i, job.get_end_time(), departure);
    }
    // Process all the remaiing events. Use any time later than any timestamp
    // in the trace for flushing.
    process_events_until(ctx, convert_time(max_tstamp));
}

void Trace::insert_job(job_no_t job_idx, sim_time_t start_time, Context& ctx)
{
    auto& job = m_data[job_idx];

    // Ensure job has actual_duration set
    // In streaming mode, this would be determined here
    // For now, it should already be set by determine_job_durations()
    if (job.get_actual_duration() <= 0.0) {
        // Fallback: use time limit if duration not set
        job.set_actual_duration(job.get_limit_time());
    }

    // Convert sim_time_t to epoch_t
    time_t start_sec = static_cast<time_t>(start_time);
    float start_frac = start_time - start_sec;
    epoch_t start_epoch = {start_sec, start_frac};

    // Calculate end time using actual duration
    tdiff_t duration = job.get_exec_time();
    sim_time_t end_time = start_time + duration;
    time_t end_sec = static_cast<time_t>(end_time);
    float end_frac = end_time - end_sec;
    epoch_t end_epoch = {end_sec, end_frac};

    // Insert events into queue (will be automatically sorted by event_q_t)
    ctx.m_evtq.emplace(job_idx, start_epoch, arrival);
    ctx.m_evtq.emplace(job_idx, end_epoch, departure);

    // Update job record with computed times (for output)
    m_data[job_idx].set_begin_time(start_epoch);
    m_data[job_idx].compute_end_time();
}

void Trace::run_until_exclusive(Context& ctx, sim_time_t target_time)
{
    // Convert sim_time_t to epoch_t for comparison
    time_t target_sec = static_cast<time_t>(target_time);
    float target_frac = target_time - target_sec;
    epoch_t target_epoch = {target_sec, target_frac};

    // Process events until we reach target_time (exclusive)
    process_events_until(ctx, target_epoch);
}

void Trace::run_until_inclusive(Context& ctx, sim_time_t target_time)
{
    // Process events at and before target_time
    while (!ctx.m_evtq.empty()) {
        const auto& event = *ctx.m_evtq.begin();
        sim_time_t event_time = static_cast<sim_time_t>(event.get_time().first) +
                               event.get_time().second;

        if (event_time > target_time) {
            break;  // Stop after processing all events <= target_time
        }

        // Process this event by running slightly past it
        process_events_until(ctx, event.get_time());
    }
}

bool Trace::process_single_event(Context& ctx)
{
    if (ctx.m_evtq.empty()) {
        return false;
    }

    // Get and remove the earliest event
    auto it = ctx.m_evtq.begin();
    auto event = *it;  // Copy before erase
    ctx.m_evtq.erase(it);

    // Process this event using replay engine's accounting logic
    const auto& job = m_data[event.get_job_idx()];

    if (event.is_arrival()) {
        // START event: allocate nodes (same logic as process_events_until)
        ctx.m_n_nodes_in_use += job.get_num_nodes();
    } else {
        // END event: free nodes (same logic as process_events_until)
        ctx.m_n_nodes_in_use -= job.get_num_nodes();
    }

    return true;
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

    auto rit = m_data.crbegin();
    const auto rit_end = m_data.crend();

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
