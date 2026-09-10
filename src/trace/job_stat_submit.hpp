/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file job_stat_submit.hpp
 * @brief Submission-arrival statistics and weekly summaries.
 */

#ifndef DR_EVT_TRACE_JOB_STAT_SUBMIT_HPP
#define DR_EVT_TRACE_JOB_STAT_SUBMIT_HPP
#include "sim/job_submit_common.hpp"
#include "trace/trace.hpp"
#include <array>
#include <map>
#include <vector>

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

/**
 *  Statistics on job submission rate during each hour of a day of a week.
 *  This assumes that the job sequence is presented in the submission time
 *  order.
 */
class Job_Stat_Submit {
public:
  /// How much of a timeslot was available for batch jobs. [0.0-1.0]
  using avail_t = float;

  /// Store how many jobs have been submitted during a particular timeslot.
  struct Hour_Slot {
    /// Number of jobs submitted during a particular hour-long period
    num_jobs_t m_num_jobs;

    /** The portion of time during which the resources were available to
     * batch jobs (i.e., no DAT reservation or maintenance) */
    avail_t m_avail;

    /** @brief Construct an empty, fully available hourly slot. */
    Hour_Slot()
        : m_num_jobs(static_cast<num_jobs_t>(0u)),
          m_avail(static_cast<avail_t>(1.0)) {}

    /** @brief Return submissions recorded in this hour.
     * @return Job count as num_jobs_t. */
    num_jobs_t num_jobs() const { return m_num_jobs; }
    /** @brief Return the batch-resource availability fraction.
     * @return Availability in the range [0, 1] as avail_t. */
    avail_t availability() const { return m_avail; }
    /** @brief Record one job submission. */
    void inc() { m_num_jobs++; }
    /** @brief Mark this hour fully available. */
    void reset_availability() { m_avail = static_cast<avail_t>(1.0); }
    /** @brief Mark this hour fully unavailable. */
    void clear_availability() { m_avail = static_cast<avail_t>(0.0); }
    /** @brief Set this hour's availability fraction.
     * @param[in] av Availability in the range [0, 1]. */
    void set_availability(const avail_t av) { m_avail = av; }
  };

  /// Slot sumamry statistics: average number of job submission and average
  /// availability
  struct Summary {
    /// Average number of job submission at a time slot
    using avg_submit_t = float;

    /// Total number of jobs submitted during this slot
    num_jobs_t m_tot_submit;

    /** Number of cases where this slot was completely blocked.
     *  (i.e., availability = 0.0) */
    num_jobs_t m_num_blocked;

    /**
     *  Min number of submission during the slot without including
     *  blocked cases.  */
    num_jobs_t m_min_submit;
    /** Max number of submission during the slot without scaling
     *  for availability */
    num_jobs_t m_max_submit;

    /** Normalized average number of jobs submitted during this slot.
     *  Normalized for full availability. */
    avg_submit_t m_avg_submit;

    /// Unnormalized average number of jobs submitted during this slot
    avg_submit_t m_avg_submit_unscaled;

    /// Average availability of the slot
    avail_t m_avg_avail;

    /** Standard deviation of number of jobs submitted during this slot
     *  with normalized average */
    avg_submit_t m_std_submit;

    /// Standard deviation of the availability of the slot
    avail_t m_std_avail;

    /** @brief Construct an empty hour-slot summary. */
    Summary();
    /** @brief Return CSV-style summary column headings.
     * @return Column-heading row as std::string. */
    static std::string header_str();
    /** @brief Render the summary as one output row.
     * @return Formatted summary row as std::string. */
    std::string to_string() const;
  };

  /** Each element represent a specific 1-hour-long timeslot, [0:00, 1:00),
   *  ..., [23:00-24:00], of a day from Monday to Friday of a particular week.
   */
  using tslot_week_t = std::array<Hour_Slot, 7u * 24u>;

  /// History of job submission
  using tsubmit_t = std::vector<tslot_week_t>;

  /// Type for hour-slot summary over multiple weeks
  using summary_week_t = std::array<Summary, 7u * 24u>;

protected:
  /// Job submission history to allow statistical analysis
  tsubmit_t m_tsubmit;

