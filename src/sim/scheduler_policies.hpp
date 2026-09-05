/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULER_POLICIES_HPP
#define DR_EVT_SIM_SCHEDULER_POLICIES_HPP

namespace dr_evt {
/** \addtogroup dr_evt_sim
 *  @{ */

/**
 * Backfilling policy types
 * Determines how backfilling is performed in the scheduler
 */
enum class BackfillPolicy {
    /**
     * EASY backfilling: Only the first job in the queue gets a reservation.
     * Other jobs can backfill if they fit in available resources and won't
     * delay the first job's reservation.
     */
    EASY,

    /**
     * Conservative backfilling: All queued jobs get reservations.
     * Backfilling jobs cannot delay ANY reservation.
     */
    CONSERVATIVE,

    NONE
};

/**
 * Priority/ordering policy for jobs in the wait queue
 * Determines the order in which jobs are considered for scheduling
 */
enum class PriorityPolicy {
    /** First-Come-First-Served: Order by submission time */
    FCFS,

    /** Alternative FCFS implementation (for differential testing) */
    FCFS_ALT,

    /** FCFS with conservative backfilling or no backfilling */
    FCFS_CONSERVATIVE,

    /** Shortest-Job-First: Order by estimated run_time (ascending) */
    SJF,

    /** Longest-Job-First: Order by estimated run_time (descending) */
    LJF
};

/**
 * @brief How the job's actual, observed run_time is determined in
 * simulation mode
 *
 * In simulation mode, the scheduler uses time_limit as the best estimator for planning
 * (realistic - what real schedulers know). This enum controls how the job's
 * actual observed execution length is determined.
 */
enum class RunTimeMode {
    ACTUAL,         ///< Read actual_run_time from trace column (most realistic)
    DISTRIBUTION,   ///< Sample from statistical distribution (realistic with variation)
    LIMIT           ///< Use time_limit as the run_time (unrealistic, for debugging/testing only)
};

/**
 * @brief Statistical distribution for sampling job run_times
 *
 * Used when RunTimeMode::DISTRIBUTION is selected.
 */
enum class DistributionType {
    NORMAL,      ///< Normal distribution N(limit*scale, limit*stddev)
    LOGNORMAL,   ///< Lognormal distribution with median=limit*scale
    UNIFORM      ///< Uniform distribution [limit*scale_min, limit*scale_max]
};

/**
 * @brief Trace processing mode
 *
 * Automatically detected based on columns present in trace file.
 */
enum class TraceMode {
    REPLAY,      ///< Has begin_time column - replay historical execution
    SIMULATION   ///< No begin_time column - scheduler computes start times
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_SIM_SCHEDULER_POLICIES_HPP
