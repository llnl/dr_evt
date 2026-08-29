/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <string>
#include <iostream>
#include <fstream>
#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/message.h>
#include "utils/file.hpp"
#include "proto/utils.hpp"
#include "proto/dr_evt_params.hpp"

namespace dr_evt {

static void set_sim_options(
    const dr_evt_proto::DR_EVT_Params::Simulation_Params& cfg,
    dr_evt::Sim_Params& sp, bool verbose = false)
{
    //using sim_params = dr_evt_proto::DR_EVT_Params::Simulation_Params;

    // Basic parameters
    sp.m_seed = cfg.seed();

    sp.m_max_jobs = cfg.max_jobs();
    sp.m_max_time = cfg.max_time();

    sp.m_is_jobs_set = (sp.m_max_jobs > 0u);
    sp.m_is_time_set = (sp.m_max_time > 0.0);

    sp.m_infile = cfg.infile();
    sp.set_outfile(cfg.outfile());

    // Verbose flag
    if (cfg.has_verbose()) {
        sp.m_verbose = cfg.verbose();
    }

    // Scheduling parameters
    if (cfg.has_total_nodes()) {
        sp.m_total_nodes = cfg.total_nodes();
    }

    if (cfg.has_backfill_policy()) {
        std::string policy = cfg.backfill_policy();
        if (policy == "easy") {
            sp.m_backfill_policy = BackfillPolicy::EASY;
        } else if (policy == "conservative") {
            sp.m_backfill_policy = BackfillPolicy::CONSERVATIVE;
        } else {
            std::cerr << "Warning: Unknown backfill_policy in protobuf: " << policy
                     << " (using default: easy)" << std::endl;
        }
    }

    if (cfg.has_priority_policy()) {
        std::string policy = cfg.priority_policy();
        if (policy == "fcfs") {
            sp.m_priority_policy = PriorityPolicy::FCFS;
        } else if (policy == "sjf") {
            sp.m_priority_policy = PriorityPolicy::SJF;
        } else if (policy == "ljf") {
            sp.m_priority_policy = PriorityPolicy::LJF;
        } else {
            std::cerr << "Warning: Unknown priority_policy in protobuf: " << policy
                     << " (using default: fcfs)" << std::endl;
        }
    }

    if (cfg.has_runtime_mode()) {
        std::string mode = cfg.runtime_mode();
        if (mode == "limit") {
            sp.m_runtime_mode = RuntimeEstimateMode::USE_LIMIT;
        } else if (mode == "actual") {
            sp.m_runtime_mode = RuntimeEstimateMode::USE_ACTUAL;
        } else {
            std::cerr << "Warning: Unknown runtime_mode in protobuf: " << mode
                     << " (using default: limit)" << std::endl;
        }
    }

    // Trace format parameters
    if (cfg.has_trace_format()) {
        sp.m_trace_format = cfg.trace_format();
    }

    if (cfg.has_timestamp_format()) {
        sp.m_timestamp_format = cfg.timestamp_format();
    }

    if (cfg.has_timezone()) {
        sp.m_timezone = cfg.timezone();
    }

    // Duration simulation parameters
    if (cfg.has_duration_mode()) {
        std::string mode = cfg.duration_mode();
        if (mode == "column") {
            sp.m_duration_mode = DurationMode::FROM_COLUMN;
        } else if (mode == "exact") {
            sp.m_duration_mode = DurationMode::EXACT;
        } else if (mode == "distribution") {
            sp.m_duration_mode = DurationMode::DISTRIBUTION;
        } else {
            std::cerr << "Warning: Unknown duration_mode in protobuf: " << mode
                     << " (using default: exact)" << std::endl;
        }
    }

    if (cfg.has_duration_distribution()) {
        std::string dist = cfg.duration_distribution();
        if (dist == "normal") {
            sp.m_duration_distribution = DistributionType::NORMAL;
        } else if (dist == "lognormal") {
            sp.m_duration_distribution = DistributionType::LOGNORMAL;
        } else if (dist == "uniform") {
            sp.m_duration_distribution = DistributionType::UNIFORM;
        } else {
            std::cerr << "Warning: Unknown duration_distribution in protobuf: " << dist
                     << " (using default: normal)" << std::endl;
        }
    }

    if (cfg.has_duration_scale()) {
        sp.m_duration_scale = cfg.duration_scale();
    }

    if (cfg.has_duration_stddev()) {
        sp.m_duration_stddev = cfg.duration_stddev();
    }

    // Handle defaults
    if (!sp.m_is_time_set) {
        sp.m_max_time = dr_evt::max_sim_time;
    }
    if (!sp.m_is_jobs_set && sp.m_is_time_set) {
        sp.m_max_jobs = std::numeric_limits<decltype(sp.m_max_jobs)>::max();
    }

    if (verbose) {
        sp.print();
    }
}

static void set_trace_options(
    const dr_evt_proto::DR_EVT_Params::Tracing_Params& cfg,
    dr_evt::Trace_Params& tp, bool verbose = false)
{
    //using trace_params = dr_evt_proto::DR_EVT_Params::Tracing_Params;

    tp.m_max_jobs = cfg.max_jobs();
    tp.m_max_time = cfg.max_time();

    tp.m_is_jobs_set = (tp.m_max_jobs > 0u);
    tp.m_is_time_set = (! tp.m_max_time.empty());

    tp.m_infile = cfg.infile();
    tp.set_outfile(cfg.outfile());
    tp.m_datfile = cfg.outfile_dat();
    tp.m_subfile = cfg.outfile_sub();
    tp.m_subsumfile = cfg.outfile_subsum();

    if (!tp.m_is_time_set) {
        tp.m_max_time = dr_evt::max_tstamp;
    }
    if (!tp.m_is_jobs_set && tp.m_is_time_set) {
        tp.m_max_jobs = std::numeric_limits<decltype(tp.m_max_jobs)>::max();
    }

    if (verbose) {
        tp.print();
    }
}

void read_proto_params(const std::string& filename,
                       dr_evt::Sim_Params& sp, bool verbose)
{
    dr_evt_proto::DR_EVT_Params::Simulation_Params dr_evt_sim_setup;
    dr_evt::read_prototext(filename, false, dr_evt_sim_setup);

    if (verbose) {
        std::string str;
        google::protobuf::TextFormat::PrintToString(dr_evt_sim_setup, &str);
        std::cout << "---- Prototext '" << filename << "' read ----"
                  << std::endl << str << std::endl;
    }

    set_sim_options(dr_evt_sim_setup, sp, verbose);
}

void read_proto_params(const std::string& filename,
                       dr_evt::Trace_Params& tp, bool verbose)
{
    dr_evt_proto::DR_EVT_Params::Tracing_Params dr_evt_trace_setup;
    dr_evt::read_prototext(filename, false, dr_evt_trace_setup);

    if (verbose) {
        std::string str;
        google::protobuf::TextFormat::PrintToString(dr_evt_trace_setup, &str);
        std::cout << "---- Prototext '" << filename << "' read ----"
                  << std::endl << str << std::endl;
    }

    set_trace_options(dr_evt_trace_setup, tp, verbose);
}

} // end of namespace dr_evt
