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
 * Scheduler's own job-length estimate, used for reservation/backfill
 * planning decisions. Distinct from RunTimeMode below, which controls
 * how the job's actual, observed execution length is determined.
 */
enum class DurationEstimateMode {
    /**
     * Use user-provided time limit (realistic mode).
     * Scheduler makes decisions based on requested walltime limit.
     * Jobs complete at their actual execution time.
     */
    USE_LIMIT,

    /**
     * Use the job's actual, observed run_time (oracle mode, for
     * comparison studies). Scheduler knows exact run_times in advance.
     * Unrealistic but useful for comparison with optimal schedules.
     * When this mode is active, RunTimeMode below is ignored entirely -
     * the trace's own real run_time is used directly.
     */
    USE_ACTUAL
};

/**
 * @brief How the job's actual, observed run_time is determined in
 * simulation mode
 *
 * In simulation mode, the scheduler computes start times but the job's
 * actual run_time must be determined. This enum controls the method.
 * Only consulted when DurationEstimateMode::USE_LIMIT is active - ignored
 * entirely under USE_ACTUAL, which uses the trace's own real run_time.
 */
enum class RunTimeMode {
    FROM_COLUMN,    ///< Read actual_run_time from trace column
    EXACT,          ///< Use time_limit as the run_time (perfect estimation)
    DISTRIBUTION    ///< Sample from statistical distribution
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
