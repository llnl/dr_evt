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
        std::cerr << "Usage: " << argv[0] << " <server_address> <trace_file>\n"
                  << "  e.g., " << argv[0] << " localhost:50051 /path/to/trace.csv" << std::endl;
        return 1;
    }
    std::string server_address = argv[1];
    std::string trace_file = argv[2];

    // Read each job's submit_time from column 0 of the simple-format CSV.
    // The server loads the same file independently for its own Trace
    // object (via InitializeTrace), but submit_job() takes an explicit
    // submit_time argument rather than reading it from the loaded trace -
    // in a real streaming/online scenario the client is the one that
    // knows when each job actually arrives (e.g. from an external feed),
    // so this mirrors that: the client is authoritative on timing, the
    // server is authoritative on scheduling state.
    std::vector<double> submit_times;
    {
        std::ifstream ifs(trace_file);
        if (!ifs) {
            std::cerr << "Failed to open trace file: " << trace_file << std::endl;
            return 1;
        }
        std::string line;
        std::getline(ifs, line);  // header
        while (std::getline(ifs, line)) {
            if (line.empty()) continue;
            std::istringstream iss(line);
            std::string first_field;
            std::getline(iss, first_field, ',');
            submit_times.push_back(std::stod(first_field));
        }
    }

    SimulationClient client(
        grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));

    try {
        // 1. Initialize the simulation on the server
        ClientMessage init_req;
        auto* init = init_req.mutable_init();
        init->set_total_nodes(100);
        init->set_trace_format("simple");
        init->set_timestamp_format("epoch");
        init->set_backfill_policy("easy");
        init->set_priority_policy("fcfs");
        init->set_duration_mode("limit");
        init->set_run_time_mode("exact");
        init->set_infile(trace_file);
        // Demonstrates the queue_impl field added to InitRequest.
        // circular is already Sim_Params' own default, so this call is
        // redundant with leaving the field unset - kept explicit here
        // as a working usage example.
        init->set_queue_impl("circular");
        client.call(init_req);
        std::cout << "Session initialized." << std::endl;

        // 2. Load the trace file on the server
        ClientMessage trace_req;
        trace_req.mutable_initialize_trace()->set_max_jobs(0);  // 0 = no limit
        auto trace_resp = client.call(trace_req);
        uint64_t num_jobs = trace_resp.initialize_trace().num_jobs_loaded();
        std::cout << "Loaded " + std::to_string(num_jobs) + " jobs from trace.\n";

        if (num_jobs != submit_times.size()) {
            throw std::runtime_error("Server loaded " + std::to_string(num_jobs) +
                " jobs but client parsed " + std::to_string(submit_times.size()) +
                " submit times from the same file - trace_format mismatch?");
        }

        // 3. Submit every job using its real submit_time, read from the
        // trace file above. All submissions happen before the first
        // advance_to() call below, so m_current_time is still 0 throughout
        // this loop and the "submit_time >= current_time" precondition
        // holds regardless of submit_time ordering.
        for (uint64_t i = 0; i < num_jobs; ++i) {
            ClientMessage submit_req;
            auto* submit = submit_req.mutable_submit_job();
            submit->set_job_idx(static_cast<uint32_t>(i));
            submit->set_submit_time(submit_times[i]);
            client.call(submit_req);
        }
        std::cout << "Submitted " + std::to_string(num_jobs) + " jobs.\n";

        // 4. Advance the simulation far enough to complete all jobs.
        // A real client would typically advance incrementally and check
        // status between steps rather than jumping to a single far-future
        // time, but for this example one large advance is enough to drain
        // the queue.
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
