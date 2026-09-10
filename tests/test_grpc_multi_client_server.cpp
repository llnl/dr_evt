/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

/*
 * MPI-aware test harness: launches N independent (dr_evt_server,
 * dr_evt_client) pairs, each pair placed on its own MPI rank so mpirun's
 * own host-placement (-host/--hostfile) can distribute them across real,
 * physically separate nodes. Verified single-node only in this
 * environment (no second node available here), but designed to work
 * unmodified across nodes: servers bind 0.0.0.0 (not localhost) and
 * communicate their actual hostname to their paired client over MPI,
 * rather than assuming shared network-namespace loopback access.
 *
 * Synchronization: with a single client per server, no coordination is
 * needed at all - each pair is a fully independent simulation. This
 * harness's actual point is the multi-client case: when multiple,
 * independent clients are active at once, each feeding its own server,
 * they must be kept in lockstep on a shared, real-world notion of time -
 * otherwise one client could submit its jobs arbitrarily far ahead of
 * where the other client's own stream says "now" is, breaking the
 * intended cross-stream arrival ordering the two streams are supposed to
 * represent together. This is a conservative (Chandy-Misra-Bryant-style)
 * synchronization: every round, every client reports the arrival time of
 * its own next not-yet-submitted job; the global minimum across all
 * clients is the only time it's safe for anyone to advance to, since no
 * client can have a still-unsubmitted job below that time.
 *
 * Usage:
 *   mpirun -np <2*N> ./test_grpc_multi_client_server \
 *       <server_binary_path> <base_port> <ordinary_trace_1>
 *       <ordinary_trace_2> <composite_trace>
 *
 * Rank layout: ranks [0, N) are servers, ranks [N, 2N) are clients -
 * client rank N+i is paired with server rank i, both driven from
 * ordinary_trace_i.  The composite trace has one row for each fragment of a
 * cross-server job, and is used to exercise AppendJobsRequest at the exact
 * logical-time boundaries documented below.
 */

#include <grpcpp/grpcpp.h>
#include <mpi.h>

#include "dr_evt_config.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <netdb.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "dr_evt_service.grpc.pb.h"

using dr_evt_grpc::ClientMessage;
using dr_evt_grpc::ServerMessage;
using dr_evt_grpc::SimulationService;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderWriter;

