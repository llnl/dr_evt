/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file dr_event.hpp
 * @brief Discrete resource-event representation and ordering.
 */

#ifndef DR_EVT_TRACE_DR_EVENT_HPP
#define DR_EVT_TRACE_DR_EVENT_HPP

#include <set>
#include "common.hpp"
#include "trace/epoch.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

/**
 * @brief A scheduled start or end event for one trace job.
 * @details Events are ordered by timestamp, event type, and job identifier so
 * an event_q_t can process resource-state transitions deterministically.
 */
class DR_Event {
  protected:
    /// Trace job identifier affected by this event.
    job_no_t m_jidx;
    /// Timestamp at which the resource transition occurs.
    epoch_t m_t;
    /// Event kind: arrival/start when true; departure/end when false.
    bool m_type;

  public:
    /** @brief Construct a job resource event.
     * @param[in] idx Trace job identifier.
     * @param[in] t Event timestamp.
     * @param[in] type true for arrival/start; false for departure/end. */
    DR_Event(job_no_t idx, const epoch_t& t, bool type);

    DR_Event(const DR_Event& other);
    DR_Event(DR_Event&& other) noexcept;
    DR_Event& operator=(const DR_Event& rhs);
    DR_Event& operator=(DR_Event&& rhs) noexcept;

    /** @brief Return the affected Trace job identifier. @return job_no_t value. */
    job_no_t get_job_idx() const { return m_jidx; }
    /** @brief Return the event timestamp. @return Const reference to epoch_t. */
    const epoch_t& get_time() const { return m_t; }
    /** @brief Return the raw event type flag. @return true for arrival/start. */
    bool get_type() const { return m_type; }
    /** @brief Report whether this is a start event. @return true for arrival/start. */
    bool is_arrival() const { return (m_type == arrival); }
    /** @brief Report whether this is an end event. @return true for departure/end. */
    bool is_departure() const { return (m_type == departure); }

    friend bool operator<(const DR_Event& e1, const DR_Event& e2);
    friend bool operator==(const DR_Event& e1, const DR_Event& e2);
};

inline bool operator<(const DR_Event& e1, const DR_Event& e2)
{
    return LESS_OR(e1.m_t, e2.m_t,\
                   LESS_OR(e1.m_type, e2.m_type,\
                           (e1.m_jidx < e2.m_jidx)));
}

inline bool operator==(const DR_Event& e1, const DR_Event& e2)
{
    return (e1.m_jidx == e2.m_jidx)
        && (e1.m_type == e2.m_type)
        && (e1.m_t == e2.m_t);
}

/** @brief Write an event's textual representation.
 * @param[in,out] os Destination stream.
 * @param[in] evt Event to format.
 * @return The destination stream after writing. */
std::ostream& operator<<(std::ostream& os, const DR_Event& evt);

/// Ordered set used as the simulation event queue.
using event_q_t = std::set<DR_Event>;

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_DR_EVENT_HPP
