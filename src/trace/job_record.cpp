/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include <iostream>
#include <stdexcept>
#include "trace/job_record.hpp"
#include "trace/parse_utils.hpp"

namespace dr_evt {

unsigned int Job_Record::num_inputs = 0u;

Job_Record::Job_Record(const Job_Record& o)
  : m_t_begin(o.m_t_begin),
    m_t_end(o.m_t_end),
    m_t_submit(o.m_t_submit),
    m_t_limit(o.m_t_limit),
    m_actual_run_time(o.m_actual_run_time),
    m_num_nodes(o.m_num_nodes),
    m_q(o.m_q),
    m_is_simulated(o.m_is_simulated),
  #if SHOW_ORG_NO
    m_org_no(o.m_org_no),
  #endif
  #if MARK_DAT_PERIOD
    m_dat(o.m_dat),
  #endif
    m_busy_nodes(o.m_busy_nodes)
{}

Job_Record::Job_Record(Job_Record&& o) noexcept
  : m_t_begin(std::move(o.m_t_begin)),
    m_t_end(std::move(o.m_t_end)),
    m_t_submit(std::move(o.m_t_submit)),
    m_t_limit(std::move(o.m_t_limit)),
    m_actual_run_time(std::move(o.m_actual_run_time)),
    m_num_nodes(std::move(o.m_num_nodes)),
    m_q(std::move(o.m_q)),
    m_is_simulated(std::move(o.m_is_simulated)),
  #if SHOW_ORG_NO
    m_org_no(std::move(o.m_org_no)),
  #endif
  #if MARK_DAT_PERIOD
    m_dat(std::move(o.m_dat)),
  #endif
    m_busy_nodes(std::move(o.m_busy_nodes))
{}

Job_Record& Job_Record::operator=(const Job_Record& o)
{
    if (this != &o) {
        m_t_begin = o.m_t_begin;
        m_t_end = o.m_t_end;
        m_t_submit = o.m_t_submit;
        m_t_limit = o.m_t_limit;
        m_actual_run_time = o.m_actual_run_time;
        m_num_nodes = o.m_num_nodes;
        m_q = o.m_q;
        m_is_simulated = o.m_is_simulated;
      #if SHOW_ORG_NO
        m_org_no = o.m_org_no;
      #endif
      #if MARK_DAT_PERIOD
        m_dat = o.m_dat;
      #endif
        m_busy_nodes = o.m_busy_nodes;
    }
    return *this;
}

Job_Record& Job_Record::operator=(Job_Record&& o) noexcept
{
    if (this != &o) {
        m_t_begin = std::move(o.m_t_begin);
        m_t_end = std::move(o.m_t_end);
        m_t_submit = std::move(o.m_t_submit);
        m_t_limit = std::move(o.m_t_limit);
        m_actual_run_time = std::move(o.m_actual_run_time);
        m_num_nodes = std::move(o.m_num_nodes);
        m_q = std::move(o.m_q);
        m_is_simulated = std::move(o.m_is_simulated);
      #if SHOW_ORG_NO
        m_org_no = std::move(o.m_org_no);
      #endif
      #if MARK_DAT_PERIOD
        m_dat = std::move(o.m_dat);
      #endif
        m_busy_nodes = std::move(o.m_busy_nodes);
    }
    return *this;
}

#if SHOW_ORG_NO
Job_Record::Job_Record(job_no_t no, const std::vector<std::string>& str_vec)
  : m_org_no(no),
#else
Job_Record::Job_Record(const std::vector<std::string>& str_vec)
  :
#endif
  #if MARK_DAT_PERIOD
    m_dat(false),
  #endif
    m_busy_nodes(static_cast<num_nodes_t>(0u))
{
    using dr_evt::operator-;
    using dr_evt::operator<;

    if ((num_inputs == 0u) || (str_vec.size() != num_inputs)) {
        throw std::invalid_argument {"Record format does not match! "
          + std::to_string(num_inputs) + " != "
          + std::to_string(str_vec.size())};
        return;
    }

    auto it = str_vec.cbegin();
    set_by(m_num_nodes, *it++);

  #if BATCH_JOB_NODE_LIMIT
    if ((m_num_nodes > BATCH_JOB_NODE_LIMIT) && _Is_Batch(m_q))
    {
        throw std::domain_error
            {"Batch job submitted at " + to_string(m_t_submit) +
             " exceeds the limit of num nodes: " +
             std::to_string(m_num_nodes) + " > " +
             std::to_string(BATCH_JOB_NODE_LIMIT)};
    }
  #endif

    // Check mode based on number of fields:
    // Replay mode (6): num_nodes, begin_time, end_time, submit_time, queue, time_limit
    // Simulation mode (4 or 5): num_nodes, submit_time, queue, time_limit[, actual_run_time]
    bool is_replay_mode = (num_inputs == 6);
    bool has_actual_run_time = (num_inputs == 5 || num_inputs == 7);

    if (is_replay_mode) {
        // Replay mode: has begin_time and end_time
        set_by(m_t_begin, *it++);
        set_by(m_t_end, *it++);
        set_by(m_t_submit, *it++);

      #if EVENT_TIME_ORDER
        if ((m_t_begin > m_t_end) || (m_t_submit > m_t_begin)) {
            // Round-trip through to_string()/convert_time() as a sanity
            // recheck. An out-of-range epoch (e.g. the unscheduled
            // sentinel - never a genuine job time here) can't round-trip;
            // treat that the same as "times are incorrect" below rather
            // than letting convert_time()'s parse failure escape uncaught.
            try {
                m_t_submit = convert_time(dr_evt::to_string(m_t_submit));
                m_t_begin  = convert_time(dr_evt::to_string(m_t_begin));
                m_t_end  = convert_time(dr_evt::to_string(m_t_end));
            } catch (const std::invalid_argument&) {
                throw std::domain_error
                    {"Job event times are incorrect! (unable to normalize)"};
            }

            if ((m_t_begin > m_t_end) || (m_t_submit > m_t_begin)) {
                throw std::domain_error
                    {"Job event times are incorrect! " +
                     dr_evt::to_string(m_t_submit) + " < " +
                     dr_evt::to_string(m_t_begin) + " < " +
                     dr_evt::to_string(m_t_end)};
            }
        }
      #endif

        set_by(m_q, *it++);
        set_by(m_t_limit, *it++);

        // Compute actual_run_time from recorded times
        m_actual_run_time = static_cast<tdiff_t>(m_t_end - m_t_begin);
        m_is_simulated = false;
    } else {
        // Simulation mode: no begin_time or end_time in input.
        // Initialize to the unscheduled sentinel (not zero - see
        // Job_Record::unscheduled_sentinel()'s comment for why) - will be
        // set by scheduler via set_begin_time()/compute_end_time().
        m_t_begin = unscheduled_sentinel();
        m_t_end = unscheduled_sentinel();
        // Was previously left unset here (only the replay-mode branch
        // above set it), leaving m_is_simulated as indeterminate/
        // undefined-behavior-uninitialized until set_begin_time() was
        // eventually called - confirmed empirically to read back as
        // true immediately after construction, before any scheduling
        // had happened. false here matches "not yet computed by
        // scheduler," consistent with the replay-mode branch's intent.
        m_is_simulated = false;

        set_by(m_t_submit, *it++);
        set_by(m_q, *it++);
        set_by(m_t_limit, *it++);

        // If actual_run_time provided, read it; otherwise will be set by determine_job_run_time()
        if (has_actual_run_time) {
            set_by(m_actual_run_time, *it++);
        } else {
            m_actual_run_time = 0.0;
        }
        // m_is_simulated stays false here (set above) until the scheduler
        // actually dispatches this job via set_begin_time(). This line
        // previously set it unconditionally to true right after parsing -
        // before any scheduling had happened - making m_is_simulated
        // always true for every simulation-mode job regardless of actual
        // status, which is exactly the bug that made it unreliable as a
        // "was scheduled" check in the first place.
    }
}

std::string Job_Record::get_header_str()
{
    return std::string("num_nodes") + '\t' + "begin_time" + '\t' + "end_time"
      + '\t' + "submit_time" + '\t' + "time_limit" + '\t' + "wait_time"
      + '\t' + "exec_time" + '\t' + "busy_nodes" + '\t' + "queue"
    #if MARK_DAT_PERIOD
      + "\tDAT"
    #endif
    #if SHOW_ORG_NO
      + "\torg_no"
    #endif
      ;
}

std::string Job_Record::to_string() const
{
    using dr_evt::to_string;
    using std::to_string;

  #if MARK_DAT_PERIOD
    static constexpr const char* const dat_str[2] = {"\tNo", "\tYes"};
  #endif

    std::string str =
        to_string(m_num_nodes) + '\t' +
        to_string(m_t_begin) + '\t' +
        to_string(m_t_end) + '\t' +
        to_string(m_t_submit) + '\t' +
        to_string(m_t_limit) + '\t' +
        to_string(get_wait_time()) + '\t' +
        to_string(get_actual_run_time()) + '\t' +
        to_string(m_busy_nodes) + '\t' +
        to_string(m_q)
      #if MARK_DAT_PERIOD
        + dat_str[m_dat]
      #endif
      #if SHOW_ORG_NO
        + '\t' + to_string(m_org_no)
      #endif
        ;
    return str;
}

std::ostream& operator<<(std::ostream& os, const Job_Record& rec)
{
    os << rec.to_string();

    return os;
}

} // end of namespace dr_evt