  /** @brief Reserve storage for weekly statistics.
   * @param[in] num_weeks Expected number of weeks. */
  void reserve(size_t num_weeks);

  /** @brief Mark hour slots outside the trace period unavailable.
   * @param[in] trace Trace defining the covered time interval. */
  void mark_slots_out_of_trace_period(const Trace &trace);

  /// Struct to keep track of temporary variables during processing
  class Context {
  public:
    /** @brief Trace reservation periods consulted for availability. */
    using reserved_t = dr_evt::Trace::reserved_t;
    constexpr static const unsigned week_in_sec =
        7 * 24 * 60 * 60; ///< Seconds per week.
    constexpr static const unsigned day_in_sec =
        24 * 60 * 60; ///< Seconds per day.
    constexpr static const unsigned hour_in_sec =
        60 * 60;                                     ///< Seconds per hour.
    constexpr static const unsigned min_in_sec = 60; ///< Seconds per minute.

  protected:
    /// The end of DAT list iterator
    const reserved_t::const_iterator m_it_end;

    /// The iterator of current/outstanding DAT
    reserved_t::const_iterator m_it;

    /// Start of the week window
    std::time_t m_week_end;

    /// End of the week window
    std::time_t m_week_start;

    /// Resource availability ratio of the current time slot for batch jobs
    avail_t m_ratio;

    /// Whether a DAT continues across a time bin boundary
    bool m_dat_continue;

    /// End of current slot
    std::time_t m_slot_end;

    /// Start of current slot
    std::time_t m_slot_start;

    /// Temporary hourly time slot boundaries
    std::array<std::time_t, 7 * 24 + 1> m_hours;

    /**
     *  Calculate resource availability ratio of the current time slot for
     *  batch jobs based on DAT history.
     */
    /** @brief Calculate batch-resource availability for the current slot. */
    void calc_availability();

  public:
    /** @brief Initialize processing state from a trace's reservations.
     * @param[in] trace Trace supplying reservation periods. */
    Context(const Trace &trace);
    /** @brief Advance context to the week containing a timestamp.
     * @param[in] t Timestamp to enter.
     * @param[in,out] history Weekly history extended as needed. */
    void advance_week(std::time_t t, tsubmit_t &history);
    /** @brief Apply calculated availability to a week's hour slots.
     * @param[in,out] week Hour slots receiving availability values. */
    void set_availability(tslot_week_t &week);
    /** @brief Return the end timestamp of the current week window.
     * @return Integral epoch timestamp. */
    std::time_t week_end() const { return m_week_end; }
  };

public:
  /** @brief Build hourly submission history from a time-ordered trace.
   * @param[in] trace Source trace. */
  void process(const Trace &trace);
  /** @brief Return the number of weeks represented in the history.
   * @return Week count as num_jobs_t. */
  num_jobs_t get_num_weeks() const;
  /** @brief Aggregate hour-slot history across all weeks.
   * @return Per-hour weekly summary array. */
  summary_week_t get_summary() const;
  /** @brief Write detailed submission history to a stream.
   * @param[in,out] os Destination stream.
   * @return Reference to @p os. */
  std::ostream &print(std::ostream &os) const;
  /** @brief Write aggregate submission statistics to a stream.
   * @param[in,out] os Destination stream.
   * @return Reference to @p os. */
  std::ostream &print_summary(std::ostream &os) const;
  /** @brief Write a supplied aggregate summary to a stream.
   * @param[in,out] os Destination stream.
   * @param[in] summary Summary values to write.
   * @return Reference to @p os. */
  std::ostream &print_summary(std::ostream &os,
                              const summary_week_t &summary) const;
  /** @brief Export weekly submission data to a stream.
   * @param[in,out] os Destination stream.
   * @param[in] scale Whether to normalize counts by availability.
   * @return Reference to @p os. */
  std::ostream &export_submit_data(std::ostream &os,
                                   const bool scale = false) const;
  /** @brief Return weekly submission data, optionally availability-scaled.
   * @param[in] scale Whether to normalize counts by availability.
   * @return Weekly submission values. */
  submit_week_t export_submit_data(const bool scale = false) const;
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_JOB_STAT_SUBMIT_HPP
