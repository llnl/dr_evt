/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/**
 * Verifies AppendJobRequest and AppendJobsRequest work correctly over
 * the actual gRPC wire - not just that Simulation::append_job()/
 * append_jobs() work in-process (already covered by
 * test_append_job_api.cpp). Connects to an already-running dr_evt_server
 * (see run_grpc_tests.sh for how it's started), loads a trace file with
 * zero jobs, appends brand-new jobs the server has never seen (singly,
 * then as a batch), submits and runs them, and checks the resulting
 * stats.
 */

#include <grpcpp/grpcpp.h>
#include <cmath>
#include <cstdio>
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

// Sends Init (with infile only used for its header - no
// InitializeTraceRequest is ever sent, so the server never loads any
// job data of its own) on an already-constructed client.
void connect_and_init(SimulationClient& client, const std::string& trace_file)
{
    ClientMessage init_req;
    auto* init = init_req.mutable_init();
    init->set_total_nodes(100);
    init->set_trace_format("simple");
    init->set_timestamp_format("epoch");
    init->set_backfill_policy("easy");
    init->set_priority_policy("fcfs");
    init->set_run_time_mode("limit");
    init->set_infile(trace_file);
    init->set_session_name("append-job-test");
    client.call(init_req);

    ClientMessage trace_req;
    trace_req.mutable_initialize_trace()->set_max_jobs(0);
    auto trace_resp = client.call(trace_req);
    uint64_t num_jobs = trace_resp.initialize_trace().num_jobs_loaded();
    if (num_jobs != 0) {
        throw std::runtime_error("expected 0 jobs loaded from " + trace_file +
            ", got " + std::to_string(num_jobs) + " - use an empty (header-only) trace file");
    }
}

// Test 1: single-job AppendJobRequest, over the wire.
bool test_single_append(const std::string& server_address, const std::string& trace_file)
{
    std::cout << "=== Test: single AppendJobRequest ===\n";
    SimulationClient client(
        grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));

    try {
        connect_and_init(client, trace_file);

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

        ClientMessage advance_req;
        advance_req.mutable_advance_to()->set_target_time(1000.0);
        client.call(advance_req);

        ClientMessage stats_req;
        stats_req.mutable_get_statistics();
        auto stats_resp = client.call(stats_req);
        const auto& stats = stats_resp.get_statistics();

        std::cout << "  submitted=" << stats.jobs_submitted()
                  << " completed=" << stats.jobs_completed()
                  << " makespan=" << stats.makespan() << "\n";

        if (stats.jobs_submitted() != 2 || stats.jobs_completed() != 2 ||
            stats.makespan() != 205.0) {
            std::cerr << "  FAIL: expected submitted=2 completed=2 makespan=205\n";
            client.finish();
            return false;
        }

        // Finish drains the session, writes uniquely-named reports, and
        // permits a fresh Init on this same stream without stopping the
        // server process.
        ClientMessage finish_req;
        finish_req.mutable_finish_simulation();
        auto finish = client.call(finish_req).finish_simulation();
        if (finish.statistics().jobs_completed() != 2 ||
            finish.session_id().empty() ||
            finish.simulated_trace_file().empty() ||
            finish.resource_trace_file().empty() ||
            finish.statistics_file().empty()) {
            std::cerr << "  FAIL: finish response did not contain final reports\n";
            client.finish();
            return false;
        }

        connect_and_init(client, trace_file);
        std::remove(finish.simulated_trace_file().c_str());
        std::remove(finish.resource_trace_file().c_str());
        std::remove(finish.statistics_file().c_str());
    } catch (const std::exception& e) {
        std::cerr << "  FAIL: " << e.what() << "\n";
        client.finish();
        return false;
    }

    grpc::Status status = client.finish();
    if (!status.ok()) {
        std::cerr << "  FAIL: RPC failed: " << status.error_message() << "\n";
        return false;
    }
    std::cout << "  PASSED\n";
    return true;
}

