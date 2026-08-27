/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
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
    CONSERVATIVE
};

/**
 * Priority/ordering policy for jobs in the wait queue
 * Determines the order in which jobs are considered for scheduling
 */
enum class PriorityPolicy {
    /** First-Come-First-Served: Order by submission time */
    FCFS,

    /** Shortest-Job-First: Order by estimated runtime (ascending) */
    SJF,

    /** Longest-Job-First: Order by estimated runtime (descending) */
    LJF
};

/**
 * Runtime estimate mode for scheduling decisions
 * Determines what runtime value is used for making scheduling decisions
 */
enum class RuntimeEstimateMode {
    /**
     * Use user-provided time limit (realistic mode).
     * Scheduler makes decisions based on requested walltime limit.
     * Jobs complete at their actual execution time.
     */
    USE_LIMIT,

    /**
     * Use actual runtime (oracle mode, for comparison studies).
     * Scheduler knows exact runtimes in advance.
     * Unrealistic but useful for comparison with optimal schedules.
     */
    USE_ACTUAL
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_SIM_SCHEDULER_POLICIES_HPP
