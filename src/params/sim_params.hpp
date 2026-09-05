/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_PARAMS_SIM_PARAMS_HPP
#define DR_EVT_PARAMS_SIM_PARAMS_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#include <string>
#include "dr_evt_types.hpp"
#include "sim/scheduler_policies.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_params
 *  @{ */

/**
 * Queue implementation selection for FCFS scheduler
 */
enum class QueueImplementation {
    CIRCULAR,   // boost::circular_buffer based (FCFS default)
    DEQUE,      // std::deque based (FCFS)
    MULTIMAP,   // std::multimap based (FCFS alt, for differential testing)
    BLOCK       // BlockWaitQueue based (optimized for large queues, FCFS only)
};

/**
 * What CircularBufferFCFSScheduler does when an insert would exceed its
 * wait queue's current capacity.
 */
enum class CircularOverflowPolicy {
    ABORT,  // Throw std::runtime_error, ending the simulation
    GROW    // Reallocate to a larger capacity, copying existing entries over
};

class Sim_Params {
 public:
    Sim_Params();
    void getopt(int& argc, char** &argv);
    void print_usage(const std::string exec, int code);
    void print() const;
    void set_outfile(const std::string& ofname);
    std::string get_outfile() const;
    void set_resource_trace(const std::string& rfname);
    std::string get_resource_trace() const;

    unsigned m_seed;
    dr_evt::num_jobs_t m_max_jobs;
    dr_evt::sim_time_t m_max_time;

    std::string m_infile;

    bool m_is_jobs_set;
    bool m_is_time_set;

    // Scheduling parameters
    BackfillPolicy m_backfill_policy;
    PriorityPolicy m_priority_policy;
    // Scheduler uses time_limit as the best estimator for planning (realistic mode).
    // m_run_time_mode below controls how jobs actually execute.
    QueueImplementation m_queue_impl;
    size_t m_block_size;  // Block size for block queue (must be power of 2)
    size_t m_circular_capacity;  // Initial capacity for circular queue (0 = size of job trace)
    CircularOverflowPolicy m_circular_overflow;  // What to do if circular queue capacity is exceeded
    num_nodes_t m_total_nodes;
    std::string m_trace_format;  // "simple" or "lassen"
    std::string m_timestamp_format;  // "epoch" or "iso"
    std::string m_timezone;  // e.g., "UTC", "America/Los_Angeles", "America/New_York"

    // Run time determination (simulation mode) - how the job's actual,
    // observed execution length is determined: actual (read from trace),
    // distribution (sampled), or limit (use time_limit in place of run_time).
    // Default is ACTUAL (most realistic).
    RunTimeMode m_run_time_mode;
    DistributionType m_run_time_distribution;
    double m_run_time_scale;    // Scale factor (e.g., 0.8 = jobs run 80% of estimate)
    double m_run_time_stddev;   // Std deviation factor

    // Output control
    bool m_verbose;  // Enable verbose/debug output (default: false for production)
    bool m_msec_output;  // Output timestamps with millisecond precision (default: false, integer seconds)

 private:
    std::string m_outfile;
    std::string m_resource_trace;  // Optional resource usage trace output
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_PARAMS_SIM_PARAMS_HPP
