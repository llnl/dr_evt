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
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <memory>
#include <string>

#include "dr_evt_service.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderWriter;
using dr_evt_grpc::SimulationService;
using dr_evt_grpc::ClientMessage;
using dr_evt_grpc::ServerMessage;

class SimulationClient {
public:
    explicit SimulationClient(std::shared_ptr<Channel> channel)
      : m_stub(SimulationService::NewStub(channel)),
        m_stream(m_stub->Session(&m_context)),
        m_next_request_id(1)
    {}

    // Sends req (with a freshly assigned request_id) and blocks for the
    // matching response. Since this client issues one request at a time
    // and waits for its response before sending the next, request/response
    // ordering is trivially preserved - request_id is still set and
    // checked as a sanity check, and to establish the pattern a more
    // pipelined client would need.
    ServerMessage call(ClientMessage req)
    {
        uint64_t id = m_next_request_id++;
        req.set_request_id(id);

        if (!m_stream->Write(req)) {
            throw std::runtime_error("Failed to write request to server (stream closed)");
        }

        ServerMessage resp;
        if (!m_stream->Read(&resp)) {
            throw std::runtime_error("Failed to read response from server (stream closed)");
        }
        if (resp.request_id() != id) {
            throw std::runtime_error("Response request_id mismatch (got " +
                std::to_string(resp.request_id()) + ", expected " + std::to_string(id) + ")");
        }
        if (resp.response_case() == ServerMessage::kError) {
            throw std::runtime_error("Server error: " + resp.error().message());
        }
        return resp;
    }

    grpc::Status finish()
    {
        m_stream->WritesDone();
        return m_stream->Finish();
    }

private:
    std::unique_ptr<SimulationService::Stub> m_stub;
    ClientContext m_context;
    std::unique_ptr<ClientReaderWriter<ClientMessage, ServerMessage>> m_stream;
    uint64_t m_next_request_id;
};

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <server_address> <job_data_file>\n"
                  << "  e.g., " << argv[0] << " localhost:50051 /path/to/jobs.csv\n"
                  << "  <job_data_file>: simple-format CSV (job_submit_time,num_nodes,\n"
                  << "  queue,time_limit) - this is *client-side* job data the server\n"
                  << "  has never seen; the server never loads this file itself (no\n"
                  << "  InitializeTraceRequest is sent at all - see below).\n";
        return 1;
    }
    std::string server_address = argv[1];
    std::string job_data_file = argv[2];

    // Parse each job's full data client-side. Unlike the old
    // submit_job()-only pattern (where the server independently loaded
    // the same file via InitializeTrace, and the client just echoed
    // back a submit_time the server already had), this file is *only*
    // ever read here - the server never sees it, and genuinely learns
    // about each job for the first time via AppendJobRequest below.
    struct JobData {
        double submit_time;
        uint32_t num_nodes;
        std::string queue;
        double limit_time;
    };
    std::vector<JobData> jobs;
    {
        std::ifstream ifs(job_data_file);
        if (!ifs) {
            std::cerr << "Failed to open job data file: " << job_data_file << std::endl;
            return 1;
        }
        std::string line;
        std::getline(ifs, line);  // header
        while (std::getline(ifs, line)) {
            if (line.empty()) continue;
            std::istringstream iss(line);
            std::string field;
            JobData j;
            std::getline(iss, field, ','); j.submit_time = std::stod(field);
            std::getline(iss, field, ','); j.num_nodes = static_cast<uint32_t>(std::stoul(field));
            std::getline(iss, field, ','); j.queue = field;
            std::getline(iss, field, ','); j.limit_time = std::stod(field);
            jobs.push_back(j);
        }
    }

    SimulationClient client(
        grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));

    try {
        // 1. Initialize the simulation on the server. infile is only
        // used for its header (Trace's constructor validates the
        // column format) - its data rows are never read, since step 2
        // below (InitializeTraceRequest) is deliberately never sent.
        ClientMessage init_req;
        auto* init = init_req.mutable_init();
        init->set_total_nodes(100);
        init->set_trace_format("simple");
        init->set_timestamp_format("epoch");
        init->set_backfill_policy("easy");
        init->set_priority_policy("fcfs");
        init->set_run_time_mode("limit");  // append_job()'d jobs have no actual_run_time column to read
        init->set_infile(job_data_file);
        init->set_queue_impl("circular");
        client.call(init_req);
        std::cout << "Session initialized. Server has not loaded any jobs yet.\n";

        // 2. Append every job - the server has never seen any of this
        // data before now. Each AppendJobRequest only adds the record
        // to the server's store and returns its job_idx; it does not
        // submit it to the scheduler (that's step 3, same two-step
        // shape as the C++ Trace::append_job()/Simulation::submit_job()
        // pair).
        std::vector<uint32_t> job_idxs;
        for (const auto& j : jobs) {
            ClientMessage append_req;
            auto* a = append_req.mutable_append_job();
            a->set_submit_time(j.submit_time);
            a->set_num_nodes(j.num_nodes);
            a->set_queue(j.queue);
            a->set_limit_time(j.limit_time);
            auto resp = client.call(append_req);
            job_idxs.push_back(resp.append_job().job_idx());
        }
        std::cout << "Appended " + std::to_string(jobs.size()) +
            " jobs the server had never seen before.\n";

        // 3. Now submit every appended job, using the same submit_time
        // each was appended with. All submissions happen before the
        // first advance_to() call below, so m_current_time is still 0
        // throughout this loop and the "submit_time >= current_time"
        // precondition holds regardless of submit_time ordering.
        for (size_t i = 0; i < jobs.size(); ++i) {
            ClientMessage submit_req;
            auto* submit = submit_req.mutable_submit_job();
            submit->set_job_idx(job_idxs[i]);
            submit->set_submit_time(jobs[i].submit_time);
            client.call(submit_req);
        }
        std::cout << "Submitted " + std::to_string(jobs.size()) + " jobs.\n";

        // 4. Advance the simulation far enough to complete all jobs.
        // A real client would typically advance incrementally and check
        // status between steps rather than jumping to a single large
        // future time - see AppendJobRequest usage from a genuinely
        // incremental driver in test_streaming_api.cpp's tests, which
        // interleave append_job()/submit_job()/advance_to() rather than
        // batching all of one before any of the next.
        ClientMessage advance_req;
        advance_req.mutable_advance_to()->set_target_time(1e9);
        client.call(advance_req);

        // 5. Query final statistics
        ClientMessage stats_req;
        stats_req.mutable_get_statistics();
        auto stats_resp = client.call(stats_req);
        const auto& stats = stats_resp.get_statistics();

        std::cout << "\n=== Final Statistics ===\n"
                  << "Jobs submitted:  " << stats.jobs_submitted() << "\n"
                  << "Jobs completed:  " << stats.jobs_completed() << "\n"
                  << "Current time:    " << stats.current_time() << "\n"
                  << "Utilization:     " << (stats.utilization() * 100.0) << "%\n"
                  << "Avg wait time:   " << stats.avg_wait_time() << "\n"
                  << "Makespan:        " << stats.makespan() << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Client error: " << e.what() << std::endl;
        client.finish();
        return 1;
    }

    grpc::Status status = client.finish();
    if (!status.ok()) {
        std::cerr << "RPC failed: " << status.error_message() << std::endl;
        return 1;
    }
    return 0;
}
