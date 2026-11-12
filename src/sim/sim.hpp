/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SIM_HPP
#define DR_EVT_SIM_SIM_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#include <algorithm>
#include <cmath>
#include <deque>
#include <iostream>
#include <limits>
#include <map>
#include <memory> // unique_ptr
#include <optional>
#include <random>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.hpp"
#include "params/sim_params.hpp"
#include "sim/capacity_schedule.hpp"
#include "sim/scheduler_base.hpp"
#include "sim/scheduler_fcfs_easy_custom.hpp"
#include "trace/dr_event.hpp"
#include "trace/trace.hpp"
#include "utils/rngen.hpp"

namespace dr_evt {

// Forward declarations
template <size_t BlockSize> class BlockWaitQueue;
class SchedulerBase;

/** \addtogroup dr_evt_sim
 *  @{ */

/**
 * @brief Orchestrate scheduling and accounting through discrete-event
 * simulation.
 * @tparam TraceType Trace implementation owning job records and event
 * accounting.
 * @details Configuration, trace, and scheduler belong to this simulation.
 * Diagnostic selection is fixed at construction; counters belong to its trace.
 */
template <typename TraceType> class BasicSimulation {
protected:
  /// Simulation parameters. Stored by value so the simulation owns its
  /// configuration and the caller's Sim_Params may be a temporary or go out
  /// of scope after construction.
  Sim_Params m_params;

  /// Job trace data
  TraceType m_trace;

  /// Job scheduler (polymorphic - FCFS/SJF/LJF)
  std::unique_ptr<SchedulerBase> m_scheduler;

  /// Non-owning pointer set only by the Custom-FCFS constructor.
  FCFSEasyCustomScheduler *m_custom_scheduler;

  /// Event queue (submit, start, end events)
  event_q_t m_event_queue;

  /// Current simulation time
  sim_time_t m_current_time;
  /// Next periodic backfill evaluation; infinity when periodic mode is idle.
  sim_time_t m_next_backfill_evaluation;
  /// True when a state event or job start requires another backfill tick.
  /// Starts expose new candidates in the bounded prefix without resetting
  /// the current timestamp's examination budget.
  bool m_periodic_backfill_dirty;

  /// Fixed physical limit used only to reject jobs that can never fit.
  num_nodes_t m_job_rejection_capacity;

  /// Ordered external machine-capacity changes and the active cursor.
  std::vector<Capacity_Change> m_capacity_changes;
  size_t m_next_capacity_change;
  num_nodes_t m_current_capacity;
  /// Integral of effective capacity over the current accounting horizon.
  /// Without a schedule, effective capacity remains m_params.m_total_nodes.
  tdiff_t m_capacity_area;
  /// Last timestamp incorporated into m_capacity_area.
  sim_time_t m_capacity_area_time;

  /// Counters
  num_jobs_t m_jobs_completed;
  num_jobs_t m_jobs_submitted; ///< Jobs accepted into the wait queue.
  num_jobs_t m_jobs_started;   ///< Jobs that began execution.
  /// Latest submit time accepted so far. The wait queues release
  /// arrivals in insertion order, so a later append with an earlier
  /// submit time would be held until this time; such appends are
  /// rejected instead.
  sim_time_t m_latest_submit_time;
  /// Configuration recorded in the checkpoint most recently restored.
  std::optional<Sim_Params> m_restored_params;
  /// Next progressive input file; advanced once its jobs are fully admitted.
  size_t m_next_progressive_file;
  /// True after load_checkpoint() restores runnable state.
  bool m_checkpoint_loaded;
  /// Completion count written by the last automatic checkpoint.
  num_jobs_t m_last_automatic_checkpoint_jobs;
  /// Whether this run has written an automatic checkpoint.
  bool m_has_automatic_checkpoint;
  /// Simulation time written by the last automatic checkpoint.
  sim_time_t m_last_automatic_checkpoint_time;
  /// Historical jobs still running during the temporary warm-start stage.
  size_t m_pre_start_jobs;
  /// Post-boundary node-seconds contributed by suppressed historical jobs.
  tdiff_t m_warm_resource_area;
  /// Latest departure among historical jobs active after the boundary.
  sim_time_t m_warm_resource_end;
  /// Ended jobs still held in m_running_jobs until their node release.
  size_t m_jobs_releasing;

  /// Serializable random-number engine used for duration sampling.
  RNGen<> m_rng;

  // NOTE: Wait queue now owned by scheduler (m_scheduler maintains internal
  // queue)

  /// Running jobs for streaming mode (job_idx -> start_time)
  running_jobs_t m_running_jobs;

  // Output statistics only; the scheduler never consults these values.
  /// Sum of the waiting-job counts observed by arriving jobs.
  size_t m_queue_length_sum;
  size_t m_queue_length_samples; ///< Number of arriving-job observations.
  size_t m_queue_length_peak;    ///< Largest observed waiting-queue length.

  /// Accepted arrivals not yet observed at their simulation timestamps.
  std::map<sim_time_t, size_t> m_pending_queue_arrivals;

  // Compact, permanent query records for jobs created by append_job(s).
  // Trace records may be reclaimed after completion, so their scheduling
  // timestamps cannot by themselves satisfy the public status API.
  struct Appended_Job_Query_Record {
    sim_time_t submit_time = 0.0;
    tdiff_t limit_time = 0.0;
    num_nodes_t num_nodes = 0;
    sim_time_t start_time = 0.0;
    sim_time_t end_time = 0.0;
    bool tracked = false;
    bool scheduled = false;
    bool rejected = false;
  };
  job_no_t m_first_appended_job_idx = std::numeric_limits<job_no_t>::max();
  std::vector<Appended_Job_Query_Record> m_appended_job_query_records;

public:
  /**
   * @brief Construct a simulation from immutable configuration.
   * @param[in] params Configuration transferred into this simulation.
   * @details Selects resource observation from resource_model_diagnostics once
   * at construction. The trace owns counters borrowed by an enabled scheduler.
   * @throws std::invalid_argument if scheduler configuration is invalid.
   * @throws std::runtime_error if the input trace cannot be initialized.
   */
  BasicSimulation(Sim_Params params);

  /**
   * @brief Construct a simulation with experimental external backfill choice.
   * @param[in] params Immutable simulation configuration.
   * @param[in] cost_function Callback that computes a job's cost at insertion.
   * @param[in] selector Callback that chooses one of the reported candidates.
   * @details Configuration and callbacks are owned by the simulation. Resource
   * diagnostics are selected once and observe trace event accounting; this
   * custom scheduler does not use the CapacityModel fit/reservation operations.
   * @throws std::invalid_argument if resource_model is not capacity.
   * @throws std::runtime_error if the input trace cannot be initialized.
   */
  BasicSimulation(Sim_Params params, job_cost_function_t cost_function,
                  backfill_selector_t selector);

  /**
   * @brief Construct a simulation owning a configured Custom-FCFS scheduler.
   * @param[in] params Configuration transferred into this simulation.
   * @param[in] scheduler Non-null scheduler whose ownership is transferred.
   * @details Diagnostics are selected once from params and observe the trace's
   * event accounting. The scheduler is destroyed before the trace counter
   * owner.
   * @throws std::invalid_argument if scheduler is null or resource_model is not
   * capacity.
   * @throws std::runtime_error if the input trace cannot be initialized.
   */
  BasicSimulation(Sim_Params params,
                  std::unique_ptr<FCFSEasyCustomScheduler> scheduler);

  /** @brief Borrow current exact allocations for placement inspection.
   * @return Placement-only running owners, valid until the next simulation
   * mutation; null when capacity accounting is selected. */
  const running_jobs_t::PlacementState *placement_state() const {
    return m_running_jobs.placement();
  }

  /** @brief Borrow the latest exact reservation-reporting snapshot.
   * @return Intervals valid until the next scheduling call or destruction. */
  std::span<const PlacementInterval> placement_reservations() const {
    return m_scheduler->placement_reservations();
  }

  /**
   * @brief Run a complete batch simulation for the configured trace.
   * @details
   * Loads and prepares the input trace when necessary and submits its jobs.
   * With max_time configured, processes events through that inclusive
   * boundary; otherwise drains the event queue. For externally fed work, use
   * append_job() or append_jobs() followed by advance_to() instead.
   * @throws std::invalid_argument for replay input in placement mode; replay
   * records do not carry exact resource identities.
   */
  void run();

  /**
   * @brief Write a human-readable statistics report.
   * @param[in,out] os Destination stream receiving the report.
   * @details Includes resource operation counters when enabled at construction.
   * Reading the counters neither advances time nor performs planning.
   */
  void print_stats(std::ostream &os) const;

  /** @brief Snapshot this simulation's resource operation counters.
   * @return Counters since construction or last successful checkpoint load;
   * std::nullopt when diagnostics were disabled at construction.
   * @details Reading counters does not advance time or perform planning. */
  std::optional<ResourceDiagnostics> get_resource_diagnostics() const {
    const auto *diagnostics = m_trace.resource_diagnostics();
    if (diagnostics == nullptr) {
      return std::nullopt;
    }
    return *diagnostics;
  }

  /**
   * @brief Write the simulated job trace to the configured output file.
   * @details
   * The destination is Sim_Params::get_outfile(). This does not advance
   * simulation time or otherwise change scheduling state.
   */
  void write_simulated_trace();

  /**
   * @brief Write and reclaim the completed front prefix at current time.
   * @details This is an explicit streaming checkpoint. It does not close the
   * schedule output, and it never reclaims an unfinished or pending record.
   */
  void flush_completed_jobs();

#if defined(DR_EVT_HAS_SER20)
  /**
   * @brief Save an exact continuation checkpoint to a binary stream.
   * @details Standard and Pcon traces are supported. Callback-based Custom FCFS
   * is supported when restart constructs the destination with equivalent
   * callbacks; callback objects themselves are not serialized. The checkpoint
   * is intended for the same DR_EVT build and matching Sim_Params. Buffered
   * output is flushed before the snapshot is written. Output files are not
   * included in the checkpoint; their committed byte boundaries are.
   * @param[in,out] output Binary destination stream.
   */
  void save_checkpoint(std::ostream &output);

  /**
   * @brief Save an exact continuation checkpoint to a file.
   * @param[in] filename Destination path, replaced after state is serialized.
   */
  void save_checkpoint(const std::string &filename);

  /**
   * @brief Restore a checkpoint into this simulation.
   * @details This replaces any current state. Configuration, scheduler kind,
   * and output paths must match those used to create the checkpoint. A Custom
   * FCFS destination must be constructed with equivalent callbacks. Existing
   * file outputs are archived as pre-restart segments, and fresh output
   * segments are opened at the configured paths. Diagnostic counters restart
   * at zero after a successful load; the destination's construction-time
   * observer selection is retained and may differ from the recorded setting.
   * @param[in,out] input Binary checkpoint source stream.
   */
  void load_checkpoint(std::istream &input);

  /**
   * @brief Restore an exact continuation checkpoint from a file.
   * @param[in] filename Existing checkpoint path.
   * @details Uses the stream overload's restoration semantics, including
   * resetting counters while retaining construction-time observer selection.
   */
  void load_checkpoint(const std::string &filename);
#endif

  /**
   * @brief Write resource-allocation history to a CSV file.
   * @param[in] filename Destination CSV path.
   */
  void write_resource_trace(const std::string &filename);

  /**
   * One job's data for append_jobs() - the same fields append_job()
   * takes individually (submit_time here using the same external,
   * pre-epoch-conversion sim_time_t append_job() takes, not Trace's
   * internal epoch_t), grouped so several new jobs can be passed in a
   * single call.
   */
  struct Job_Append_Request {
    sim_time_t submit_time; ///< Requested arrival time.
    num_nodes_t num_nodes;  ///< Requested node count.
    std::string queue;      ///< Queue ID, or a name in legacy-input builds.
    tdiff_t limit_time;     ///< Requested time limit.
    std::optional<tdiff_t> actual_run_time =
        std::nullopt; ///< Known execution time, if any.
  };

  /**
   * @brief Add a new job record and enqueue it for scheduling.
   *
   * Append a genuinely new job - the streaming counterpart to
   * load_data(), for a job the trace has never seen before (as
   * opposed to the internal submit_job() helper, which enqueues a job
   * already present in m_data). Adds the job to the store and immediately
   * enqueues it for scheduling.
   *
   * @param[in] submit_time Arrival time, which must be >= current time and
   *        >= the submit time of every job appended before it.
   * @param[in] num_nodes Number of nodes the job requests.
   * @param[in] queue Numeric queue ID by default (for example, "1" for the
   * default Queue1), or a queue name such as "pbatch" when built with
   * `DR_EVT_LEGACY_QUEUE_INPUT`.
   * @param[in] limit_time User-estimated time limit in seconds.
   * @param[in] actual_run_time Known execution time, or empty to use
   *        limit_time as the streaming execution duration.
   * @return New Trace job identifier as job_no_t.
   * @throws std::invalid_argument if limit_time is not a positive,
   *         representable whole number of seconds, if actual_run_time is
   *         non-positive, non-finite, or greater than limit_time, or if
   *         submit_time is earlier than an already appended job's.
   * @see submit_job()
   * @see SchedulerBase::insert_job()
   */
  job_no_t append_job(sim_time_t submit_time, num_nodes_t num_nodes,
                      const std::string &queue, tdiff_t limit_time,
                      std::optional<tdiff_t> actual_run_time = std::nullopt);

  /**
   * @brief Add several new job records and enqueue all of them.
   *
   * Append several genuinely new jobs in one call - the batch
   * counterpart to append_job(), for the same never-seen-before case.
   * Validates every request's submit_time >= current_time before any
   * of them are appended (same precondition append_job() enforces per
   * job) - combined with Trace::append_jobs()'s own validation
   * (ordering, capacity for the whole batch), this call is fully
   * all-or-nothing: nothing is appended unless the whole batch can be.
   * See Trace::append_jobs()'s own doc comment for the rest.
   *
   * @param[in] requests New jobs' data in submit_time order (see
   *        Trace::append_jobs() for why - this function forwards
   *        requests as-is, so pass them there already sorted).
   * @return New Trace job identifiers in the same order as requests.
   * @throws std::invalid_argument if any limit_time is not a positive,
   *         representable whole number of seconds, or if an actual_run_time
   *         is invalid or exceeds its limit_time.
   * @see append_job()
   * @see submit_job()
   */
  std::vector<job_no_t>
  append_jobs(const std::vector<Job_Append_Request> &requests);

  /** Lifecycle state returned by get_job_statuses(). */
  enum class Job_State { PENDING, RUNNING, COMPLETED, REJECTED };

  /** Source of a pending job's reported start time. */
  enum class Start_Time_Kind { NONE, RESERVED, ESTIMATED };

  /** Point-in-time status of one job created by append_job(s). */
  struct Job_Status {
    job_no_t job_idx;
    Job_State state;
    std::optional<sim_time_t> start_time;
    std::optional<sim_time_t> end_time;
    /**
     * @brief Reservation-derived or estimated start while the job is pending.
     *
     * Inspect start_time_kind to distinguish a protected scheduler reservation
     * from a non-binding estimate. Every waiting job can be considered,
     * including jobs beyond a conservative/hybrid backfill queue-depth limit
     * and jobs whose potential reservation is beyond the planning horizon; a
     * job with no feasible placement has no forecast. A later scheduler pass,
     * arrival, capacity change, or backfill decision can change an estimate.
     */
    std::optional<sim_time_t> expected_start_time;
    Start_Time_Kind start_time_kind = Start_Time_Kind::NONE;
    std::vector<num_nodes_t> node_ids{}; ///< Exact live or reserved IDs in
                                         ///< placement mode; empty otherwise.
  };

  /** @brief Resource-area approximation for one waiting job. */
  struct Queue_Drain_Start_Estimate {
    job_no_t job_idx;
    sim_time_t expected_start_time;
    Start_Time_Kind start_time_kind;
  };

  /** A pending job's prediction retained from a live scheduling pass. */
  struct Estimated_Job_Start {
    job_no_t job_idx;
    std::optional<sim_time_t> expected_start_time;
    Start_Time_Kind start_time_kind = Start_Time_Kind::NONE;
  };

  /** Latest live planning timestamp and currently eligible pending jobs. */
  struct Estimated_Job_Starts {
    std::optional<sim_time_t> estimated_at;
    std::vector<Estimated_Job_Start> jobs;
  };

  /**
   * @brief Read cached live-planner predictions, like Slurm squeue --start.
   * @details Does not run a forecast or advance simulation. Only projections
   * already computed by the scheduler are returned; unexamined jobs have no
   * time. Out-of-horizon estimates can be present without protection. Jobs
   * are sorted by expected start, with unknown starts last in queue order.
   * Future arrivals and started jobs are excluded. estimated_at identifies the
   * last planning pass, which may precede current_time with periodic backfill.
   * A restored checkpoint has no cached plan until scheduling runs again.
   * Query cost is O(N + P + K log K), for N waiting-queue entries, P cached
   * projections, and K eligible known starts. Unknowns retain queue order
   * without sorting. No prediction fields are added to waiting-queue entries.
   */
  Estimated_Job_Starts get_start_times_by_est_caches() const;

  /** Completion state of one synchronous exact-lookahead request. */
  enum class Exact_Start_Outcome {
    STARTED,
    REJECTED,
    CAPACITY_UNREACHABLE,
  };

  /** Exact start returned by an output-free scheduler replay. */
  struct Exact_Start_Result {
    job_no_t job_idx;
    Exact_Start_Outcome outcome;
    std::optional<sim_time_t> start_time;
  };

  /**
   * @brief Query selected appended jobs, or enumerate a waiting-queue prefix.
   * @details Pending jobs reuse cached live-planner starts by default. Missing
   * starts receive an on-demand reservation-aware forecast. Cached predictions
   * before current_time are not reused; other predictions can be stale between
   * periodic passes. With default reuse, cache-only requests skip resource
   * forecasting entirely. Protected-only reuse checks and installs compatible
   * reservation windows before projecting unprotected jobs.
   * The temporary capacity profile begins with current capacity and
   * running-job releases. It then retains the in-horizon reservations selected
   * by the configured EASY, HYBRID, or CONSERVATIVE policy. A protected job is
   * reported as RESERVED; another job receives an ESTIMATED earliest fit that
   * respects retained reservations. The query considers the entire waiting
   * queue by default and never mutates scheduler state. A finite
   * estimation_depth bounds the temporary projection and suppresses pending
   * timing outside its queue prefix. Scheduled capacity changes are part of
   * the profile; future
   * arrivals are not considered. Scheduled jobs
   * return their actual simulated start and end timestamps. Results preserve
   * request order and duplicate IDs. Cost is linear in the number of running
   * plus pending jobs per forecasted pending job. The fallback uses
   * half-open resource intervals and unrounded pending limits, whereas live
   * backfill uses a strict completion-before-reservation test and may round
   * planned releases to planning_time_resolution; use exact lookahead when
   * those boundary details matter.
   * @note Placement mode reports exact live or reserved node_ids and only
   * reservations from the latest scheduling pass; it does not run a capacity
   * forecast. Reporting snapshots are cleared on checkpoint restore and
   * rebuilt by subsequent scheduling. Released allocations are not retained.
   * @param[in] job_idxs Identifiers returned by append_job() or append_jobs().
   * An empty vector returns the waiting jobs in scheduler queue order, limited
   * by estimation_depth; those queue entries need not have been appended.
   * @param[in] estimation_depth Maximum number of waiting jobs, in scheduler
   * queue order, to receive a pending start-time result. Zero (the default)
   * covers the complete waiting queue. This affects query work and returned
   * pending timing only; it never changes scheduler configuration or state.
   * @return One Job_Status per requested identifier, in request order; with an
   * empty job_idxs vector, one pending status per returned queue entry.
   * @throws std::out_of_range if an ID was not returned by append_job(s).
   */
  std::vector<Job_Status>
  get_job_statuses(const std::vector<job_no_t> &job_idxs,
                   size_t estimation_depth = 0) const;

  /**
   * @brief Query with explicit cache reuse.
   * @note In placement mode, false selects an aggregate approximation;
   * use get_start_times_by_lookahead() for a fresh exact placement forecast.
   * @param[in] job_idxs Appended job identifiers; empty enumerates waiting
   * jobs.
   * @param[in] estimation_depth Waiting-queue prefix to forecast; zero is
   * unlimited.
   * @param[in] use_cached_predictions Reuse live starts and forecast gaps when
   * true, including unprotected estimates. False recomputes all pending starts,
   * including reservations, without replaying future backfill admissions.
   * @return Job statuses in request order, or waiting-queue order for empty
   * IDs.
   */
  std::vector<Job_Status>
  get_job_statuses(const std::vector<job_no_t> &job_idxs,
                   size_t estimation_depth, bool use_cached_predictions) const;

  /**
   * @brief Query with protected-only or full cache reuse.
   * @note In placement mode, disabling either reuse flag selects an aggregate
   * approximation; default reuse reports the live exact reservation snapshot.
   * @param[in] job_idxs Appended job identifiers; empty enumerates waiting
   * jobs.
   * @param[in] estimation_depth Waiting-queue prefix; zero is unlimited.
   * @param[in] use_cached_predictions Enable reuse of compatible live starts.
   * False recomputes all pending starts, including protected reservations.
   * @param[in] use_cached_unprotected_predictions When cache reuse is enabled,
   * true also reuses unprotected estimates. False preserves compatible cached
   * reservation windows and forecasts all unprotected jobs against them.
   * Ignored when use_cached_predictions is false.
   * @return Job statuses in request order, or queue order for empty IDs.
   */
  std::vector<Job_Status>
  get_job_statuses(const std::vector<job_no_t> &job_idxs,
                   size_t estimation_depth, bool use_cached_predictions,
                   bool use_cached_unprotected_predictions) const;

#if defined(DR_EVT_HAS_SER20)
  /**
   * @brief Replay an isolated copy of the scheduler to obtain exact starts.
   * @details Unlike get_job_statuses(), this advances a private in-memory
   * continuation through ordinary scheduler events.  It never changes this
   * simulation, its output streams, Redis state, or checkpoint files.
   * An empty ID list enumerates the current waiting-queue prefix; zero depth
   * selects the complete prefix. With explicit IDs, every requested ID is
   * returned and estimation_depth is ignored. The initial implementation runs
   * until the requested starts are known or the clone drains.
   */
  std::vector<Exact_Start_Result>
  get_start_times_by_lookahead(const std::vector<job_no_t> &job_idxs,
                               size_t estimation_depth = 0) const;
#endif

  /**
   * @brief Approximate pending-job starts from cumulative resource-time area.
   * @details Protected EASY, hybrid, and conservative reservation windows are
   * removed from the available-resource profile.  Each unreserved job is then
   * assigned the time at which utilization-scaled free resource area covers
   * the unreserved queued node-time ahead of it.  This is faster than a
   * first-fit placement for every unreserved job, but it is not a guarantee:
   * it does not model fragmentation or future backfill choices.  A protected
   * job returns its reservation with kind RESERVED.  The query is on demand
   * and does not retain state in the scheduler.
   * @param[in] utilization Expected usable-capacity factor in (0, 1], or zero
   * to use one.
   * @param[in] estimation_depth Maximum waiting-queue prefix to return; zero
   * covers the entire waiting queue.
   * @return Estimates in scheduler queue order.
   * @throws std::invalid_argument if utilization is not finite or is outside
   * [0, 1].
   */
  std::vector<Queue_Drain_Start_Estimate>
  get_start_times_by_queue_drain(double utilization,
                                 size_t estimation_depth = 0) const;

protected:
  /**
   * @brief Query job status with a compile-time-selected forecast rule.
   * @details GroupEvents enables inclusive one-second forecast groups.
   * Selection occurs once in the public query; the job loop has no policy
   * checks and ordinary forecasts allocate no grouping event list.
   * @tparam GroupEvents Whether to group predicted events.
   * @param[in] job_idxs Requested job identifiers, or empty to enumerate.
   * @param[in] estimation_depth Maximum pending-job prefix; zero is unlimited.
   * @param[in] use_cached_predictions Reuse live start predictions first.
   * @param[in] use_cached_unprotected_predictions Also reuse unprotected
   * starts.
   * @return Job statuses following the selected forecast rule.
   */
  template <bool GroupEvents>
  std::vector<Job_Status>
  get_job_statuses_impl(const std::vector<job_no_t> &job_idxs,
                        size_t estimation_depth, bool use_cached_predictions,
                        bool use_cached_unprotected_predictions) const;

  /**
   * @brief Estimate queue drain with a compile-time-selected forecast rule.
   * @tparam GroupEvents Whether to group predicted reservation events.
   * @param[in] utilization Expected usable-capacity factor; zero means one.
   * @param[in] estimation_depth Maximum waiting-job prefix; zero is unlimited.
   * @return Pending-job start estimates without per-job forecast-policy checks.
   */
  template <bool GroupEvents>
  std::vector<Queue_Drain_Start_Estimate>
  get_start_times_by_queue_drain_impl(double utilization,
                                      size_t estimation_depth) const;

#if defined(DR_EVT_HAS_SER20)
  /** Estimate serialized checkpoint bytes before allocating a buffer. */
  size_t checkpoint_reserve_bytes() const;
  void save_checkpoint_impl(std::ostream &output, bool preserve_outputs);
  void load_checkpoint_impl(std::istream &input, bool restore_outputs);
#endif
  /**
   * @brief Validate an existing job and enqueue it in the scheduler.
   *
   * Submit an already-known job to the scheduler's waiting queue.
   * This is an implementation detail used by append_job(), append_jobs(),
   * and batch loading; external streaming callers use append_job().
   * The internal scheduler will decide when to start the job based on
   * resources and backfilling policy.
   *
   * @param[in] job_idx Existing Trace job identifier to submit.
   * @param[in] submit_time Original job submission time retained by the
   * scheduler.
   *
   * This does not add a job record to Trace: job_idx must already identify
   * one. It validates the arrival, records arrival-time accounting, then
   * calls SchedulerBase::insert_job() to enqueue the scheduling data.
   * Call advance_to() to let the scheduler select a job to start; that
   * path calls Trace::insert_job() to record the start and end events.
   *
   * @see append_job()
   * @see SchedulerBase::insert_job()
   * @see Trace::insert_job()
   */
  void submit_job(job_no_t job_idx, sim_time_t submit_time);

  /** Submit a stored job observed as arriving at arrival_time. */
  void submit_job(job_no_t job_idx, sim_time_t submit_time,
                  sim_time_t arrival_time);

  void record_appended_job(job_no_t job_idx, sim_time_t submit_time,
                           num_nodes_t num_nodes, tdiff_t limit_time);
  void record_appended_job_start(job_no_t job_idx);

public:
  /**
   * @brief Advance simulation time and start eligible queued jobs.
   *
   * Advance simulation to target time (streaming mode)
   * Processes all events up to target_time and lets the scheduler make
   * decisions about which jobs to start.
   *
   * @param[in] target_time Time to advance to, which must be >= current time.
   *
   * PRECONDITION: Caller guarantees no jobs will be submitted with
   * submit_time < target_time. This means either:
   * - All jobs have already been submitted, OR
   * - External tool knows the next job arrival is at >= target_time
   *
   * POSTCONDITION: for a finite target, m_current_time == target_time and all
   * scheduling decisions have been made up to that time. The maximum
   * representable value is treated as a drain sentinel; after draining,
   * m_current_time is the last real event time.
   *
   * Jobs selected by the scheduler are recorded through Trace::insert_job().
   * @see submit_job()
   * @see Trace::insert_job()
   */
  void advance_to(sim_time_t target_time);

  /**
   * @brief Get the number of nodes currently in use.
   * @return Allocated-node count as num_nodes_t.
   */
  num_nodes_t get_nodes_in_use() const;

  /**
   * @brief Return instantaneous node utilization.
   * @return Nodes currently used divided by effective current capacity.
   * @details Effective capacity is the scheduled capacity, raised to current
   * occupancy while a non-preemptive reduction is still draining. The result
   * therefore remains in [0,1], including when scheduled capacity is zero.
   */
  double get_current_utilization() const {
    const auto used = get_nodes_in_use();
    const auto effective_capacity = std::max(m_current_capacity, used);
    return effective_capacity == 0
               ? 0.0
               : static_cast<double>(used) /
                     static_cast<double>(effective_capacity);
  }

  /**
   * @brief Return Custom-FCFS time-accounted resource usage in node-seconds.
   * @details Available only when this simulation was constructed with the
   * FCFSEasyCustomScheduler callback constructor. Accounting occurs once per
   * settled scheduling timestamp and includes the elapsed portion of the
   * current interval for finite snapshots.
   * @return Resource area in node-seconds.
   * @throws std::logic_error if this simulation does not use Custom FCFS.
   */
  tdiff_t get_resource_area() const;

  /**
   * @brief Get the current simulation time.
   * @return Current simulation time as sim_time_t.
   */
  sim_time_t get_current_time() const { return m_current_time; }

  /**
   * @brief Get mutable trace data for streaming-mode access.
   * @return Mutable reference to the simulation Trace.
   */
  TraceType &get_trace() { return m_trace; }
  /** @brief Return read-only access to the simulation trace.
   * @return Read-only reference to the simulation Trace. */
  const TraceType &get_trace() const { return m_trace; }

  // ========================================================================
  // Monitoring and Statistics API for Python/External Tools
  // ========================================================================

  /**
   * @brief Get the number of available nodes.
   * @return Unallocated-node count as num_nodes_t.
   */
  num_nodes_t get_available_nodes() const {
    const auto occupied = get_nodes_occupied();
    return occupied < m_current_capacity ? m_current_capacity - occupied : 0;
  }

  /**
   * @brief Nodes whose job has ended but which are not yet reusable.
   * @details Nonzero only with a node release delay (see
   * Sim_Params::m_node_release_delay). These nodes are neither in use nor
   * available.
   * @return Releasing-node count as num_nodes_t.
   */
  num_nodes_t get_nodes_releasing() const {
    return m_trace.get_nodes_releasing();
  }

  /** @brief Nodes unavailable to new starts: in use plus releasing. */
  num_nodes_t get_nodes_occupied() const {
    return m_trace.get_nodes_occupied();
  }

  /** @brief Capacity currently available to this simulated workload. */
  num_nodes_t get_current_capacity() const { return m_current_capacity; }

  /**
   * @brief Get the count of jobs that have arrived but remain unscheduled.
   *
   * Uses SchedulerBase::active_job_count(), which is correct for all four
   * scheduler implementations - see SchedulerBase::sync_to() for the
   * full explanation of how eligibility tracking stays fresh without
   * this method needing to pass or check a time itself. Safe to call
   * with no time argument because nothing can call this while
   * advance_to() is mid-execution (single-threaded, no reentrancy) -
   * by the time this runs, the scheduler is already synced to
   * m_current_time from the last completed advance_to() call (or from
   * construction, if none has run yet).
   *
   * @return Waiting-job count as size_t.
   */
  size_t get_active_job_count() const {
    return m_scheduler->active_job_count();
  }

  /**
   * @brief Get the estimated start time of the FCFS queue head.
   * Returns the shadow time (earliest time head of queue can start)
   * @return Estimated start time as sim_time_t, or -1 when the queue is empty
   * or when no running-job release or scheduled capacity change ever lets
   * the head start (a reduction below its request with no later increase).
   * An expired placement forecast is unknown until another scheduling pass.
   */
  sim_time_t get_fcfs_head_shadow_time() const {
    if (m_scheduler->active_job_count() == 0) {
      return -1.0;
    }
    const sim_time_t reservation = m_scheduler->get_fcfs_reservation_time();
    return reservation == std::numeric_limits<sim_time_t>::max() ||
                   (m_running_jobs.placement() && reservation < m_current_time)
               ? -1.0
               : reservation;
  }

  /**
   * @brief Point-in-time FCFS/EASY reservation projection.
   *
   * @details Returned by get_backfill_window() for a caller evaluating
   * whether a candidate can backfill without delaying the FCFS queue head.
   * `current_time` and `available_nodes` describe capacity immediately;
   * `releases` then describes every projected capacity increase from the
   * currently running jobs. Release events use the time-limit estimates used
   * by SchedulerBase::calculate_fcfs_reservation, rather than actual runtimes,
   * so the projection and reservation agree. `releases` lists running-job
   * releases only; `shadow_time` additionally accounts for scheduled
   * capacity changes. `shadow_time` is -1 when no FCFS head is waiting or
   * when the head has no projected start (see get_fcfs_head_shadow_time()).
   */
  struct Backfill_Window {
    /**
     * @brief One future capacity increase in a Backfill_Window.
     *
     * @details Entries in Backfill_Window::releases are sorted by time.
     * Multiple job completions at the same time are combined into one
     * record whose nodes_released is their total.
     */
    struct Resource_Release {
      sim_time_t time;            ///< Absolute simulation time of the release.
      num_nodes_t nodes_released; ///< Nodes becoming available at time.
    };

    sim_time_t current_time;     ///< Time at which this snapshot was captured.
    num_nodes_t available_nodes; ///< Nodes free immediately at current_time.
    sim_time_t
        shadow_time; ///< Reserved FCFS-head start time, or -1 if no head waits.
    std::vector<Resource_Release> releases; ///< Projected capacity increases.
  };

  /**
   * @brief Return the current EASY-backfilling reservation projection.
   * @note In placement mode this summarizes quantities and the actual head
   * start; it cannot prove a placement fit. Inspect placement_reservations()
   * for exact masks.
   * @details
   * The result is a snapshot: available nodes and release times reflect
   * current scheduler state, while release times are based on the same
   * time-limit estimates used for the FCFS reservation. Releases include
   * every currently running job, including when no FCFS head waits.
   * @return Backfill_Window value for the current simulation time.
   */
  Backfill_Window get_backfill_window() const;

  /**
   * @brief Estimate the Custom-FCFS waiting-queue drain time.
   * @note This aggregate approximation is available in placement mode and
   * does not account for exact resource identities.
   * @details Computes the resource-area prediction horizon beginning at the
   * FCFS head's shadow time. Current waiting demand A_Q is the sum of each
   * waiting job's requested nodes times estimated runtime. Service capacity
   * is projected from current running-job releases and scaled by utilization.
   * Future arrivals are not included.
   *
   * The horizon is estimated only after the current FCFS-EASY backfilling
   * cycle has completed. Consequently, all jobs that can be immediately
   * dispatched have already entered the running set, and the waiting queue
   * contains only jobs that remain waiting. The running set includes jobs
   * newly dispatched during the cycle. The estimator then forward-replays
   * predicted completions of that fixed running-job set without admitting
   * additional jobs from the waiting queue.
   *
   * Between completion events, available resources are constant. If the
   * accumulated usable area reaches queued demand within one such interval,
   * that interval's ending completion event defines the horizon. If the last
   * currently running job completes first, the remaining demand is estimated
   * continuously using utilization times total system capacity. Utilization
   * is an expected capacity factor for fragmentation and scheduling losses;
   * it is not instantaneous utilization at the current scheduling time. An
   * input value of zero selects the fallback factor U = 1.
   *
   * @pre The caller has completed scheduling at the current time, normally by
   * calling advance_to(get_current_time()) after adding jobs at that time.
   * @pre The selected scheduler supports on-demand waiting-resource-area
   * queries and uses EASY backfilling.
   * @param[in] utilization Expected system-utilization factor in (0, 1], or
   * zero to use the fallback factor 1.
   * @return Horizon duration from the shadow time. Returns zero for an empty
   * queue and infinity only if positive demand exists with zero total system
   * resources.
   * @throws std::invalid_argument if utilization is non-finite, negative, or
   * greater than 1.
   * @throws std::logic_error if the scheduler does not support prediction or
   * does not use EASY backfilling.
   */
  tdiff_t estimate_queue_horizon(double utilization) const;

  /**
   * Get detailed scheduling statistics
   * @return Structure with wait times, turnaround, utilization
   */
  struct Statistics {
    num_jobs_t jobs_submitted;   ///< Jobs accepted into the wait queue.
    num_jobs_t jobs_started;     ///< Jobs that have begun execution.
    num_jobs_t jobs_completed;   ///< Jobs that have completed execution.
    num_jobs_t jobs_running;     ///< Jobs executing at the snapshot time.
    num_jobs_t jobs_waiting;     ///< Jobs awaiting scheduler placement.
    sim_time_t current_time;     ///< Time represented by this snapshot.
    num_nodes_t total_nodes;     ///< Configured cluster capacity.
    num_nodes_t nodes_in_use;    ///< Nodes allocated at current_time.
    num_nodes_t nodes_available; ///< Nodes free at current_time.
    /**
     * @brief Allocated-node area in node-seconds.
     * @details Live time-accounted area for Custom FCFS; scheduled-job area
     * for standard schedulers.
     */
    tdiff_t resource_area;
    /**
     * @brief Resource-area utilization.
     * @details Divides allocated-node area by effective-capacity area. Uses
     * the live accounting horizon for Custom FCFS and makespan for standard
     * schedulers.
     */
    double utilization;
    tdiff_t avg_wait_time;       ///< Mean completed-job wait duration.
    tdiff_t avg_run_time;        ///< Mean completed-job execution duration.
    tdiff_t avg_turnaround_time; ///< Mean completed-job submit-to-end duration.
    double avg_bounded_slowdown; ///< Mean bounded slowdown (10-second bound).
    sim_time_t makespan;         ///< Latest completion time in the trace.
  };

  /**
   * @brief Calculate aggregate and point-in-time simulation statistics.
   * @return Statistics value derived from the trace and current scheduler
   * state.
   */
  Statistics get_statistics() const;

  /**
   * @brief Report the configuration recorded in the last restored checkpoint.
   * @details The simulation keeps running with its own parameters; this is
   * the complete Sim_Params of the run that wrote the checkpoint.
   * @return The recorded configuration, or empty if none was loaded.
   */
  const std::optional<Sim_Params> &restored_params() const {
    return m_restored_params;
  }

  /**
   * @brief Run until just before a target time.
   *
   * Excludes events occurring exactly at target_time.
   * @param[in] target_time Exclusive upper time bound as sim_time_t.
   */
  void run_until_exclusive(sim_time_t target_time) {
    // Process every pending event with time < target_time, leaving events
    // at exactly target_time for a later advance_to(). Pending work lives
    // in three places: the trace's start/end event queue, the scheduler's
    // future arrivals (appended jobs), and the capacity schedule. This
    // used to scan m_event_queue, which nothing populates outside of
    // checkpoint I/O, so it never advanced at all.
    if (target_time <= m_current_time) {
      return;
    }
    sim_time_t previous = -std::numeric_limits<sim_time_t>::infinity();
    while (true) {
      m_scheduler->sync_to(m_current_time); // get_next_arrival_time() needs it
      sim_time_t next = m_scheduler->get_next_arrival_time();
      if (!m_trace.pending_events().empty()) {
        next = std::min(next, m_trace.pending_events().begin()->get_time());
      }
      if (m_next_capacity_change < m_capacity_changes.size()) {
        next = std::min(next, m_capacity_changes[m_next_capacity_change].time);
      }
      // In periodic mode a backfill tick is an event too, but only while a
      // job is waiting for a backfill decision (advance_to() applies the same
      // rule); otherwise the tick is skipped and the cadence kept.
      if (m_params.scheduling_interval() > 0.0 &&
          m_scheduler->active_job_count() > 0) {
        align_backfill_tick();
        next = std::min(next, m_next_backfill_evaluation);
      }
      // Stop at the first event at or after the target, or if no progress
      // was made (defensive; advance_to() consumes everything <= next).
      if (next >= target_time || next <= previous) {
        break;
      }
      previous = next;
      advance_to(next);
    }
  }

  /**
   * @brief Initialize trace data and reset simulation state.
   *
   * Load trace data, sort by submission time, and
   * determine job durations (simulation mode only). Resets simulation
   * state (m_current_time, job counters) for a fresh run.
   *
   * Public streaming callers (Python bindings, gRPC server) must call
   * this instead of calling get_trace().load_data() directly - that
   * skips the sort and duration-determination steps below, which was a
   * real, previously undetected bug affecting both the existing Python
   * bindings and an early gRPC server draft: jobs loaded that way get
   * silently wrong actual-duration data, which cascades into wrong
   * scheduling decisions and wrong avg_wait_time/avg_turnaround_time/
   * makespan statistics, with no error raised anywhere.
   *
   * @param[in] max_jobs Maximum jobs to load. 0 (the default)
   *                 falls back to m_params.m_max_jobs if that was set,
   *                 or no limit otherwise - this is what run() relies on
   *                 for batch mode. Streaming callers should pass an
   *                 explicit value here instead of relying on Sim_Params.
   * @return Number of jobs loaded as num_jobs_t.
   */
  num_jobs_t initialize_trace(num_jobs_t max_jobs = 0);

protected:
  /**
   * Advance using compile-time-selected accounting, warm-start, and trigger
   * paths. WarmStage is instantiated only while historical jobs remain; the
   * normal stage contains no pre-start-time condition.
   */
  template <bool AccountResources, bool WarmStage = false,
            bool PeriodicBackfill = false, bool FixedPeriodicDispatch = false,
            bool DispatchOnEvents = false>
  void advance_to_impl(sim_time_t target_time,
                       FCFSEasyCustomScheduler *custom_scheduler);

  /** Seed replay jobs active before m_params.m_sim_start_time, then reschedule
   * post-boundary work through the ordinary scheduler. */
  void run_warm_start();

  /**
   * @brief Process an arrival event for an existing trace job.
   * @param[in] job_idx Identifier of the arriving job.
   */
  void process_submit_event(job_no_t job_idx);

  /**
   * @brief Process a scheduler-selected job start event.
   * @param[in] job_idx Identifier of the job to start.
   */
  void process_start_event(job_no_t job_idx);

  /**
   * @brief Process a job completion event and release its resources.
   * @param[in] job_idx Identifier of the completing job.
   */
  void process_end_event(job_no_t job_idx);

  /**
   * @brief Process the chronologically next queued event.
   * @return true when an event was processed; false when the queue is empty.
   */
  bool advance_to_next_event();

  /**
   * @brief Queue start events for jobs selected by the scheduler.
   * @param[in] jobs Identifiers of jobs that can start at the current time.
   */
  void schedule_start_events(const std::vector<job_no_t> &jobs);

  /**
   * @brief Queue the completion event for a started job.
   * @param[in] job_idx Identifier of the running job.
   * @param[in] start_time Time at which the job started.
   */
  void schedule_end_event(job_no_t job_idx, sim_time_t start_time);

  /**
   * @brief Determine actual durations for all jobs in simulation mode.
   *
   * Called during initialization before simulation starts
   */
  void determine_job_run_time();

  /**
   * @brief Determine actual durations for selected jobs.
   *
   * Same as determine_job_run_time(), but only for the given jobs -
   * for progressive loading, where each file's jobs need this done
   * once they're loaded, not just once for the initial batch.
   * @param[in] job_nos Permanent identifiers of jobs to update.
   */
  void determine_job_run_time(const std::vector<job_no_t> &job_nos);

  /**
   * @brief Determine the simulated duration of one job.
   * @param[in,out] job Trace record whose simulated duration is updated.
   */
  void determine_one_job_run_time(Job_Record &job);

  /**
   * @brief Run the configured sequence of trace files incrementally.
   * @details
   * Loads each file in turn, submits its jobs in submit-time order, then
   * advances to its last arrival before loading the next file. A final
   * advance drains outstanding work. REPLAY input is not supported.
   */
  void run_progressive();

  /**
   * @brief Write a configured automatic checkpoint when it is due.
   * @param[in] file_boundary Whether a progressive file was fully processed.
   */
  void maybe_save_automatic_checkpoint(bool file_boundary);

  /**
   * @brief Record what each job arriving now sees ahead of it in the queue.
   * @details The scheduler must already be synchronized to current_time. For
   * several jobs with the same timestamp, input order defines their
   * observations: if n earlier jobs are waiting, they observe n, n+1, ... .
   */
  bool record_queue_arrivals(sim_time_t current_time);

  /** Reset the capacity cursor to the configured maximum. */
  void reset_capacity_schedule();

  /**
   * Hand the scheduler the capacity in effect, the nodes occupied now, and
   * the scheduled changes still ahead, so reservations can anticipate them.
   * Called before every scheduling pass; the range points into
   * m_capacity_changes, which outlives the scheduler's use of it.
   */
  void publish_capacity_outlook() {
    const Capacity_Change *data = m_capacity_changes.data();
    m_scheduler->set_capacity_outlook(m_current_capacity, get_nodes_occupied(),
                                      data + m_next_capacity_change,
                                      data + m_capacity_changes.size());
  }

  /** Install the configured node release delay in the trace and scheduler. */
  void configure_node_release_delay();

  /**
   * Bring the next periodic backfill tick up to date with the clock.
   *
   * Initializes the first tick one interval after the current time and moves
   * a tick left in the past (unconsumed while nothing waited) forward on the
   * same cadence to the first grid point at or after the current time. No-op
   * when periodic backfill is not configured.
   */
  void align_backfill_tick();

  /**
   * Return the planned node-release offset after quantizing the projected
   * release timestamp. Execution itself remains exact. A node release delay
   * is not planned for: the projection is start + estimate, as in production
   * schedulers; once the job ends, the entry is updated to its actual release.
   */
  tdiff_t planning_run_time(sim_time_t start_time,
                            tdiff_t run_time_estimate) const {
    const tdiff_t resolution = m_params.m_planning_time_resolution;
    sim_time_t release_time = start_time + run_time_estimate;
    if (resolution != 0.0) {
      release_time = std::ceil(release_time / resolution) * resolution;
    }
    return release_time - start_time;
  }

  /**
   * Nodes not running work, for Custom-FCFS resource-area accounting.
   * Releasing nodes are not utilization, so they count as available here even
   * though the scheduler cannot allocate them yet.
   */
  num_nodes_t accounting_available_nodes() const {
    const auto used = get_nodes_in_use();
    return used < m_current_capacity ? m_current_capacity - used : 0;
  }

  /** Reset time-integrated effective-capacity accounting at start_time. */
  void reset_capacity_accounting(sim_time_t start_time);

  /** Accumulate effective capacity through current_time. */
  void advance_capacity_accounting_to(sim_time_t current_time);

  /** Return effective-capacity area through a finite snapshot time. */
  tdiff_t capacity_area_through(sim_time_t through_time) const;

  /** Apply every capacity change effective at the supplied timestamp. */
  bool apply_capacity_changes(sim_time_t current_time);

  /**
   * @brief Sample a job duration from the configured distribution.
   * @param[in] time_limit User-provided time limit.
   * @param[in] dist Distribution type.
   * @param[in] scale Distribution scale factor.
   * @param[in] stddev Distribution standard-deviation factor.
   * @return Sampled duration as tdiff_t.
   */
  tdiff_t sample_run_time(tdiff_t time_limit, DistributionType dist,
                          double scale, double stddev);
};

using Simulation = BasicSimulation<Trace>;
using PconSimulation = BasicSimulation<PconTrace>;
using SwfSimulation = BasicSimulation<SwfTrace>;

extern template class BasicSimulation<Trace>;
extern template class BasicSimulation<PconTrace>;
extern template class BasicSimulation<SwfTrace>;

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_SIM_SIM_HPP
