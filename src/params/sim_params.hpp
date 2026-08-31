/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
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
    RuntimeEstimateMode m_runtime_mode;
    num_nodes_t m_total_nodes;
    std::string m_trace_format;  // "simple" or "lassen"
    std::string m_timestamp_format;  // "epoch" or "iso"
    std::string m_timezone;  // e.g., "UTC", "America/Los_Angeles", "America/New_York"

    // Duration determination (simulation mode)
    DurationMode m_duration_mode;
    DistributionType m_duration_distribution;
    double m_duration_scale;    // Scale factor (e.g., 0.8 = jobs run 80% of estimate)
    double m_duration_stddev;   // Std deviation factor

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
