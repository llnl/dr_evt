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
#include <memory>
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

                        // block_size/circular_capacity: 0 means "use Sim_Params'
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
                        if (r.circular_capacity() != 0) sp.m_circular_capacity = r.circular_capacity();

                        if (r.circular_overflow().empty()) sp.m_circular_overflow = dr_evt::CircularOverflowPolicy::GROW;
                        else if (r.circular_overflow() == "abort") sp.m_circular_overflow = dr_evt::CircularOverflowPolicy::ABORT;
                        else if (r.circular_overflow() == "grow") sp.m_circular_overflow = dr_evt::CircularOverflowPolicy::GROW;
                        else {
                            throw std::runtime_error("Unknown circular_overflow: " + r.circular_overflow());
                        }

                        sim = std::make_unique<dr_evt::Simulation>(sp);
                        resp.mutable_init()->set_ok(true);
                        break;
                    }
                    case ClientMessage::kInitializeTrace: {
                        require_init(sim);
                        dr_evt::num_jobs_t loaded = sim->initialize_trace(
                            static_cast<dr_evt::num_jobs_t>(req.initialize_trace().max_jobs()));
                        resp.mutable_initialize_trace()->set_num_jobs_loaded(loaded);
                        break;
                    }
                    case ClientMessage::kSubmitJob: {
                        require_init(sim);
                        const SubmitJobRequest& r = req.submit_job();
                        sim->submit_job(r.job_idx(), r.submit_time());
                        resp.mutable_submit_job();
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
                    case ClientMessage::kGetStatistics: {
                        require_init(sim);
                        auto stats = sim->get_statistics();
                        auto* out = resp.mutable_get_statistics();
                        out->set_jobs_submitted(stats.jobs_submitted);
                        out->set_jobs_completed(stats.jobs_completed);
                        out->set_jobs_running(stats.jobs_running);
                        out->set_jobs_waiting(stats.jobs_waiting);
                        out->set_current_time(stats.current_time);
                        out->set_total_nodes(stats.total_nodes);
                        out->set_nodes_in_use(stats.nodes_in_use);
                        out->set_nodes_available(stats.nodes_available);
                        out->set_utilization(stats.utilization);
                        out->set_avg_wait_time(stats.avg_wait_time);
                        out->set_avg_turnaround_time(stats.avg_turnaround_time);
                        out->set_makespan(stats.makespan);
                        break;
                    }
                    case ClientMessage::kGetTraceSize: {
                        require_init(sim);
                        resp.mutable_get_trace_size()->set_trace_size(sim->get_trace().data().size());
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
