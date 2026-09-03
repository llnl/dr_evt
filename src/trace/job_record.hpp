/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_TRACE_JOB_RECORD_HPP
#define DR_EVT_TRACE_JOB_RECORD_HPP

#include <vector>
#include <string>
#include <limits>
#include "common.hpp"
#include "trace/epoch.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

class Job_Record {
  public:
    /**
     * Sentinel begin_time/end_time value for a simulation-mode job that
     * hasn't been scheduled yet. Deliberately not (0, 0.0f): 0 is a
     * legitimate, real timestamp for any job that starts at simulation
     * time 0, so using it as a "never scheduled" marker made that
     * genuinely common case indistinguishable from "not yet run" -
     * silently excluding such jobs from completion counts and
     * wait/turnaround statistics elsewhere in the codebase. The maximum
     * representable time_t value can never collide with a real,
     * computed simulation timestamp.
     */
    static epoch_t unscheduled_sentinel() {
        return {std::numeric_limits<time_t>::max(), 0.0f};
    }

  protected:
    epoch_t m_t_begin; ///< The starting time of the job execution
    epoch_t m_t_end; ///< The end time of job execution
    epoch_t m_t_submit; ///< The time when the job was submitted
    timeout_t m_t_limit; ///< The time limit of the job (user estimate)
    tdiff_t m_actual_run_time; ///< Actual execution time (ground truth)
    num_nodes_t m_num_nodes; ///< The amount of resources this job uses
    job_queue_t m_q; ///< The queue to which the job was submitted
    bool m_is_simulated; ///< True if times were computed by scheduler (simulation mode)
  #if SHOW_ORG_NO
    job_no_t m_org_no;
  #endif
  #if MARK_DAT_PERIOD
    /** Is the job duration overlaps with DAT period?
     *  i.e., the period of other job from pAll queue
     */
    bool m_dat;
  #endif

    /// Number of nodes being used at the submit time
    num_nodes_t m_busy_nodes;

    /// Number of inputs which the constructor is expecting
    static unsigned int num_inputs;


  public:
  #if SHOW_ORG_NO
    Job_Record(job_no_t n, const std::vector<std::string>& svec) noexcept(false);
  #else
    Job_Record(const std::vector<std::string>& str_vec) noexcept(false);
  #endif

    Job_Record(const Job_Record& other);
    Job_Record(Job_Record&& other) noexcept;
    Job_Record& operator=(const Job_Record& rhs);
    Job_Record& operator=(Job_Record&& rhs) noexcept;

    static void set_num_inputs(num_nodes_t n) { num_inputs = n; }
    epoch_t get_begin_time() const { return m_t_begin; }
    epoch_t get_end_time() const { return m_t_end; }
    epoch_t get_submit_time() const { return m_t_submit; }
    tdiff_t get_wait_time() const { return (m_t_begin - m_t_submit); }
    timeout_t get_limit_time() const { return m_t_limit; }
    tdiff_t get_actual_run_time() const { return m_actual_run_time; }
    num_nodes_t get_num_nodes() const { return m_num_nodes; }
    num_nodes_t get_busy_nodes() const { return m_busy_nodes; }
    bool is_simulated() const { return m_is_simulated; }
    /**
     * True if this job has valid timing data to use for statistics -
     * i.e. it actually ran (simulation mode) or came with timing built in
     * (replay mode). Use this (not begin_time/end_time == 0) to check
     * "did this job run" - see unscheduled_sentinel()'s comment for why
     * not begin_time/end_time directly.
     *
     * Primarily relies on m_is_simulated, not the sentinel: m_is_simulated
     * is an explicit bool that must be set correctly by whoever
     * constructs/schedules the job, regardless of what any numeric field
     * defaults to - notably, a protobuf-constructed job's unspecified
     * begin_time/end_time would default to 0 (proto3's bool/numeric
     * default), not max value, so a sentinel-only check would silently
     * break the moment any code path populates a Job_Record from such a
     * source. The sentinel comparison is kept as a fallback specifically
     * for replay-mode jobs: m_is_simulated is correctly false for them
     * (their times come from the input file, not "computed by scheduler"),
     * but they always have valid timing (never touch the sentinel at all,
     * since they're populated directly from the file's replay-format
     * columns and never go through set_begin_time()) - so m_t_end will
     * differ from the sentinel for any replay-mode job, correctly making
     * this true for them via the fallback term.
     */
    bool is_scheduled() const { return m_is_simulated || (m_t_end != unscheduled_sentinel()); }

    // Setters for simulation mode
    void set_begin_time(const epoch_t& t) { m_t_begin = t; m_is_simulated = true; }
    void set_actual_run_time(tdiff_t d) { m_actual_run_time = d; }
    void compute_end_time() {
        m_t_end = m_t_begin;
        time_t seconds = static_cast<time_t>(m_actual_run_time);
        float fraction = m_actual_run_time - seconds;
        m_t_end.first += seconds;
        m_t_end.second += fraction;
        if (m_t_end.second >= 1.0) {
            m_t_end.first += 1;
            m_t_end.second -= 1.0;
        }
    }
  #if MARK_DAT_PERIOD
    bool does_overlap_dat() const { return m_dat; }
    void set_busy_nodes(num_nodes_t n, bool dat = false)
    { m_busy_nodes = (!dat)*n + dat*total_nodes; m_dat = dat; }
    void set_dat() { m_dat = true; }
  #else
    void set_busy_nodes(num_nodes_t n) { m_busy_nodes = n; }
  #endif
    job_queue_t get_queue() const { return m_q; }

  #if SHOW_ORG_NO
    void set_org_line_no(job_no_t i) { m_org_no = i; }
  #endif

    std::string to_string() const;
    static std::string get_header_str();

    friend bool operator<(const Job_Record& r1, const Job_Record& r2);
};

inline bool operator<(const Job_Record& r1, const Job_Record& r2)
{
    return(r1.m_t_submit < r2.m_t_submit);
}

std::ostream& operator<<(std::ostream& os, const Job_Record& rec);

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_JOB_RECORD_HPP
