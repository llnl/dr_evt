/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <grpcpp/grpcpp.h>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <iostream>

#include "dr_evt_service.grpc.pb.h"
#include "sim/sim.hpp"
#include "params/sim_params.hpp"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReaderWriter;
using grpc::Status;

namespace dr_evt_grpc {

namespace {

std::atomic<uint64_t> next_session_sequence{0};

bool is_safe_session_name(const std::string& name)
{
    if (name.empty() || name.size() > 128) {
        return false;
    }
    for (unsigned char character : name) {
        if (!std::isalnum(character) && character != '-' &&
            character != '_' && character != '.') {
            return false;
        }
    }
    return name != "." && name != "..";
}

std::string make_session_id(const std::string& session_name)
{
    const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const uint64_t sequence = next_session_sequence.fetch_add(1);
    const uint32_t nonce = std::random_device{}();
    return session_name + "-" + std::to_string(timestamp) + "-" +
           std::to_string(sequence) + "-" + std::to_string(nonce);
}

void copy_statistics(const dr_evt::Simulation::Statistics& statistics,
                     GetStatisticsResponse* response)
{
    response->set_jobs_submitted(statistics.jobs_submitted);
    response->set_jobs_completed(statistics.jobs_completed);
    response->set_jobs_running(statistics.jobs_running);
    response->set_jobs_waiting(statistics.jobs_waiting);
    response->set_current_time(statistics.current_time);
    response->set_total_nodes(statistics.total_nodes);
    response->set_nodes_in_use(statistics.nodes_in_use);
    response->set_nodes_available(statistics.nodes_available);
    response->set_utilization(statistics.utilization);
    response->set_avg_wait_time(statistics.avg_wait_time);
    response->set_avg_turnaround_time(statistics.avg_turnaround_time);
    response->set_makespan(statistics.makespan);
}

void write_statistics_file(const std::string& filename,
                           const dr_evt::Simulation::Statistics& statistics)
{
    std::ofstream output(filename);
    if (!output) {
        throw std::runtime_error("Failed to write statistics file: " + filename);
    }
    output << "{\n"
           << "  \"jobs_submitted\": " << statistics.jobs_submitted << ",\n"
           << "  \"jobs_completed\": " << statistics.jobs_completed << ",\n"
           << "  \"jobs_running\": " << statistics.jobs_running << ",\n"
           << "  \"jobs_waiting\": " << statistics.jobs_waiting << ",\n"
           << "  \"current_time\": " << statistics.current_time << ",\n"
           << "  \"total_nodes\": " << statistics.total_nodes << ",\n"
           << "  \"nodes_in_use\": " << statistics.nodes_in_use << ",\n"
           << "  \"nodes_available\": " << statistics.nodes_available << ",\n"
           << "  \"utilization\": " << statistics.utilization << ",\n"
           << "  \"avg_wait_time\": " << statistics.avg_wait_time << ",\n"
           << "  \"avg_turnaround_time\": " << statistics.avg_turnaround_time << ",\n"
           << "  \"makespan\": " << statistics.makespan << "\n"
           << "}\n";
    if (!output) {
        throw std::runtime_error("Failed to finish statistics file: " + filename);
    }
}

} // namespace

class SimulationServiceImpl final : public SimulationService::Service {
public:
    Status Session(ServerContext* context,
                    ServerReaderWriter<ServerMessage, ClientMessage>* stream) override
    {
        // sim_params must outlive sim: Simulation stores its parameters
        // as a reference (const Sim_Params& m_params), not a copy - a
        // local Sim_Params scoped to just the kInit case block below
        // would be destroyed as soon as that block ends, leaving
        // m_params a dangling reference to deallocated stack memory for
        // the rest of the session. Confirmed empirically: this was
        // exactly the cause of every session loading precisely 1 job
        // from its trace regardless of the trace's actual size -
        // m_params.m_is_jobs_set/m_max_jobs were reading back as
        // garbage (m_is_jobs_set=16, not a valid bool at all) by the
        // time initialize_trace() ran, since kInit's own local sp had
        // already gone out of scope by then.
        dr_evt::Sim_Params sim_params;
        std::unique_ptr<dr_evt::Simulation> sim;
        std::string session_id;
        std::string simulated_trace_file;
        std::string resource_trace_file;
        std::string statistics_file;
        ClientMessage req;

        while (stream->Read(&req)) {
            ServerMessage resp;
            resp.set_request_id(req.request_id());

            try {
                switch (req.request_case()) {
                    case ClientMessage::kInit: {
                        if (sim) {
                            throw std::runtime_error("Init already called on this session");
                        }
                        const InitRequest& r = req.init();
                        if (!is_safe_session_name(r.session_name())) {
                            throw std::runtime_error(
                                "session_name must be 1-128 filename-safe characters "
                                "(letters, digits, '.', '_' or '-')");
                        }
                        session_id = make_session_id(r.session_name());
                        simulated_trace_file = session_id + ".simulated.csv";
                        resource_trace_file = session_id + ".resource.csv";
                        statistics_file = session_id + ".statistics.json";
                        dr_evt::Sim_Params& sp = sim_params;
                        sp.m_total_nodes = r.total_nodes();
                        if (!r.trace_format().empty()) sp.m_trace_format = r.trace_format();
                        if (!r.timestamp_format().empty()) sp.m_timestamp_format = r.timestamp_format();
                        if (!r.timezone().empty()) sp.m_timezone = r.timezone();
                        if (!r.infile().empty()) sp.m_infile = r.infile();
                        sp.m_msec_output = r.msec_output();

                        if (r.backfill_policy().empty()) sp.m_backfill_policy = dr_evt::BackfillPolicy::EASY;
                        else if (r.backfill_policy() == "easy") sp.m_backfill_policy = dr_evt::BackfillPolicy::EASY;
                        else if (r.backfill_policy() == "conservative") sp.m_backfill_policy = dr_evt::BackfillPolicy::CONSERVATIVE;
                        else if (r.backfill_policy() == "none") sp.m_backfill_policy = dr_evt::BackfillPolicy::NONE;
                        else {
                            throw std::runtime_error("Unknown backfill_policy: " + r.backfill_policy());
                        }

                        if (r.priority_policy().empty()) sp.m_priority_policy = dr_evt::PriorityPolicy::FCFS;
                        else if (r.priority_policy() == "fcfs") sp.m_priority_policy = dr_evt::PriorityPolicy::FCFS;
                        else if (r.priority_policy() == "fcfs_alt") sp.m_priority_policy = dr_evt::PriorityPolicy::FCFS_ALT;
                        else if (r.priority_policy() == "sjf") sp.m_priority_policy = dr_evt::PriorityPolicy::SJF;
                        else if (r.priority_policy() == "ljf") sp.m_priority_policy = dr_evt::PriorityPolicy::LJF;
                        else {
                            throw std::runtime_error("Unknown priority_policy: " + r.priority_policy());
                        }

                        if (r.run_time_mode().empty()) sp.m_run_time_mode = dr_evt::RunTimeMode::ACTUAL;  // default
                        else if (r.run_time_mode() == "actual") sp.m_run_time_mode = dr_evt::RunTimeMode::ACTUAL;
                        else if (r.run_time_mode() == "distribution") sp.m_run_time_mode = dr_evt::RunTimeMode::DISTRIBUTION;
                        else if (r.run_time_mode() == "limit") sp.m_run_time_mode = dr_evt::RunTimeMode::LIMIT;
                        else {
                            throw std::runtime_error("Unknown run_time_mode: " + r.run_time_mode() + " (valid: actual, distribution, limit)");
                        }

                        // block_size/wait_queue_capacity: 0 means "use Sim_Params'
                        // own constructor default" - handled by Sim_Params
                        // itself when constructing CircularBufferFCFSScheduler/
                        // BlockQueueFCFSScheduler, not by this function.
                        if (r.queue_impl().empty()) sp.m_queue_impl = dr_evt::QueueImplementation::CIRCULAR;
                        else if (r.queue_impl() == "circular") sp.m_queue_impl = dr_evt::QueueImplementation::CIRCULAR;
                        else if (r.queue_impl() == "deque") sp.m_queue_impl = dr_evt::QueueImplementation::DEQUE;
                        else if (r.queue_impl() == "multimap") sp.m_queue_impl = dr_evt::QueueImplementation::MULTIMAP;
                        else if (r.queue_impl() == "block") sp.m_queue_impl = dr_evt::QueueImplementation::BLOCK;
                        else {
                            throw std::runtime_error("Unknown queue_impl: " + r.queue_impl());
                        }

                        if (r.block_size() != 0) sp.m_block_size = r.block_size();
                        if (r.wait_queue_capacity() != 0) sp.m_wait_queue_capacity = r.wait_queue_capacity();

                        if (r.wait_queue_overflow().empty()) sp.m_wait_queue_overflow = dr_evt::CircularOverflowPolicy::GROW;
                        else if (r.wait_queue_overflow() == "abort") sp.m_wait_queue_overflow = dr_evt::CircularOverflowPolicy::ABORT;
                        else if (r.wait_queue_overflow() == "grow") sp.m_wait_queue_overflow = dr_evt::CircularOverflowPolicy::GROW;
                        else {
                            throw std::runtime_error("Unknown wait_queue_overflow: " + r.wait_queue_overflow());
                        }

                        sp.set_outfile(simulated_trace_file);
                        sp.set_resource_trace(resource_trace_file);

                        sim = std::make_unique<dr_evt::Simulation>(sp);
                        auto* init_response = resp.mutable_init();
                        init_response->set_ok(true);
                        init_response->set_session_id(session_id);
                        init_response->set_simulated_trace_file(simulated_trace_file);
                        init_response->set_resource_trace_file(resource_trace_file);
                        init_response->set_statistics_file(statistics_file);
                        break;
                    }
                    case ClientMessage::kInitializeTrace: {
                        require_init(sim);
                        dr_evt::num_jobs_t loaded = sim->initialize_trace(
                            static_cast<dr_evt::num_jobs_t>(req.initialize_trace().max_jobs()));
                        resp.mutable_initialize_trace()->set_num_jobs_loaded(loaded);
                        break;
                    }
                    case ClientMessage::kAppendJob: {
                        require_init(sim);
                        const AppendJobRequest& r = req.append_job();
                        dr_evt::job_no_t job_idx = sim->append_job(
                            r.submit_time(), r.num_nodes(), r.queue(), r.limit_time());
                        resp.mutable_append_job()->set_job_idx(job_idx);
                        break;
                    }
                    case ClientMessage::kAppendJobs: {
                        require_init(sim);
                        const AppendJobsRequest& r = req.append_jobs();
                        std::vector<dr_evt::Simulation::Job_Append_Request> reqs;
                        reqs.reserve(r.requests_size());
                        for (const auto& jd : r.requests()) {
                            reqs.push_back(dr_evt::Simulation::Job_Append_Request{
                                jd.submit_time(), jd.num_nodes(), jd.queue(), jd.limit_time()});
                        }
                        std::vector<dr_evt::job_no_t> job_idxs = sim->append_jobs(reqs);
                        auto* out = resp.mutable_append_jobs();
                        for (dr_evt::job_no_t idx : job_idxs) {
                            out->add_job_idx(idx);
                        }
                        break;
                    }
                    case ClientMessage::kAdvanceTo: {
                        require_init(sim);
                        sim->advance_to(req.advance_to().target_time());
                        resp.mutable_advance_to();
                        break;
                    }
                    case ClientMessage::kRunUntilExclusive: {
                        require_init(sim);
                        sim->run_until_exclusive(req.run_until_exclusive().target_time());
                        resp.mutable_run_until_exclusive();
                        break;
                    }
                    case ClientMessage::kGetCurrentTime: {
                        require_init(sim);
                        resp.mutable_get_current_time()->set_current_time(sim->get_current_time());
                        break;
                    }
                    case ClientMessage::kGetNodesInUse: {
                        require_init(sim);
                        resp.mutable_get_nodes_in_use()->set_nodes_in_use(sim->get_nodes_in_use());
                        break;
                    }
                    case ClientMessage::kGetAvailableNodes: {
                        require_init(sim);
                        resp.mutable_get_available_nodes()->set_available_nodes(sim->get_available_nodes());
                        break;
                    }
                    case ClientMessage::kGetActiveJobCount: {
                        require_init(sim);
                        resp.mutable_get_active_job_count()->set_active_job_count(sim->get_active_job_count());
                        break;
                    }
                    case ClientMessage::kGetFcfsHeadShadowTime: {
                        require_init(sim);
                        resp.mutable_get_fcfs_head_shadow_time()->set_shadow_time(sim->get_fcfs_head_shadow_time());
                        break;
                    }
                    case ClientMessage::kGetBackfillWindow: {
                        require_init(sim);
                        const auto window = sim->get_backfill_window();
                        auto* out = resp.mutable_get_backfill_window();
                        out->set_current_time(window.current_time);
                        out->set_available_nodes(window.available_nodes);
                        out->set_shadow_time(window.shadow_time);
                        for (const auto& release : window.releases) {
                            auto* event = out->add_releases();
                            event->set_time(release.time);
                            event->set_nodes_released(release.nodes_released);
                        }
                        break;
                    }
                    case ClientMessage::kGetStatistics: {
                        require_init(sim);
                        auto stats = sim->get_statistics();
                        copy_statistics(stats, resp.mutable_get_statistics());
                        break;
                    }
                    case ClientMessage::kGetTraceSize: {
                        require_init(sim);
                        resp.mutable_get_trace_size()->set_trace_size(sim->get_trace().data().size());
                        break;
                    }
                    case ClientMessage::kFinishSimulation: {
                        require_init(sim);
                        sim->advance_to(std::numeric_limits<dr_evt::sim_time_t>::max());
                        sim->write_simulated_trace();
                        sim->write_resource_trace(resource_trace_file);
                        const auto stats = sim->get_statistics();
                        write_statistics_file(statistics_file, stats);

                        auto* out = resp.mutable_finish_simulation();
                        copy_statistics(stats, out->mutable_statistics());
                        out->set_session_id(session_id);
                        out->set_simulated_trace_file(simulated_trace_file);
                        out->set_resource_trace_file(resource_trace_file);
                        out->set_statistics_file(statistics_file);

                        // The process and stream stay alive. A later Init on
                        // this stream creates a fresh, isolated simulation.
                        sim.reset();
                        sim_params = dr_evt::Sim_Params();
                        session_id.clear();
                        simulated_trace_file.clear();
                        resource_trace_file.clear();
                        statistics_file.clear();
                        break;
                    }
                    case ClientMessage::REQUEST_NOT_SET:
                    default:
                        throw std::runtime_error("Empty or unrecognized request");
                }
            } catch (const std::exception& e) {
                resp.mutable_error()->set_message(e.what());
            }

            if (!stream->Write(resp)) {
                break;
            }
        }

        return Status::OK;
    }

private:
    static void require_init(const std::unique_ptr<dr_evt::Simulation>& sim)
    {
        if (!sim) {
            throw std::runtime_error("Init must be called before any other request on this session");
        }
    }
};

} // namespace dr_evt_grpc

void RunServer(const std::string& address)
{
    dr_evt_grpc::SimulationServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "DR_EVT simulation server listening on " + address + "\n";
    server->Wait();
}

int main(int argc, char** argv)
{
    std::string address = "0.0.0.0:50051";
    if (argc > 1) {
        address = argv[1];
    }
    RunServer(address);
    return 0;
}
