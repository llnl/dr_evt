/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

// MPI-aware test harness: launches N independent (dr_evt_server,
// dr_evt_client) pairs, each pair placed on its own MPI rank so mpirun's
// own host-placement (-host/--hostfile) can distribute them across real,
// physically separate nodes. Verified single-node only in this
// environment (no second node available here), but designed to work
// unmodified across nodes: servers bind 0.0.0.0 (not localhost) and
// communicate their actual hostname to their paired client over MPI,
// rather than assuming shared network-namespace loopback access.
//
// Synchronization: with a single client per server, no coordination is
// needed at all - each pair is a fully independent simulation. This
// harness's actual point is the multi-client case: when multiple,
// independent clients are active at once, each feeding its own server,
// they must be kept in lockstep on a shared, real-world notion of time -
// otherwise one client could submit its jobs arbitrarily far ahead of
// where the other client's own stream says "now" is, breaking the
// intended cross-stream arrival ordering the two streams are supposed to
// represent together. This is a conservative (Chandy-Misra-Bryant-style)
// synchronization: every round, every client reports the arrival time of
// its own next not-yet-submitted job; the global minimum across all
// clients is the only time it's safe for anyone to advance to, since no
// client can have a still-unsubmitted job below that time.
//
// Usage:
//   mpirun -np <2*N> ./test_grpc_multi_client_server \
//       <server_binary_path> <base_port> <trace_file_1> [<trace_file_2> ...]
//
// Rank layout: ranks [0, N) are servers, ranks [N, 2N) are clients -
// client rank N+i is paired with server rank i, both driven from
// trace_file_i.

#include <mpi.h>
#include <grpcpp/grpcpp.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <limits>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#include "dr_evt_service.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderWriter;
using dr_evt_grpc::SimulationService;
using dr_evt_grpc::ClientMessage;
using dr_evt_grpc::ServerMessage;

namespace {

std::string get_own_hostname()
{
    char buf[256];
    if (gethostname(buf, sizeof(buf)) != 0) {
        return "localhost";
    }
    return std::string(buf);
}

// Reads job_submit_time (column 0) from a simple-format trace CSV. Same
// approach as dr_evt_client.cpp's own trace reading - the client, not the
// server, is authoritative on arrival timing for the streaming API (see
// dr_evt_client.cpp's own comment on this).
std::vector<double> read_submit_times(const std::string& trace_file)
{
    std::vector<double> times;
    std::ifstream ifs(trace_file);
    if (!ifs) {
        throw std::runtime_error("Failed to open trace file: " + trace_file);
    }
    std::string line;
    std::getline(ifs, line);  // header
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string first_field;
        std::getline(iss, first_field, ',');
        times.push_back(std::stod(first_field));
    }
    return times;
}

} // namespace

