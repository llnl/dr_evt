/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <getopt.h>
#include <limits>
#include <string>
#include <iostream>
#include <cstdlib>
#include "utils/file.hpp"
#include "params/sim_params.hpp"


namespace dr_evt {

#define OPTIONS "hi:j:n:o:s:t:b:p:r:f:T:z:"
static const struct option longopts[] = {
    {"help",             no_argument,        0, 'h'},
    {"infile",           required_argument,  0, 'i'},
    {"max_jobs",         required_argument,  0, 'j'},
    {"total_nodes",      required_argument,  0, 'n'},
    {"outfile",          required_argument,  0, 'o'},
    {"seed",             required_argument,  0, 's'},
    {"max_time",         required_argument,  0, 't'},
    {"backfill_policy",  required_argument,  0, 'b'},
    {"priority_policy",  required_argument,  0, 'p'},
    {"runtime_mode",     required_argument,  0, 'r'},
    {"trace_format",     required_argument,  0, 'f'},
    {"timestamp_format", required_argument,  0, 'T'},
    {"timezone",         required_argument,  0, 'z'},
    { 0, 0, 0, 0 },
};

Sim_Params::Sim_Params()
  : m_seed(0u), m_max_jobs(10u),
    m_max_time(dr_evt::max_sim_time),
    m_is_jobs_set(false),
    m_is_time_set(false),
    m_backfill_policy(BackfillPolicy::EASY),
    m_priority_policy(PriorityPolicy::FCFS),
    m_runtime_mode(RuntimeEstimateMode::USE_LIMIT),
    m_total_nodes(dr_evt::total_nodes),
    m_trace_format("lassen"),  // Default to Lassen format for backward compatibility
    m_timestamp_format("iso"),  // Default to ISO/human-readable timestamps
    m_timezone("America/Los_Angeles")  // Default timezone
{}

void Sim_Params::getopt(int& argc, char** &argv)
{
    int c;
    m_is_jobs_set = false;
    m_is_time_set = false;

    while ((c = getopt_long(argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (c) {
            case 'h': /* --help */
                print_usage(argv[0], 0);
                break;
            case 'i': /* --infile */
                m_infile = std::string(optarg);
                break;
            case 'j': /* --max_jobs */
                m_max_jobs = static_cast<dr_evt::num_jobs_t>(atoi(optarg));
                m_is_jobs_set = true;
                break;
            case 'n': /* --total_nodes */
                m_total_nodes = static_cast<num_nodes_t>(atoi(optarg));
                break;
            case 'o': /* --outfile */
                m_outfile = std::string(optarg);
                break;
            case 's': /* --seed */
                m_seed = static_cast<unsigned>(atoi(optarg));
                break;
            case 't': /* --max_time */
                m_max_time = static_cast<dr_evt::sim_time_t>(std::stod(optarg));
                m_is_time_set = true;
                break;
            case 'b': /* --backfill_policy */
                {
                    std::string policy(optarg);
                    if (policy == "easy") {
                        m_backfill_policy = BackfillPolicy::EASY;
                    } else if (policy == "conservative") {
                        m_backfill_policy = BackfillPolicy::CONSERVATIVE;
                    } else {
                        std::cerr << "Unknown backfill policy: " << policy << std::endl;
                        print_usage(argv[0], 1);
                    }
                }
                break;
            case 'p': /* --priority_policy */
                {
                    std::string policy(optarg);
                    if (policy == "fcfs") {
                        m_priority_policy = PriorityPolicy::FCFS;
                    } else if (policy == "sjf") {
                        m_priority_policy = PriorityPolicy::SJF;
                    } else if (policy == "ljf") {
                        m_priority_policy = PriorityPolicy::LJF;
                    } else {
                        std::cerr << "Unknown priority policy: " << policy << std::endl;
                        print_usage(argv[0], 1);
                    }
                }
                break;
            case 'r': /* --runtime_mode */
                {
                    std::string mode(optarg);
                    if (mode == "limit") {
                        m_runtime_mode = RuntimeEstimateMode::USE_LIMIT;
                    } else if (mode == "actual") {
                        m_runtime_mode = RuntimeEstimateMode::USE_ACTUAL;
                    } else {
                        std::cerr << "Unknown runtime mode: " << mode << std::endl;
                        print_usage(argv[0], 1);
                    }
                }
                break;
            case 'f': /* --trace_format */
                {
                    std::string format(optarg);
                    if (format == "simple" || format == "lassen") {
                        m_trace_format = format;
                    } else {
                        std::cerr << "Unknown trace format: " << format << std::endl;
                        print_usage(argv[0], 1);
                    }
                }
                break;
            case 'T': /* --timestamp_format */
                {
                    std::string format(optarg);
                    if (format == "epoch" || format == "iso") {
                        m_timestamp_format = format;
                    } else {
                        std::cerr << "Unknown timestamp format: " << format << std::endl;
                        print_usage(argv[0], 1);
                    }
                }
                break;
            case 'z': /* --timezone */
                m_timezone = optarg;
                break;
            default:
                print_usage(argv[0], 1);
                break;
        }
    }

    if (optind != (argc - 1)) {
        print_usage (argv[0], 1);
    }

    m_infile = argv[optind];
    set_outfile(m_outfile);

    if (!m_is_jobs_set && m_is_time_set) {
        m_max_jobs = std::numeric_limits<decltype(m_max_jobs)>::max();
    }
}

void Sim_Params::print_usage(const std::string exec, int code)
{
    std::cerr <<
        "Usage: " << exec << " inputfile\n"
        "    Run a job scheduling simulation with backfilling.\n"
        "    Simulates job scheduling and resource allocation\n"
        "    for the given trace file.\n"
        "\n"
        "  OPTIONS:\n"
        "    -h, --help\n"
        "        Display this usage information\n"
        "\n"
        "    -i, --infile\n"
        "        Specify the input file name for simulation.\n"
        "\n"
        "    -j, --max_jobs\n"
        "        Specify the maximum number of jobs to run.\n"
        "\n"
        "    -n, --total_nodes\n"
        "        Specify total number of nodes in the system (default: 795).\n"
        "\n"
        "    -o, --outfile\n"
        "        Specify the output file name for simulation.\n"
        "\n"
        "    -s, --seed\n"
        "        Specify the seed for random number generator. Without this,\n"
        "        it will use a value dependent on the current system clock.\n"
        "\n"
        "    -t, --max_time\n"
        "        Specify the upper limit of simulation time to run.\n"
        "\n"
        "    -b, --backfill_policy {easy|conservative}\n"
        "        Backfilling policy (default: easy).\n"
        "        easy: Only first job gets reservation\n"
        "        conservative: All jobs get reservations\n"
        "\n"
        "    -p, --priority_policy {fcfs|sjf|ljf}\n"
        "        Job priority/ordering policy (default: fcfs).\n"
        "        fcfs: First-Come-First-Served\n"
        "        sjf: Shortest-Job-First\n"
        "        ljf: Longest-Job-First\n"
        "\n"
        "    -r, --runtime_mode {limit|actual}\n"
        "        Runtime estimate mode (default: limit).\n"
        "        limit: Use user-provided time limit (realistic)\n"
        "        actual: Use actual runtime (oracle mode)\n"
        "\n"
        "    -f, --trace_format {simple|lassen}\n"
        "        Trace file format (default: lassen).\n"
        "        simple: CSV with [arrival_time,start_time,end_time,num_nodes,...]\n"
        "        lassen: 33-column LLNL Lassen format\n"
        "\n"
        "    -T, --timestamp_format {epoch|iso}\n"
        "        Timestamp format in trace file (default: iso).\n"
        "        epoch: Unix epoch seconds (integers)\n"
        "        iso: ISO 8601 or human-readable timestamps\n"
        "\n"
        "    -z, --timezone TIMEZONE\n"
        "        Timezone for timestamp parsing (default: America/Los_Angeles).\n"
        "        Examples: UTC, America/New_York, America/Los_Angeles\n"
        "        Only used when timestamp_format=iso\n"
        "\n";
    exit(code);
}

void Sim_Params::print() const
{
    using std::to_string;
    using std::string;
    string msg;
    msg = "------ Sim params ------\n";
    msg += " - seed: " + to_string(m_seed) + "\n";
    msg += " - max_jobs: " + to_string(m_max_jobs) + "\n";
    msg += " - max_time: " + to_string(m_max_time) + "\n";
    msg += " - infile: " + m_infile + "\n";
    msg += " - outfile: " + m_outfile + "\n";
    msg += " - total_nodes: " + to_string(m_total_nodes) + "\n";
    msg += " - is_jobs_set: " + string{m_is_jobs_set? "true" : "false"} + "\n";
    msg += " - is_time_set: " + string{m_is_time_set? "true" : "false"} + "\n";

    msg += " - backfill_policy: ";
    msg += (m_backfill_policy == BackfillPolicy::EASY) ? "EASY" : "CONSERVATIVE";
    msg += "\n";

    msg += " - priority_policy: ";
    if (m_priority_policy == PriorityPolicy::FCFS) msg += "FCFS";
    else if (m_priority_policy == PriorityPolicy::SJF) msg += "SJF";
    else msg += "LJF";
    msg += "\n";

    msg += " - runtime_mode: ";
    msg += (m_runtime_mode == RuntimeEstimateMode::USE_LIMIT) ? "USE_LIMIT" : "USE_ACTUAL";
    msg += "\n";

    msg += " - trace_format: " + m_trace_format + "\n";
    msg += " - timestamp_format: " + m_timestamp_format + "\n";
    msg += " - timezone: " + m_timezone + "\n";

    std::cout << msg << std::endl;
}

void Sim_Params::set_outfile(const std::string& ofname)
{
    m_outfile = ofname;
    if (m_outfile.empty()) {
        if (!m_infile.empty()) {
            m_outfile = dr_evt::get_default_ofname_from_ifname(m_infile);
        } else {
            m_outfile = "sim_out.txt";
        }
    }
}

std::string Sim_Params::get_outfile() const
{
    return m_outfile;
}

} // end of namespace dr_evt