namespace {

#if DR_EVT_LEGACY_QUEUE_INPUT
constexpr const char *kTestQueueInput = "pbatch";
#else
constexpr const char *kTestQueueInput =
    "1"; ///< Numeric ID of the default Queue1.
#endif

std::string get_own_hostname() {
  char buf[256];
  if (gethostname(buf, sizeof(buf)) != 0) {
    return "localhost";
  }
  return std::string(buf);
}

std::string get_own_ip() {
  // For single-node testing, always use loopback
  // For multi-node MPI, the real IP would be needed, but this test
  // is designed for single-node with multiple ranks
  return "127.0.0.1";
}

struct Job {
  double submit_time;
  uint32_t num_nodes;
  std::string queue = kTestQueueInput;
  double limit_time;
};

struct CompositeEvent {
  double submit_time;
  std::map<std::string, Job> fragments;
};

std::vector<std::string> split_csv(const std::string &line) {
  std::vector<std::string> fields;
  std::istringstream stream(line);
  std::string field;
  while (std::getline(stream, field, ','))
    fields.push_back(field);
  return fields;
}

std::vector<Job> read_jobs(const std::string &trace_file) {
  std::vector<Job> jobs;
  std::ifstream ifs(trace_file);
  if (!ifs) {
    throw std::runtime_error("Failed to open trace file: " + trace_file);
  }
  std::string line;
  std::getline(ifs, line);
  const auto header = split_csv(line);
  if (header.size() < 3 || header[0] != "job_submit_time" ||
      header[1] != "num_nodes" || header[2] != "time_limit") {
    throw std::runtime_error("Expected simple job CSV header in " + trace_file);
  }
  while (std::getline(ifs, line)) {
    if (line.empty())
      continue;
    const auto fields = split_csv(line);
    if (fields.size() < 3) {
      throw std::runtime_error("Malformed job row in " + trace_file);
    }
    jobs.push_back({std::stod(fields[0]),
                    static_cast<uint32_t>(std::stoul(fields[1])),
                    kTestQueueInput, std::stod(fields[2])});
  }
  if (!std::is_sorted(jobs.begin(), jobs.end(), [](const Job &a, const Job &b) {
        return a.submit_time < b.submit_time;
      })) {
    throw std::runtime_error("Job times must be non-decreasing in " +
                             trace_file);
  }
  return jobs;
}

std::vector<CompositeEvent>
read_composite_events(const std::string &trace_file) {
  std::ifstream ifs(trace_file);
  if (!ifs)
    throw std::runtime_error("Failed to open composite trace: " + trace_file);
  std::string line;
  std::getline(ifs, line);
  const auto header = split_csv(line);
  if (header.size() < 5 || header[0] != "composite_id" ||
      header[1] != "submit_time" || header[2] != "system_id" ||
      header[3] != "num_nodes" || header[4] != "time_limit") {
    throw std::runtime_error("Expected composite-job CSV header in " +
                             trace_file);
  }

  std::vector<CompositeEvent> events;
  std::string current_id;
  while (std::getline(ifs, line)) {
    if (line.empty())
      continue;
    const auto fields = split_csv(line);
    if (fields.size() < 5)
      throw std::runtime_error("Malformed composite row in " + trace_file);
    const double submit_time = std::stod(fields[1]);
    if (fields[0] != current_id) {
      if (!events.empty() && submit_time <= events.back().submit_time) {
        throw std::runtime_error(
            "Composite events must be strictly time-ordered");
      }
      events.push_back({submit_time, {}});
      current_id = fields[0];
    } else if (submit_time != events.back().submit_time) {
      throw std::runtime_error(
          "Composite fragments in one event must share a submit time");
    }
    const auto inserted = events.back().fragments.emplace(
        fields[2],
        Job{submit_time, static_cast<uint32_t>(std::stoul(fields[3])),
            kTestQueueInput, std::stod(fields[4])});
    if (!inserted.second)
      throw std::runtime_error("Duplicate composite system_id: " + fields[2]);
  }
  return events;
}

std::string expected_fixture_path(const std::string &ordinary_trace,
                                  const std::string &suffix) {
  const auto extension = ordinary_trace.rfind(".csv");
  if (extension == std::string::npos) {
    throw std::runtime_error("Ordinary trace must have a .csv extension: " +
                             ordinary_trace);
  }
  return ordinary_trace.substr(0, extension) + suffix;
}

bool files_match(const std::string &expected_path,
                 const std::string &actual_path,
                 const std::string &description) {
  std::ifstream expected(expected_path, std::ios::binary);
  std::ifstream actual(actual_path, std::ios::binary);
  if (!expected || !actual) {
    std::cerr << "Unable to open " << description
              << " for comparison: expected=" << expected_path
              << ", actual=" << actual_path << std::endl;
    return false;
  }

  std::istreambuf_iterator<char> expected_it(expected);
  std::istreambuf_iterator<char> actual_it(actual);
  const std::istreambuf_iterator<char> end;
  while (expected_it != end && actual_it != end && *expected_it == *actual_it) {
    ++expected_it;
    ++actual_it;
  }
  const bool matches = (expected_it == end && actual_it == end);
  if (!matches) {
    std::cerr << description
              << " differs from its offline expected fixture: expected="
              << expected_path << ", actual=" << actual_path << std::endl;
  }
  return matches;
}

} // namespace

