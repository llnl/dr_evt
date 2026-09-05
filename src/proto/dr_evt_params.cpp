/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
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

    // Resource trace file (optional)
    if (!cfg.resource_trace().empty()) {
        sp.set_resource_trace(cfg.resource_trace());
    }

    // Verbose flag (always set in proto3, use the value directly)
    sp.m_verbose = cfg.verbose();

    // Scheduling parameters (0 means use default from Sim_Params constructor)
    if (cfg.total_nodes() > 0) {
        sp.m_total_nodes = cfg.total_nodes();
    }

    // Backfill policy (default: EASY)
    if (!cfg.backfill_policy().empty()) {
        std::string policy = cfg.backfill_policy();
        if (policy == "easy") {
            sp.m_backfill_policy = BackfillPolicy::EASY;
        } else if (policy == "conservative") {
            sp.m_backfill_policy = BackfillPolicy::CONSERVATIVE;
        } else if (policy == "none") {
            sp.m_backfill_policy = BackfillPolicy::NONE;
        } else {
            throw std::runtime_error("Unknown backfill_policy in protobuf: " + policy);
        }
    } else {
        sp.m_backfill_policy = BackfillPolicy::EASY;
    }

    // Priority policy (default: FCFS)
    if (!cfg.priority_policy().empty()) {
        std::string policy = cfg.priority_policy();
        if (policy == "fcfs") {
            sp.m_priority_policy = PriorityPolicy::FCFS;
        } else if (policy == "fcfs_conservative") {
            sp.m_priority_policy = PriorityPolicy::FCFS_CONSERVATIVE;
        } else if (policy == "sjf") {
            sp.m_priority_policy = PriorityPolicy::SJF;
        } else if (policy == "ljf") {
            sp.m_priority_policy = PriorityPolicy::LJF;
        } else {
            throw std::runtime_error("Unknown priority_policy in protobuf: " + policy);
        }
    } else {
        sp.m_priority_policy = PriorityPolicy::FCFS;
    }

    // Trace format (options: "simple" or "lassen", default: "simple")
    if (!cfg.trace_format().empty()) {
        std::string format = cfg.trace_format();
        if (format == "simple" || format == "lassen") {
            sp.m_trace_format = format;
        } else {
            throw std::runtime_error("Unknown trace_format in protobuf: " + format);
        }
    } else {
        sp.m_trace_format = "simple";
    }

    // Timestamp format (options: "epoch" or "iso", default: "iso")
    if (!cfg.timestamp_format().empty()) {
        std::string format = cfg.timestamp_format();
        if (format == "epoch" || format == "iso") {
            sp.m_timestamp_format = format;
        } else {
            throw std::runtime_error("Unknown timestamp_format in protobuf: " + format);
        }
    } else {
        sp.m_timestamp_format = "iso";
    }

    // Timezone (examples: "UTC", "America/Los_Angeles", "America/New_York", default: "America/Los_Angeles")
    // Accepts any valid IANA timezone string
    if (!cfg.timezone().empty()) {
        sp.m_timezone = cfg.timezone();
    } else {
        sp.m_timezone = "America/Los_Angeles";
    }

    // Run time mode - how the job's actual, observed execution length
    // is determined. Scheduler uses time_limit as the best estimator for planning.
    // (default: ACTUAL - most realistic)
    std::string mode = cfg.run_time_mode();
    if (mode.empty()) {
        sp.m_run_time_mode = RunTimeMode::ACTUAL;  // default
    } else if (mode == "actual") {
        sp.m_run_time_mode = RunTimeMode::ACTUAL;
    } else if (mode == "distribution") {
        sp.m_run_time_mode = RunTimeMode::DISTRIBUTION;
    } else if (mode == "limit") {
        sp.m_run_time_mode = RunTimeMode::LIMIT;
    } else {
        throw std::runtime_error("Unknown run_time_mode in protobuf: " + mode + " (valid: actual, distribution, limit)");
    }

    // Run time distribution (default: NORMAL)
    if (!cfg.run_time_distribution().empty()) {
        std::string dist = cfg.run_time_distribution();
        if (dist == "normal") {
            sp.m_run_time_distribution = DistributionType::NORMAL;
        } else if (dist == "lognormal") {
            sp.m_run_time_distribution = DistributionType::LOGNORMAL;
        } else if (dist == "uniform") {
            sp.m_run_time_distribution = DistributionType::UNIFORM;
        } else {
            throw std::runtime_error("Unknown run_time_distribution in protobuf: " + dist);
        }
    } else {
        sp.m_run_time_distribution = DistributionType::NORMAL;
    }

    // Run time scale (default: 1.0 = jobs run 100% of time_limit)
    // 0.0 doesn't make sense (zero duration), treat as "use default"
    if (cfg.run_time_scale() > 0.0) {
        sp.m_run_time_scale = cfg.run_time_scale();
    } else {
        sp.m_run_time_scale = 1.0;
    }

    // Run time stddev (default: 0.0 = no variation)
    // Negative values don't make sense, treat as "use default"
    if (cfg.run_time_stddev() >= 0.0) {
        sp.m_run_time_stddev = cfg.run_time_stddev();
    } else {
        std::cerr << "Warning: Negative run_time_stddev in protobuf: " << cfg.run_time_stddev()
                 << " (using default: 0.0)" << std::endl;
        sp.m_run_time_stddev = 0.0;
    }

    // Queue implementation (default: CIRCULAR)
    if (!cfg.queue_impl().empty()) {
        std::string impl = cfg.queue_impl();
        if (impl == "circular") {
            sp.m_queue_impl = QueueImplementation::CIRCULAR;
        } else if (impl == "deque") {
            sp.m_queue_impl = QueueImplementation::DEQUE;
        } else if (impl == "multimap") {
            sp.m_queue_impl = QueueImplementation::MULTIMAP;
        } else if (impl == "block") {
            sp.m_queue_impl = QueueImplementation::BLOCK;
        } else {
            throw std::runtime_error("Unknown queue_impl in protobuf: " + impl);
        }
    } else {
        sp.m_queue_impl = QueueImplementation::CIRCULAR;
    }

    // Block size for block queue implementation (0 means use default from Sim_Params constructor)
    if (cfg.block_size() > 0) {
        sp.m_block_size = cfg.block_size();
    }

    // Initial capacity for circular queue implementation (0 means use
    // default from Sim_Params constructor - size of the job trace)
    if (cfg.circular_capacity() > 0) {
        sp.m_circular_capacity = cfg.circular_capacity();
    }

    // Circular queue overflow policy (default: GROW)
    if (!cfg.circular_overflow().empty()) {
        std::string policy = cfg.circular_overflow();
        if (policy == "abort") {
            sp.m_circular_overflow = CircularOverflowPolicy::ABORT;
        } else if (policy == "grow") {
            sp.m_circular_overflow = CircularOverflowPolicy::GROW;
        } else {
            throw std::runtime_error("Unknown circular_overflow in protobuf: " + policy);
        }
    } else {
        sp.m_circular_overflow = CircularOverflowPolicy::GROW;
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
        if (!google::protobuf::TextFormat::PrintToString(dr_evt_sim_setup, &str)) {
            std::cerr << "Warning: Failed to serialize config to textproto format" << std::endl;
        }
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
        if (!google::protobuf::TextFormat::PrintToString(dr_evt_trace_setup, &str)) {
            std::cerr << "Warning: Failed to serialize config to textproto format" << std::endl;
        }
        std::cout << "---- Prototext '" << filename << "' read ----"
                  << std::endl << str << std::endl;
    }

    set_trace_options(dr_evt_trace_setup, tp, verbose);
}

} // end of namespace dr_evt
