/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

/** @file dr_evt_client.cpp
 * @brief Command-line client for the DR_EVT protobuf simulation service.
 */

#include <fstream>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "dr_evt_config.hpp"
#include "dr_evt_service.grpc.pb.h"

using dr_evt_grpc::ClientMessage;
using dr_evt_grpc::ServerMessage;
using dr_evt_grpc::SimulationService;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderWriter;

namespace {
#if DR_EVT_LEGACY_QUEUE_INPUT
constexpr const char *kDefaultQueueInput = "pbatch";
#else
constexpr const char *kDefaultQueueInput =
    "1"; ///< Numeric ID of the default Queue1.
#endif
} // namespace

/**
 * @brief Synchronous request/response facade over a bidirectional gRPC stream.
 * @details A call is outstanding at most one at a time.  Monotonic request IDs
 * detect a mismatched response even though the transport preserves ordering.
 * The client owns the context, generated stub, and stream for their full RPC
 * lifetime; finish() must be called after the final request.
 */
class SimulationClient {
public:
  /** @brief Open a streaming session over an existing gRPC channel.
   * @param[in] channel Connected channel used to create the service stub. */
  explicit SimulationClient(std::shared_ptr<Channel> channel)
      : m_stub(SimulationService::NewStub(channel)),
        m_stream(m_stub->Session(&m_context)), m_next_request_id(1) {}

  /** @brief Send one request and wait for its matching response.
   * @param[in] req Client request; its request ID is replaced by this client.
   * @return Server response with the matching request ID.
   * @throws std::runtime_error for transport, ordering, or server errors.
   * @details Sends req (with a freshly assigned request_id) and blocks for
   * the matching response. Since this client issues one request at a time
   * and waits for its response before sending the next, request/response
   * ordering is trivially preserved. The request ID is still set and
   * checked as a sanity check, and to establish the pattern a more
   * pipelined client would need. */
  ServerMessage call(ClientMessage req) {
    uint64_t id = m_next_request_id++;
    req.set_request_id(id);

    if (!m_stream->Write(req)) {
      throw std::runtime_error(
          "Failed to write request to server (stream closed)");
    }

    ServerMessage resp;
    if (!m_stream->Read(&resp)) {
      throw std::runtime_error(
          "Failed to read response from server (stream closed)");
    }
    if (resp.request_id() != id) {
      throw std::runtime_error("Response request_id mismatch (got " +
                               std::to_string(resp.request_id()) +
                               ", expected " + std::to_string(id) + ")");
    }
    if (resp.response_case() == ServerMessage::kError) {
      throw std::runtime_error("Server error: " + resp.error().message());
    }
    return resp;
  }

  /** @brief Finish the streaming RPC and obtain its final status.
   * @return gRPC status returned by the server. */
  grpc::Status finish() {
    m_stream->WritesDone();
    return m_stream->Finish();
  }

private:
  /** @brief Generated service proxy used to create the session stream. */
  std::unique_ptr<SimulationService::Stub> m_stub;
  /** @brief Context owning cancellation, metadata, and status for this RPC. */
  ClientContext m_context;
  /** @brief Active bidirectional stream; valid from construction through
   * finish(). */
  std::unique_ptr<ClientReaderWriter<ClientMessage, ServerMessage>> m_stream;
  /** @brief Correlation ID assigned to the next request; zero is never emitted.
   */
  uint64_t m_next_request_id;
};

/** @brief Run the example gRPC streaming client.
 * @param[in] argc Command-line argument count.
 * @param[in] argv Command-line argument vector.
 * @return Process status: zero when the session completes successfully. */