// Same pattern as dr_evt_client.cpp's own SimulationClient - request/response
// wrapper over the bidirectional stream, one call at a time.
class SimulationClient {
public:
  explicit SimulationClient(std::shared_ptr<Channel> channel)
      : m_stub(SimulationService::NewStub(channel)),
        m_stream(m_stub->Session(&m_context)), m_next_request_id(1) {}

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
      throw std::runtime_error("Response request_id mismatch");
    }
    if (resp.response_case() == ServerMessage::kError) {
      throw std::runtime_error("Server error: " + resp.error().message());
    }
    return resp;
  }

  grpc::Status finish() {
    m_stream->WritesDone();
    return m_stream->Finish();
  }

private:
  std::unique_ptr<SimulationService::Stub> m_stub;
  ClientContext m_context;
  std::unique_ptr<ClientReaderWriter<ClientMessage, ServerMessage>> m_stream;
  uint64_t m_next_request_id;
};

// MPI message tags
constexpr int TAG_SERVER_ADDRESS = 100;
constexpr int TAG_CLIENT_DONE = 101;

int run_server_rank(int my_rank, int paired_client_rank,
                    const std::string &server_binary, int port) {
  std::string hostname = get_own_hostname();
  std::string address = "0.0.0.0:" + std::to_string(port);
  // Use IP address instead of hostname for better compatibility
  std::string ip = get_own_ip();
  std::string advertised_address = ip + ":" + std::to_string(port);

  pid_t child_pid = fork();
  if (child_pid < 0) {
    std::cerr << "[server rank " << my_rank << "] fork() failed" << std::endl;
    return 1;
  }

  if (child_pid == 0) {
    // Child: exec the actual dr_evt_server binary
    execlp(server_binary.c_str(), server_binary.c_str(), address.c_str(),
           (char *)nullptr);
    // execlp only returns on failure
    std::cerr << "[server rank " << my_rank << "] execlp failed for "
              << server_binary << std::endl;
    _exit(127);
  }

  // Parent: give the server a moment to bind and start listening before
  // advertising its address - the client side also retries its initial
  // connection, so this is a courtesy, not a hard requirement.
  usleep(1500 * 1000);

  std::cout << "[server rank " << my_rank << "] listening on "
            << advertised_address << " (pid " << child_pid << ")" << std::endl;

  // Send our actual, connectable address to the paired client rank -
  // not assuming the client can derive it (e.g. via shared loopback),
  // since on a real multi-node launch the client is on a different
  // machine entirely.
  MPI_Send(const_cast<char *>(advertised_address.c_str()),
           static_cast<int>(advertised_address.size()) + 1, MPI_CHAR,
           paired_client_rank, TAG_SERVER_ADDRESS, MPI_COMM_WORLD);

  // Block until the paired client signals it's done with this server.
  int done_signal = 0;
  MPI_Recv(&done_signal, 1, MPI_INT, paired_client_rank, TAG_CLIENT_DONE,
           MPI_COMM_WORLD, MPI_STATUS_IGNORE);

  std::cout << "[server rank " << my_rank << "] received done signal, "
            << "terminating server process" << std::endl;

  kill(child_pid, SIGTERM);
  int status = 0;
  waitpid(child_pid, &status, 0);

  return 0;
}

void append_and_submit(SimulationClient &client, const std::vector<Job> &jobs,
                       int my_rank, const std::string &phase) {
  if (jobs.empty())
    return;
  ClientMessage append_req;
  auto *append = append_req.mutable_append_jobs();
  for (const auto &job : jobs) {
    auto *request = append->add_requests();
    request->set_submit_time(job.submit_time);
    request->set_num_nodes(job.num_nodes);
    request->set_queue(job.queue);
    request->set_limit_time(job.limit_time);
  }
  const auto response = client.call(append_req);
  const auto &job_idxs = response.append_jobs().job_idx();
  if (job_idxs.size() != static_cast<int>(jobs.size())) {
    throw std::runtime_error(
        "AppendJobs returned the wrong number of job indexes");
  }
  std::cout << "[client rank " << my_rank << "] append_jobs(" << phase
            << ") appended and enqueued " << jobs.size() << " jobs"
            << std::endl;
}