// Test 2: batch AppendJobsRequest (3 jobs in one round-trip), over the
// wire - the server has never seen any of them, same as the single-job
// case, just carried in one message instead of three.
bool test_batch_append(const std::string& server_address, const std::string& trace_file)
{
    std::cout << "=== Test: batch AppendJobsRequest ===\n";
    SimulationClient client(
        grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));

    try {
        connect_and_init(client, trace_file);

        ClientMessage batch_req;
        auto* aj = batch_req.mutable_append_jobs();
        struct { double submit_time; uint32_t num_nodes; double limit_time; } jobs[] = {
            {0.0, 10, 100.0},
            {5.0, 20, 200.0},
            {10.0, 15, 150.0},
        };
        for (const auto& j : jobs) {
            auto* r = aj->add_requests();
            r->set_submit_time(j.submit_time);
            r->set_num_nodes(j.num_nodes);
            r->set_queue("pbatch");
            r->set_limit_time(j.limit_time);
        }

        auto resp = client.call(batch_req);
        const auto& job_idxs = resp.append_jobs().job_idx();
        std::cout << "  appended " << job_idxs.size() << " jobs\n";
        if (job_idxs.size() != 3 || job_idxs[0] != 0 || job_idxs[1] != 1 || job_idxs[2] != 2) {
            std::cerr << "  FAIL: expected job_idxs [0, 1, 2]\n";
            client.finish();
            return false;
        }

        ClientMessage advance_req;
        advance_req.mutable_advance_to()->set_target_time(1e9);
        client.call(advance_req);

        ClientMessage stats_req;
        stats_req.mutable_get_statistics();
        auto stats_resp = client.call(stats_req);
        const auto& stats = stats_resp.get_statistics();

        std::cout << "  submitted=" << stats.jobs_submitted()
                  << " completed=" << stats.jobs_completed()
                  << " makespan=" << stats.makespan() << "\n";

        if (stats.jobs_submitted() != 3 || stats.jobs_completed() != 3 ||
            stats.makespan() != 205.0) {
            std::cerr << "  FAIL: expected submitted=3 completed=3 makespan=205\n";
            client.finish();
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "  FAIL: " << e.what() << "\n";
        client.finish();
        return false;
    }

    grpc::Status status = client.finish();
    if (!status.ok()) {
        std::cerr << "  FAIL: RPC failed: " << status.error_message() << "\n";
        return false;
    }
    std::cout << "  PASSED\n";
    return true;
}

// Test 3: the single backfill-window query returns the same EASY reservation
// projection as the scheduler: current free capacity plus releases through
// the FCFS head's shadow time.
bool test_backfill_window(const std::string& server_address, const std::string& trace_file)
{
    std::cout << "=== Test: GetBackfillWindowRequest ===\n";
    SimulationClient client(
        grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));

    try {
        connect_and_init(client, trace_file);

        struct { uint32_t nodes; double limit; } jobs[] = {
            {40, 50.0}, {60, 100.0}, {100, 200.0},
        };
        for (const auto& job : jobs) {
            ClientMessage append;
            auto* request = append.mutable_append_job();
            request->set_submit_time(0.0);
            request->set_num_nodes(job.nodes);
            request->set_queue("pbatch");
            request->set_limit_time(job.limit);
            client.call(append);
        }

        ClientMessage advance;
        advance.mutable_advance_to()->set_target_time(0.0);
        client.call(advance);

        ClientMessage query;
        query.mutable_get_backfill_window();
        const auto query_response = client.call(query);
        const auto& window = query_response.get_backfill_window();
        std::cout << "  now=" << window.current_time()
                  << " available=" << window.available_nodes()
                  << " shadow=" << window.shadow_time()
                  << " releases=" << window.releases_size() << "\n";

        const bool expected_snapshot =
            std::fabs(window.current_time()) < 1e-12 &&
            window.available_nodes() == 0 &&
            std::fabs(window.shadow_time() - 100.0) < 1e-12 &&
            window.releases_size() == 2 &&
            std::fabs(window.releases(0).time() - 50.0) < 1e-12 &&
            window.releases(0).nodes_released() == 40 &&
            std::fabs(window.releases(1).time() - 100.0) < 1e-12 &&
            window.releases(1).nodes_released() == 60;
        if (!expected_snapshot) {
            std::cerr << "  FAIL: expected 0 free nodes, shadow=100, "
                      << "releases [(50,40), (100,60)]\n";
            client.finish();
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "  FAIL: " << e.what() << "\n";
        client.finish();
        return false;
    }

    grpc::Status status = client.finish();
    if (!status.ok()) {
        std::cerr << "  FAIL: RPC failed: " << status.error_message() << "\n";
        return false;
    }
    std::cout << "  PASSED\n";
    return true;
}

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

    bool ok = true;
    ok &= test_single_append(server_address, trace_file);
    ok &= test_batch_append(server_address, trace_file);
    ok &= test_backfill_window(server_address, trace_file);

    if (!ok) {
        std::cerr << "SOME TESTS FAILED\n";
        return 1;
    }
    std::cout << "PASSED\n";
    return 0;
}
