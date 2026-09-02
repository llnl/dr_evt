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
            std::cerr << "Warning: Unknown backfill_policy in protobuf: " << policy
                     << " (using default: easy)" << std::endl;
            sp.m_backfill_policy = BackfillPolicy::EASY;
        }
    } else {
        sp.m_backfill_policy = BackfillPolicy::EASY;
    }

    // Priority policy (default: FCFS)
    if (!cfg.priority_policy().empty()) {
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
            sp.m_priority_policy = PriorityPolicy::FCFS;
        }
    } else {
        sp.m_priority_policy = PriorityPolicy::FCFS;
    }

    // Runtime estimate mode (default: USE_LIMIT)
    if (!cfg.runtime_mode().empty()) {
        std::string mode = cfg.runtime_mode();
        if (mode == "limit") {
            sp.m_runtime_mode = RuntimeEstimateMode::USE_LIMIT;
        } else if (mode == "actual") {
            sp.m_runtime_mode = RuntimeEstimateMode::USE_ACTUAL;
        } else {
            std::cerr << "Warning: Unknown runtime_mode in protobuf: " << mode
                     << " (using default: limit)" << std::endl;
            sp.m_runtime_mode = RuntimeEstimateMode::USE_LIMIT;
        }
    } else {
        sp.m_runtime_mode = RuntimeEstimateMode::USE_LIMIT;
    }

    // Trace format (options: "simple" or "lassen", default: "lassen")
    if (!cfg.trace_format().empty()) {
        std::string format = cfg.trace_format();
        if (format == "simple" || format == "lassen") {
            sp.m_trace_format = format;
        } else {
            std::cerr << "Warning: Unknown trace_format in protobuf: " << format
                     << " (using default: lassen)" << std::endl;
            sp.m_trace_format = "lassen";
        }
    } else {
        sp.m_trace_format = "lassen";
    }

    // Timestamp format (options: "epoch" or "iso", default: "iso")
    if (!cfg.timestamp_format().empty()) {
        std::string format = cfg.timestamp_format();
        if (format == "epoch" || format == "iso") {
            sp.m_timestamp_format = format;
        } else {
            std::cerr << "Warning: Unknown timestamp_format in protobuf: " << format
                     << " (using default: iso)" << std::endl;
            sp.m_timestamp_format = "iso";
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

    // Duration mode (default: EXACT)
    if (!cfg.duration_mode().empty()) {
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
            sp.m_duration_mode = DurationMode::EXACT;
        }
    } else {
        sp.m_duration_mode = DurationMode::EXACT;
    }

    // Duration distribution (default: NORMAL)
    if (!cfg.duration_distribution().empty()) {
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
            sp.m_duration_distribution = DistributionType::NORMAL;
        }
    } else {
        sp.m_duration_distribution = DistributionType::NORMAL;
    }

    // Duration scale (default: 1.0 = jobs run 100% of time_limit)
    // 0.0 doesn't make sense (zero duration), treat as "use default"
    if (cfg.duration_scale() > 0.0) {
        sp.m_duration_scale = cfg.duration_scale();
    } else {
        sp.m_duration_scale = 1.0;
    }

    // Duration stddev (default: 0.0 = no variation)
    // Negative values don't make sense, treat as "use default"
    if (cfg.duration_stddev() >= 0.0) {
        sp.m_duration_stddev = cfg.duration_stddev();
    } else {
        std::cerr << "Warning: Negative duration_stddev in protobuf: " << cfg.duration_stddev()
                 << " (using default: 0.0)" << std::endl;
        sp.m_duration_stddev = 0.0;
    }

    // Queue implementation (default: DEQUE)
    if (!cfg.queue_impl().empty()) {
        std::string impl = cfg.queue_impl();
        if (impl == "deque") {
            sp.m_queue_impl = QueueImplementation::DEQUE;
        } else if (impl == "multimap") {
            sp.m_queue_impl = QueueImplementation::MULTIMAP;
        } else if (impl == "block") {
            sp.m_queue_impl = QueueImplementation::BLOCK;
        } else if (impl == "circular") {
            sp.m_queue_impl = QueueImplementation::CIRCULAR;
        } else {
            std::cerr << "Warning: Unknown queue_impl in protobuf: " << impl
                     << " (using default: deque)" << std::endl;
            sp.m_queue_impl = QueueImplementation::DEQUE;
        }
    } else {
        sp.m_queue_impl = QueueImplementation::DEQUE;
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
            std::cerr << "Warning: Unknown circular_overflow in protobuf: " << policy
                     << " (using default: grow)" << std::endl;
            sp.m_circular_overflow = CircularOverflowPolicy::GROW;
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