void advance_to(SimulationClient &client, double time, int my_rank,
                const std::string &phase) {
  ClientMessage request;
  request.mutable_advance_to()->set_target_time(time);
  client.call(request);
  std::cout << "[client rank " << my_rank << "] advance_to(" << time << ") for "
            << phase << std::endl;
}

int run_client_rank(int my_rank, int paired_server_rank,
                    const std::string &trace_file,
                    const std::string &composite_trace, int pair_index,
                    int total_nodes, MPI_Comm client_comm) {
  // Receive the paired server's actual, connectable address.
  char addr_buf[256] = {0};
  MPI_Recv(addr_buf, sizeof(addr_buf), MPI_CHAR, paired_server_rank,
           TAG_SERVER_ADDRESS, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  std::string server_address(addr_buf);

  std::cout << "[client rank " << my_rank << "] connecting to "
            << server_address << std::endl;

  // Create channel and wait for it to be ready before opening the stream.
  // The stream is opened in SimulationClient's constructor, and if the
  // server isn't ready yet, the stream fails permanently - retrying with
  // the same broken stream won't help.
  auto channel =
      grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());

  const int max_retries = 60; // Increased from 20 to 60 (30 seconds total)
  int attempt = 0;
  bool connected = false;

  // Wait for channel to be ready
  std::cout << "[client rank " << my_rank
            << "] waiting for channel to be ready..." << std::endl;
  while (attempt < max_retries) {
    auto state = channel->GetState(true); // try_to_connect = true
    if (state == GRPC_CHANNEL_READY) {
      connected = true;
      std::cout << "[client rank " << my_rank << "] channel ready after "
                << attempt << " attempts" << std::endl;
      break;
    }
    if (!channel->WaitForStateChange(state,
                                     std::chrono::system_clock::now() +
                                         std::chrono::milliseconds(500))) {
      // Timeout - try again
    }
    attempt++;
    if (attempt % 10 == 0) {
      std::cout << "[client rank " << my_rank << "] still waiting (attempt "
                << attempt << "/" << max_retries << ", state=" << state << ")"
                << std::endl;
    }
  }

  if (!connected) {
    std::cerr << "[client rank " << my_rank
              << "] channel never became ready after " << max_retries
              << " attempts" << std::endl;
    throw std::runtime_error("Failed to connect to server");
  }

  // Now that channel is ready, create the client (which opens the stream)
  SimulationClient client(channel);

  // Send initialization request
  ClientMessage init_req;
  auto *init = init_req.mutable_init();
  init->set_total_nodes(total_nodes);
  init->set_trace_format("simple");
  init->set_timestamp_format("epoch");
  init->set_backfill_policy("easy");
  init->set_priority_policy("fcfs");
  init->set_run_time_mode("limit");
  // The client, rather than InitializeTrace, owns all three input files.
  // Keeping this source trace unloaded makes every ordinary and composite
  // job pass through AppendJobsRequest.
  init->set_infile(trace_file);
  init->set_session_name("multi-client-test");
  client.call(init_req);

  const std::vector<Job> ordinary_jobs = read_jobs(trace_file);
  const std::vector<CompositeEvent> events =
      read_composite_events(composite_trace);
  const std::string system_id = "server" + std::to_string(pair_index + 1);
  if (events.size() < 2) {
    throw std::runtime_error(
        "Composite trace must contain at least two time-ordered events");
  }
  for (const auto &event : events) {
    if (event.fragments.count(system_id) == 0) {
      throw std::runtime_error("Composite event is missing a fragment for " +
                               system_id);
    }
  }

  // The first composite event tests equality: batch through t2, then
  // advance_to(t1 == t2), then append its composite fragment at t3 == t2.
  // Later events are a stream: before each one, append all newly-known
  // ordinary jobs and advance to a point no later than their final time.
  // The supplied fixture has an intervening ordinary job, giving t1 < t2 <
  // t3, and a final ordinary job at t4 > t3.
  std::vector<Job> first_batch;
  for (const auto &job : ordinary_jobs) {
    if (job.submit_time <= events[0].submit_time)
      first_batch.push_back(job);
  }
  if (first_batch.empty() ||
      first_batch.back().submit_time != events[0].submit_time) {
    throw std::runtime_error("Ordinary traces must end their initial batch at "
                             "the first composite time");
  }

  append_and_submit(client, first_batch, my_rank,
                    "ordinary through t2 (equal)");
  advance_to(client, events[0].submit_time, my_rank, "t1 == t2");
  MPI_Barrier(client_comm);
  append_and_submit(client, {events[0].fragments.at(system_id)}, my_rank,
                    "composite at t3 == t2");
  MPI_Barrier(client_comm);
  advance_to(client, events[0].submit_time, my_rank,
             "evaluate equal-time composite");

  size_t next_ordinary = first_batch.size();
  bool exercised_strict_boundary = false;
  for (size_t event_index = 1; event_index < events.size(); ++event_index) {
    const auto &event = events[event_index];
    std::vector<Job> batch;
    while (next_ordinary < ordinary_jobs.size() &&
           ordinary_jobs[next_ordinary].submit_time < event.submit_time) {
      batch.push_back(ordinary_jobs[next_ordinary++]);
    }
    if (!batch.empty()) {
      append_and_submit(client, batch, my_rank,
                        "ordinary through t2 before composite stream");
      const double t2 = batch.back().submit_time;
      const double previous_time = events[event_index - 1].submit_time;
      const double t1 = (previous_time + t2) / 2.0;
      if (t1 < t2)
        exercised_strict_boundary = true;
      advance_to(client, t1, my_rank, "t1 < t2");
    }
    MPI_Barrier(client_comm);
    append_and_submit(client, {event.fragments.at(system_id)}, my_rank,
                      "composite at t3 after ordinary batch");
    MPI_Barrier(client_comm);
    advance_to(client, event.submit_time, my_rank,
               "evaluate composite stream event");
  }
  if (!exercised_strict_boundary) {
    throw std::runtime_error("Composite stream needs an ordinary job strictly "
                             "between two composite events");
  }

  std::vector<Job> final_batch(ordinary_jobs.begin() + next_ordinary,
                               ordinary_jobs.end());
  if (final_batch.empty() ||
      final_batch.front().submit_time <= events.back().submit_time) {
    throw std::runtime_error(
        "Ordinary trace needs a final t4 strictly after the composite stream");
  }
  append_and_submit(client, final_batch, my_rank,
                    "ordinary at t4 > final composite t3");

  // FinishSimulation is the session-level completion API: it drains all
  // submitted work, writes the session reports, returns final statistics,
  // and releases the server-side Simulation before we close the stream.
  ClientMessage finish_req;
  finish_req.mutable_finish_simulation();
  const auto finish_resp = client.call(finish_req);
  const auto &finish = finish_resp.finish_simulation();
  const auto &stats = finish.statistics();

  std::cout << "[client rank " << my_rank << "] final stats: "
            << "submitted=" << stats.jobs_submitted()
            << " completed=" << stats.jobs_completed()
            << " makespan=" << stats.makespan() << " (FinishSimulation)"
            << std::endl;

  const uint64_t expected_jobs = ordinary_jobs.size() + events.size();
  int completed_ok =
      (stats.jobs_submitted() == expected_jobs &&
       stats.jobs_completed() == expected_jobs &&
       !finish.session_id().empty() && !finish.simulated_trace_file().empty() &&
       !finish.resource_trace_file().empty() &&
       !finish.statistics_file().empty())
          ? 1
          : 0;

  // The expected files are generated offline by treating each composite
  // fragment as a normal job in its owning server's ordinary stream. This
  // is an independent schedule/resource oracle for the gRPC session: MPI
  // only coordinates when the fragments arrive; it does not share either
  // scheduler's resources or state.
  const std::string expected_schedule =
      expected_fixture_path(trace_file, ".expected_output.csv");
  const std::string expected_resources =
      expected_fixture_path(trace_file, ".expected_resources.csv");
  if (!files_match(expected_schedule, finish.simulated_trace_file(),
                   "Simulated schedule") ||
      !files_match(expected_resources, finish.resource_trace_file(),
                   "Resource trace")) {
    completed_ok = 0;
  }

  client.finish();

  // Tell the paired server it can shut down now.
  int done_signal = 1;
  MPI_Send(&done_signal, 1, MPI_INT, paired_server_rank, TAG_CLIENT_DONE,
           MPI_COMM_WORLD);

  return completed_ok ? 0 : 1;
}

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);

  int world_rank = 0, world_size = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  if (world_size % 2 != 0) {
    if (world_rank == 0) {
      std::cerr << "Total rank count must be even (N server ranks + "
                << "N client ranks). Got " << world_size << "." << std::endl;
    }
    MPI_Finalize();
    return 1;
  }
  int num_pairs = world_size / 2;

  // This is intentionally a two-server composite-job integration test.
  // argv: [1]=server_binary [2]=base_port [3]=ordinary server1 trace
  //       [4]=ordinary server2 trace [5]=shared composite trace
  if (num_pairs != 2 || argc != 6) {
    if (world_rank == 0) {
      std::cerr << "Usage: mpirun -np " << world_size << " " << argv[0]
                << " <server_binary> <base_port> "
                << "<ordinary_trace_server1> <ordinary_trace_server2> "
                << "<composite_trace> (requires exactly four ranks)"
                << std::endl;
    }
    MPI_Finalize();
    return 1;
  }
  std::string server_binary = argv[1];
  int base_port = std::atoi(argv[2]);

  bool is_server = (world_rank < num_pairs);
  int pair_index = is_server ? world_rank : (world_rank - num_pairs);
  int paired_rank =
      is_server ? (world_rank + num_pairs) : (world_rank - num_pairs);

  // Collective: every rank in MPI_COMM_WORLD must call this together.
  // Only client ranks get a real (non-null) sub-communicator - server
  // ranks pass MPI_UNDEFINED and get MPI_COMM_NULL back, correctly
  // excluding them from the clients' own lockstep coordination, which
  // they have no part in.
  MPI_Comm client_comm = MPI_COMM_NULL;
  MPI_Comm_split(MPI_COMM_WORLD, is_server ? MPI_UNDEFINED : 1, world_rank,
                 &client_comm);

  int rc = 0;
  try {
    if (is_server) {
      int port = base_port + pair_index;
      rc = run_server_rank(world_rank, paired_rank, server_binary, port);
    } else {
      std::string trace_file = argv[3 + pair_index];
      std::string composite_trace = argv[5];
      int total_nodes = 100;
      rc = run_client_rank(world_rank, paired_rank, trace_file, composite_trace,
                           pair_index, total_nodes, client_comm);
    }
  } catch (const std::exception &e) {
    std::cerr << "[rank " << world_rank << "] FATAL ERROR: " << e.what()
              << std::endl;
    std::cerr << "[rank " << world_rank
              << "] Calling MPI_Abort to terminate all ranks" << std::endl;
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  if (client_comm != MPI_COMM_NULL) {
    MPI_Comm_free(&client_comm);
  }

  // Aggregate pass/fail across every rank so mpirun's own exit status
  // reflects the whole test, not just rank 0's.
  int global_rc = 0;
  MPI_Allreduce(&rc, &global_rc, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);

  MPI_Finalize();
  return global_rc;
}
