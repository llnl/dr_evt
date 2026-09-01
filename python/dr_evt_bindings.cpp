/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/**
 * Python bindings for DR_EVT streaming simulation API
 *
 * Provides Python interface to:
 * - Streaming API (submit_job, advance_to, run_until_exclusive)
 * - Monitoring API (resource status, queue status, statistics)
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "sim/sim.hpp"
#include "params/sim_params.hpp"

namespace py = pybind11;
using namespace dr_evt;

PYBIND11_MODULE(dr_evt, m) {
    m.doc() = "DR_EVT: HPC Job Scheduler Simulator - Python API";

    // Enumerations
    py::enum_<DurationMode>(m, "DurationMode")
        .value("FROM_COLUMN", DurationMode::FROM_COLUMN)
        .value("EXACT", DurationMode::EXACT)
        .value("DISTRIBUTION", DurationMode::DISTRIBUTION)
        .export_values();

    py::enum_<BackfillPolicy>(m, "BackfillPolicy")
        .value("NONE", BackfillPolicy::NONE)
        .value("EASY", BackfillPolicy::EASY)
        .value("CONSERVATIVE", BackfillPolicy::CONSERVATIVE)
        .export_values();

    py::enum_<PriorityPolicy>(m, "PriorityPolicy")
        .value("FCFS", PriorityPolicy::FCFS)
        .value("SJF", PriorityPolicy::SJF)
        .value("LJF", PriorityPolicy::LJF)
        .export_values();

    // Simulation Parameters
    py::class_<Sim_Params>(m, "SimParams")
        .def(py::init<>())
        .def_readwrite("infile", &Sim_Params::m_infile)
        .def_readwrite("total_nodes", &Sim_Params::m_total_nodes)
        .def_readwrite("trace_format", &Sim_Params::m_trace_format)
        .def_readwrite("timestamp_format", &Sim_Params::m_timestamp_format)
        .def_readwrite("duration_mode", &Sim_Params::m_duration_mode)
        .def_readwrite("backfill_policy", &Sim_Params::m_backfill_policy)
        .def_readwrite("priority_policy", &Sim_Params::m_priority_policy)
        .def_readwrite("verbose", &Sim_Params::m_verbose);

    // Statistics structure
    py::class_<Simulation::Statistics>(m, "Statistics")
        .def_readonly("jobs_submitted", &Simulation::Statistics::jobs_submitted)
        .def_readonly("jobs_completed", &Simulation::Statistics::jobs_completed)
        .def_readonly("jobs_running", &Simulation::Statistics::jobs_running)
        .def_readonly("jobs_waiting", &Simulation::Statistics::jobs_waiting)
        .def_readonly("current_time", &Simulation::Statistics::current_time)
        .def_readonly("total_nodes", &Simulation::Statistics::total_nodes)
        .def_readonly("nodes_in_use", &Simulation::Statistics::nodes_in_use)
        .def_readonly("nodes_available", &Simulation::Statistics::nodes_available)
        .def_readonly("utilization", &Simulation::Statistics::utilization)
        .def_readonly("avg_wait_time", &Simulation::Statistics::avg_wait_time)
        .def_readonly("avg_turnaround_time", &Simulation::Statistics::avg_turnaround_time)
        .def_readonly("makespan", &Simulation::Statistics::makespan)
        .def("__repr__", [](const Simulation::Statistics &s) {
            return "Statistics(jobs=" + std::to_string(s.jobs_completed) + "/" +
                   std::to_string(s.jobs_submitted) + ", utilization=" +
                   std::to_string(s.utilization * 100) + "%)";
        });

    // Main Simulation class
    py::class_<Simulation>(m, "Simulation")
        .def(py::init<const Sim_Params&>())

        // Batch mode
        .def("run", &Simulation::run,
             "Run batch simulation (all jobs at once)")

        // Streaming API - Job submission
        .def("submit_job", &Simulation::submit_job,
             py::arg("job_idx"), py::arg("submit_time"),
             "Submit a job to scheduler's waiting queue")

        // Streaming API - Time advancement
        .def("run_until_exclusive", &Simulation::run_until_exclusive,
             py::arg("target_time"),
             "Advance simulation to just before target_time, excluding events at target_time")

        .def("advance_to", &Simulation::advance_to,
             py::arg("target_time"),
             "Advance simulation to target time, processing events AT target_time")

        // Monitoring - Basic state
        .def("get_current_time", &Simulation::get_current_time,
             "Get current simulation time")

        .def("get_nodes_in_use", &Simulation::get_nodes_in_use,
             "Get number of nodes currently allocated")

        .def("get_available_nodes", &Simulation::get_available_nodes,
             "Get number of nodes currently available")

        // Monitoring - Queue status
        .def("get_active_job_count", &Simulation::get_active_job_count,
             "Get number of jobs currently active (arrived but not yet scheduled)")

        .def("get_fcfs_head_shadow_time", &Simulation::get_fcfs_head_shadow_time,
             "Get estimated start time for FCFS head (shadow time)")

        // Monitoring - Comprehensive statistics
        .def("get_statistics", &Simulation::get_statistics,
             "Get comprehensive scheduling statistics")

        // Output
        .def("write_simulated_trace", &Simulation::write_simulated_trace,
             "Write simulated job trace to output file")

        .def("print_stats", [](const Simulation &sim) {
            sim.print_stats(std::cout);
        }, "Print simulation statistics to stdout")

        // Trace access
        .def("get_trace_size", [](const Simulation &sim) {
            return sim.get_trace().data().size();
        }, "Get number of jobs in loaded trace")

        .def("initialize_trace", [](Simulation &sim, size_t max_jobs) {
            return sim.initialize_trace(static_cast<num_jobs_t>(max_jobs));
        }, py::arg("max_jobs") = 0,
           "Load trace data from file (must call before streaming)");

    // Module-level version info
    m.attr("__version__") = "1.0.0";
}