// Same pattern as dr_evt_client.cpp's own SimulationClient - request/response
// wrapper over the bidirectional stream, one call at a time.
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
        if (resp.request_id() != id) {
            throw std::runtime_error("Response request_id mismatch");
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

// MPI message tags
constexpr int TAG_SERVER_ADDRESS = 100;
constexpr int TAG_CLIENT_DONE = 101;

int run_server_rank(int my_rank, int paired_client_rank,
                     const std::string& server_binary, int port)
{
    std::string hostname = get_own_hostname();
    std::string address = "0.0.0.0:" + std::to_string(port);
    std::string advertised_address = hostname + ":" + std::to_string(port);

    pid_t child_pid = fork();
    if (child_pid < 0) {
        std::cerr << "[server rank " << my_rank << "] fork() failed" << std::endl;
        return 1;
    }

    if (child_pid == 0) {
        // Child: exec the actual dr_evt_server binary
        execlp(server_binary.c_str(), server_binary.c_str(), address.c_str(),
               (char*)nullptr);
        // execlp only returns on failure
        std::cerr << "[server rank " << my_rank << "] execlp failed for "
                  << server_binary << std::endl;
        _exit(127);
    }

    // Parent: give the server a moment to bind and start listening before
    // advertising its address - the client side also retries its initial
    // connection, so this is a courtesy, not a hard requirement.
    usleep(500 * 1000);

    std::cout << "[server rank " << my_rank << "] listening on "
              << advertised_address << " (pid " << child_pid << ")" << std::endl;

    // Send our actual, connectable address to the paired client rank -
    // not assuming the client can derive it (e.g. via shared loopback),
    // since on a real multi-node launch the client is on a different
    // machine entirely.
    MPI_Send(advertised_address.c_str(), static_cast<int>(advertised_address.size()) + 1,
              MPI_CHAR, paired_client_rank, TAG_SERVER_ADDRESS, MPI_COMM_WORLD);

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

int run_client_rank(int my_rank, int paired_server_rank,
                     const std::string& trace_file, int total_nodes,
                     MPI_Comm client_comm)
{
    // Receive the paired server's actual, connectable address.
    char addr_buf[256] = {0};
    MPI_Recv(addr_buf, sizeof(addr_buf), MPI_CHAR, paired_server_rank,
              TAG_SERVER_ADDRESS, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    std::string server_address(addr_buf);

    std::cout << "[client rank " << my_rank << "] connecting to "
              << server_address << std::endl;

    SimulationClient client(
        grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));

    // Retry the initial call briefly - the server may still be starting.
    const int max_retries = 20;
    int attempt = 0;
    while (true) {
        try {
            ClientMessage init_req;
            auto* init = init_req.mutable_init();
            init->set_total_nodes(total_nodes);
            init->set_trace_format("simple");
            init->set_timestamp_format("epoch");
            init->set_backfill_policy("easy");
            init->set_priority_policy("fcfs");
            init->set_duration_mode("limit");
            init->set_run_time_mode("exact");
            init->set_infile(trace_file);
            client.call(init_req);
            break;
        } catch (const std::exception& e) {
            if (++attempt >= max_retries) {
                std::cerr << "[client rank " << my_rank << "] failed to "
                          << "initialize session after " << max_retries
                          << " attempts: " << e.what() << std::endl;
                throw;
            }
            usleep(250 * 1000);
        }
    }

    ClientMessage trace_req;
    trace_req.mutable_initialize_trace()->set_max_jobs(0);
    auto trace_resp = client.call(trace_req);
    uint64_t num_jobs = trace_resp.initialize_trace().num_jobs_loaded();

    std::vector<double> submit_times = read_submit_times(trace_file);
    if (submit_times.size() != num_jobs) {
        throw std::runtime_error("Server loaded " + std::to_string(num_jobs) +
            " jobs but client parsed " + std::to_string(submit_times.size()));
    }

    std::cout << "[client rank " << my_rank << "] loaded " << num_jobs
              << " jobs, entering lockstep loop" << std::endl;

    // Lockstep loop: every round, every client (across client_comm)
    // reports the arrival time of its own next not-yet-submitted job (or
    // +infinity once exhausted). The global minimum across all clients is
    // the only time it's safe for anyone to advance to - no client can
    // still have an unsubmitted job below that time once this round's
    // Allreduce completes. Whoever's own next job matches the global
    // minimum submits it (there may be ties - simultaneous arrivals
    // across streams - each such client submits its own job this round).
    size_t next_idx = 0;
    const double INF = std::numeric_limits<double>::max();

    while (true) {
        double my_next_time = (next_idx < submit_times.size())
                                 ? submit_times[next_idx] : INF;
        double global_min_time = INF;
        MPI_Allreduce(&my_next_time, &global_min_time, 1, MPI_DOUBLE,
                       MPI_MIN, client_comm);

        if (global_min_time == INF) {
            break;  // every client has exhausted its jobs
        }

        if (my_next_time == global_min_time) {
            ClientMessage submit_req;
            auto* submit = submit_req.mutable_submit_job();
            submit->set_job_idx(static_cast<uint32_t>(next_idx));
            submit->set_submit_time(my_next_time);
            client.call(submit_req);
            ++next_idx;
        }

        ClientMessage advance_req;
        advance_req.mutable_advance_to()->set_target_time(global_min_time);
        client.call(advance_req);
    }

    // Drain any jobs still running past the last submission.
    ClientMessage final_advance;
    final_advance.mutable_advance_to()->set_target_time(1e12);
    client.call(final_advance);

    ClientMessage stats_req;
    stats_req.mutable_get_statistics();
    auto stats_resp = client.call(stats_req);
    const auto& stats = stats_resp.get_statistics();

    std::cout << "[client rank " << my_rank << "] final stats: "
              << "submitted=" << stats.jobs_submitted()
              << " completed=" << stats.jobs_completed()
              << " makespan=" << stats.current_time() << std::endl;

    int completed_ok = (stats.jobs_completed() == num_jobs) ? 1 : 0;

    client.finish();

    // Tell the paired server it can shut down now.
    int done_signal = 1;
    MPI_Send(&done_signal, 1, MPI_INT, paired_server_rank, TAG_CLIENT_DONE,
              MPI_COMM_WORLD);

    return completed_ok ? 0 : 1;
}

int main(int argc, char** argv)
{
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

    // argv: [1]=server_binary [2]=base_port [3..]=trace files, one per pair
    if (argc < 3 + num_pairs) {
        if (world_rank == 0) {
            std::cerr << "Usage: mpirun -np " << world_size << " " << argv[0]
                      << " <server_binary> <base_port> "
                      << "<trace_file_1> ... <trace_file_" << num_pairs << ">"
                      << std::endl;
        }
        MPI_Finalize();
        return 1;
    }
    std::string server_binary = argv[1];
    int base_port = std::atoi(argv[2]);

    bool is_server = (world_rank < num_pairs);
    int pair_index = is_server ? world_rank : (world_rank - num_pairs);
    int paired_rank = is_server ? (world_rank + num_pairs) : (world_rank - num_pairs);

    // Collective: every rank in MPI_COMM_WORLD must call this together.
    // Only client ranks get a real (non-null) sub-communicator - server
    // ranks pass MPI_UNDEFINED and get MPI_COMM_NULL back, correctly
    // excluding them from the clients' own lockstep coordination, which
    // they have no part in.
    MPI_Comm client_comm = MPI_COMM_NULL;
    MPI_Comm_split(MPI_COMM_WORLD, is_server ? MPI_UNDEFINED : 1,
                    world_rank, &client_comm);

    int rc = 0;
    try {
        if (is_server) {
            int port = base_port + pair_index;
            rc = run_server_rank(world_rank, paired_rank, server_binary, port);
        } else {
            std::string trace_file = argv[3 + pair_index];
            int total_nodes = 100;
            rc = run_client_rank(world_rank, paired_rank, trace_file,
                                   total_nodes, client_comm);
        }
    } catch (const std::exception& e) {
        std::cerr << "[rank " << world_rank << "] error: " << e.what() << std::endl;
        rc = 1;
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
