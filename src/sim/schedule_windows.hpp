/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULE_WINDOWS_HPP
#define DR_EVT_SIM_SCHEDULE_WINDOWS_HPP

#include <vector>
#include <map>
#include "common.hpp"
#include "trace/job_record.hpp"
#include "trace/trace.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_sim
 *  @{ */

/**
 * @brief Represents a scheduling window (available time/resource slot) in the schedule
 *
 * A window is a time period during which a certain number of nodes are available
 * for scheduling jobs. Used for finding backfill opportunities.
 */
struct Window {
    sim_time_t start;          ///< Start time of the window
    sim_time_t end;            ///< End time of the window
    num_nodes_t available_nodes; ///< Number of nodes available during this window

    /** @brief Construct a resource-availability interval.
     * @param[in] s Interval start time.
     * @param[in] e Interval end time.
     * @param[in] n Nodes available throughout the interval. */
    Window(sim_time_t s, sim_time_t e, num_nodes_t n)
      : start(s), end(e), available_nodes(n) {}
};

/**
 * @brief Tracks available resource windows in a schedule for backfilling.
 *
 * Based on ScheduleFlow's implementation. Maintains a list of time windows
 * where resources are available, enabling efficient backfill opportunity detection.
 *
 * A scheduling window represents a time period with available resources.
 * As jobs are added/removed, windows are updated to reflect resource availability.
 */
class ScheduleWindows {
  protected:
    /// Total nodes in the simulated system.
    num_nodes_t m_total_nodes;
    /// Current resource-availability intervals.
    std::vector<Window> m_windows;

    /// Map of reserved jobs: job_idx -> start_time
    std::map<job_no_t, sim_time_t> m_reserved_jobs;

    /// Reference to job data for accessing job properties
    const Trace::trace_data_t* m_job_data_ptr;

  public:
    /**
     * @brief Constructor
     * @param[in] total_nodes Total number of nodes in the system.
     * @param[in] job_data Trace data used to obtain reservation requirements.
     */
    ScheduleWindows(num_nodes_t total_nodes,
                    const Trace::trace_data_t& job_data);

    /**
     * @brief Find all scheduling windows where a job can fit
     * @param[in] start_time Earliest time the job can start.
     * @param[in] length Job's requested walltime.
     * @param[in] nodes Number of nodes required.
     * @return Windows that satisfy the time and node requirements.
     */
    std::vector<Window> get_windows(sim_time_t start_time,
                                     tdiff_t length,
                                     num_nodes_t nodes) const;

    /**
     * Find the earliest time a job can fit at the end of the schedule
     * (after all currently reserved jobs)
     * @param[in] start_time Earliest time the job can start.
     * @param[in] nodes Number of nodes required.
     * @return Earliest feasible end-of-schedule start time as sim_time_t.
     */
    sim_time_t fit_at_the_end(sim_time_t start_time, num_nodes_t nodes) const;

    /**
     * Add a job reservation to the schedule
     * Updates gaps to reflect the resource usage
     * @param[in] job_idx Trace job identifier.
     * @param[in] start_time Scheduled start time.
     * @param[in] request_walltime Requested or estimated job walltime.
     */
    void add_reservation(job_no_t job_idx, sim_time_t start_time,
                         tdiff_t request_walltime);

    /**
     * Remove a job reservation from the schedule
     * Updates gaps to reflect freed resources
     * @param[in] job_idx Trace job identifier to remove.
     */
    void remove_reservation(job_no_t job_idx);

    /**
     * @brief Clear all windows and reservations
     */
    void clear();

    /**
     * @brief Remove windows that end before the given time
     * @param[in] current_time Time threshold for trimming.
     */
    void trim(sim_time_t current_time);

    /**
     * Check if a job is in the reservation table
     * @param[in] job_idx Trace job identifier to check.
     * @return true if the job has a reservation.
     */
    bool has_reservation(job_no_t job_idx) const;

    /**
     * Get the reservation time for a job
     * @param[in] job_idx Trace job identifier.
     * @return Reservation start time as sim_time_t, or -1 when absent.
     */
    sim_time_t get_reservation_time(job_no_t job_idx) const;

  protected:
    /**
     * @brief Update windows when adding or removing a job
     * @param[in] job_idx Job being added or removed.
     * @param[in] start_time Start time of the job.
     * @param[in] end_time End time of the job.
     * @param[in] nodes Number of nodes.
     * @param[in] op Operation: -1 for add, +1 for remove.
     */
    void update_windows(job_no_t job_idx, sim_time_t start_time,
                        sim_time_t end_time, num_nodes_t nodes, int op);

    /**
     * @brief Consolidate overlapping windows
     *
     * Merges windows that can be combined to reduce the number of
     * windows that need to be checked during scheduling.
     */
    void consolidate_windows();

    /**
     * @brief Get windows at the end of the current schedule
     * @param[in] start_time Minimum start time.
     * @return Windows at the end of the schedule.
     */
    std::vector<Window> get_ending_windows(sim_time_t start_time) const;
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_SIM_SCHEDULE_WINDOWS_HPP
