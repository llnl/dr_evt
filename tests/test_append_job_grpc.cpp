/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/**
 * Verifies AppendJobRequest works correctly over the actual gRPC wire -
 * not just that Simulation::append_job() works in-process (already
 * covered by test_append_job_api.cpp). Connects to an already-running
 * dr_evt_server (see run_grpc_tests.sh for how it's started), loads a
 * trace file with zero jobs, appends two brand-new jobs the server has
 * never seen, submits and runs them, and checks the resulting stats.
 */

#include <grpcpp/grpcpp.h>
#include <iostream>
#include <cassert>
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
        std::cerr << "Usage: " << argv[0] << " <server_address> <empty_trace_file>\n"
                  << "  <empty_trace_file> must have a valid header and zero data rows -\n"
                  << "  this test's whole point is appending jobs the server never loaded.\n";
        return 1;
    }
    std::string server_address = argv[1];
    std::string trace_file = argv[2];

    SimulationClient client(
        grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));

    try {
        ClientMessage init_req;
        auto* init = init_req.mutable_init();
        init->set_total_nodes(100);
        init->set_trace_format("simple");
        init->set_timestamp_format("epoch");
        init->set_backfill_policy("easy");
        init->set_priority_policy("fcfs");
        init->set_run_time_mode("limit");
        init->set_infile(trace_file);
        client.call(init_req);

        ClientMessage trace_req;
        trace_req.mutable_initialize_trace()->set_max_jobs(0);
        auto trace_resp = client.call(trace_req);
        uint64_t num_jobs = trace_resp.initialize_trace().num_jobs_loaded();
        if (num_jobs != 0) {
            std::cerr << "FAIL: expected 0 jobs loaded from " << trace_file
                      << ", got " << num_jobs << " - use an empty (header-only) trace file\n";
            return 1;
        }

        // Genuine streaming append over the wire: jobs the server has
        // never seen before, no prior knowledge from a loaded trace.
        ClientMessage append_req1;
        auto* a1 = append_req1.mutable_append_job();
        a1->set_submit_time(0.0);
        a1->set_num_nodes(10);
        a1->set_queue("pbatch");
        a1->set_limit_time(100.0);
        uint32_t job_idx1 = client.call(append_req1).append_job().job_idx();
        assert(job_idx1 == 0);

        ClientMessage append_req2;
        auto* a2 = append_req2.mutable_append_job();
        a2->set_submit_time(5.0);
        a2->set_num_nodes(20);
        a2->set_queue("pbatch");
        a2->set_limit_time(200.0);
        uint32_t job_idx2 = client.call(append_req2).append_job().job_idx();
        assert(job_idx2 == 1);

        ClientMessage submit_req1;
        auto* s1 = submit_req1.mutable_submit_job();
        s1->set_job_idx(job_idx1);
        s1->set_submit_time(0.0);
        client.call(submit_req1);

        ClientMessage submit_req2;
        auto* s2 = submit_req2.mutable_submit_job();
        s2->set_job_idx(job_idx2);
        s2->set_submit_time(5.0);
        client.call(submit_req2);

        ClientMessage advance_req;
        advance_req.mutable_advance_to()->set_target_time(1000.0);
        client.call(advance_req);

        ClientMessage stats_req;
        stats_req.mutable_get_statistics();
        auto stats_resp = client.call(stats_req);
        const auto& stats = stats_resp.get_statistics();

        std::cout << "submitted=" << stats.jobs_submitted()
                  << " completed=" << stats.jobs_completed()
                  << " makespan=" << stats.makespan() << "\n";

        if (stats.jobs_submitted() != 2 || stats.jobs_completed() != 2 ||
            stats.makespan() != 205.0) {
            std::cerr << "FAIL: expected submitted=2 completed=2 makespan=205\n";
            client.finish();
            return 1;
        }

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
    std::cout << "PASSED\n";
    return 0;
}