int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr
        << "Usage: " << argv[0] << " <server_address> <job_data_file>\n"
        << "  e.g., " << argv[0] << " localhost:50051 /path/to/jobs.csv\n"
        << "  <job_data_file>: queue-free simple-format CSV\n"
        << "  (job_submit_time,num_nodes,time_limit) - this is *client-side*\n"
        << "  job data the server\n"
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
  /** @brief Client-side representation of one row from the job CSV.
   * @details Values are forwarded to AppendJobRequest without first loading
   * the trace on the server. */
  struct JobData {
    double submit_time; ///< Input arrival timestamp in simulation seconds.
    uint32_t num_nodes; ///< Input number of nodes requested by the job.
    std::string queue;  ///< Compile-time schema spelling of default Queue1.
    double limit_time;  ///< Input wall-time limit in simulation seconds.
  };
  std::vector<JobData> jobs;
  {
    std::ifstream ifs(job_data_file);
    if (!ifs) {
      std::cerr << "Failed to open job data file: " << job_data_file
                << std::endl;
      return 1;
    }
    std::string line;
    std::getline(ifs, line);
    if (line != "job_submit_time,num_nodes,time_limit") {
      std::cerr << "Expected queue-free simple job CSV header in "
                << job_data_file << std::endl;
      return 1;
    }
    while (std::getline(ifs, line)) {
      if (line.empty())
        continue;
      std::istringstream iss(line);
      std::string field;
      JobData j;
      std::getline(iss, field, ',');
      j.submit_time = std::stod(field);
      std::getline(iss, field, ',');
      j.num_nodes = static_cast<uint32_t>(std::stoul(field));
      std::getline(iss, field, ',');
      j.queue = kDefaultQueueInput;
      j.limit_time = std::stod(field);
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
    auto *init = init_req.mutable_init();
    init->set_total_nodes(100);
    init->set_trace_format("simple");
    init->set_timestamp_format("epoch");
    init->set_backfill_policy("easy");
    init->set_priority_policy("fcfs");
    init->set_run_time_mode(
        "limit"); // append_job()'d jobs have no actual_run_time column to read
    init->set_infile(job_data_file);
    init->set_queue_impl("circular");
    init->set_session_name("example-client");
    client.call(init_req);
    std::cout << "Session initialized. Server has not loaded any jobs yet.\n";

    // 2. Append every job - the server has never seen any of this
    // data before now. Each AppendJobRequest adds the record and
    // immediately enqueues it for scheduling.
    std::vector<uint32_t> job_idxs;
    for (const auto &j : jobs) {
      ClientMessage append_req;
      auto *a = append_req.mutable_append_job();
      a->set_submit_time(j.submit_time);
      a->set_num_nodes(j.num_nodes);
      a->set_queue(j.queue);
      a->set_limit_time(j.limit_time);
      auto resp = client.call(append_req);
      job_idxs.push_back(resp.append_job().job_idx());
    }
    std::cout << "Appended " + std::to_string(jobs.size()) +
                     " jobs the server had never seen before.\n";

    // 3. Finish declares there will be no more arrivals. It drains all
    // submitted work, writes session-scoped reports, and resets only
    // this stream's simulation; dr_evt_server itself keeps running.
    ClientMessage finish_req;
    finish_req.mutable_finish_simulation();
    auto finish_resp = client.call(finish_req).finish_simulation();
    const auto &stats = finish_resp.statistics();

    std::cout << "\n=== Final Statistics ===\n"
              << "Jobs submitted:  " << stats.jobs_submitted() << "\n"
              << "Jobs completed:  " << stats.jobs_completed() << "\n"
              << "Current time:    " << stats.current_time() << "\n"
              << "Utilization:     " << (stats.utilization() * 100.0) << "%\n"
              << "Avg wait time:   " << stats.avg_wait_time() << "\n"
              << "Makespan:        " << stats.makespan() << "\n"
              << "Session ID:      " << finish_resp.session_id() << "\n"
              << "Statistics file: " << finish_resp.statistics_file() << "\n";

    // To reuse this same bidirectional stream, send another InitRequest
    // here and run its append/submit/finish sequence; call client.finish()
    // only when no further simulations will use the stream.
  } catch (const std::exception &e) {
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
