/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_base.hpp
 * @brief Shared capacity forecasts and scheduler selection.
 * @details Forecasts subtract requested capacity over complete reservation
 * intervals. The factory chooses queue order, backfill protection,
 * examination accounting, and timing. The simulation driver owns event and
 * timer wakes; this shared code is not a standalone scheduler.
 * @note CLI: Concrete selection uses --priority_policy, --backfill_policy,
 * --queue_impl, --per_pass_accounting, --backfill_interval, and
 * --dispatch_interval. --reservation_depth and the backfill/dispatch queue
 * depths configure the chosen policy. See the concrete scheduler file for a
 * complete supported combination.
 * Protobuf: Simulation_Params and InitRequest use the same field names
 * without the leading --; see dr_evt_params.proto and dr_evt_service.proto.
 */

#ifndef DR_EVT_SIM_SCHEDULER_BASE_HPP
#define DR_EVT_SIM_SCHEDULER_BASE_HPP

#include "common.hpp"
#include "params/sim_params.hpp"
#include "sim/capacity_schedule.hpp"
#include "sim/resource/count_timeline.hpp"
#include "sim/scheduler_policies.hpp"
#include "trace/job_record.hpp"
#include "trace/trace.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dr_evt {

template <typename TraceType> class BasicSimulation;

/** \addtogroup dr_evt_sim
 *  @{ */

/**
 * @brief Describes a currently running job for reservation calculations.
 */
struct Running_Job {
  sim_time_t start_time;
  tdiff_t run_time;
  num_nodes_t nodes;
};

using running_jobs_t = std::map<job_no_t, Running_Job>;
/// Jobs selected during the current scheduling pass but not yet published.
using projected_starts_t = std::vector<Running_Job>;

/// Compatibility name for the standalone aggregate quantity timeline.
using Reservation_Timeline = CountTimeline;

/**
 * @brief Abstract interface shared by all job schedulers.
 * @details
 * Implementations provide FCFS or priority-based queue ordering and optional
 * backfilling. The simulation submits validated job metadata, synchronizes
 * arrival eligibility, and repeatedly asks the scheduler which jobs can start
 * with the currently available nodes.
 * Scheduling triggers belong to the simulation driver; this interface owns
 * no event loop or timer and specifies no queue container.
 * @note No option selects SchedulerBase directly. create_scheduler() chooses
 * a concrete implementation from --priority_policy, --backfill_policy,
 * --queue_impl, --dispatch_interval, and --backfill_interval.
 */
class SchedulerBase {
  template <typename TraceType> friend class BasicSimulation;

protected:
  /// Total nodes available to jobs selected by this scheduler.
  num_nodes_t m_total_nodes;
  /// Backfill rule applied when jobs other than the FCFS head are considered.
  BackfillPolicy m_backfill_policy;
  /// Projected earliest start time reserved for the FCFS queue head.
  sim_time_t m_fcfs_reservation_time;
  /// Grid used to round projected release timestamps; zero is exact.
  tdiff_t m_planning_time_resolution = 0.0;
  /// Scheduled capacity the projections start from (see set_capacity_outlook).
  num_nodes_t m_outlook_capacity = 0;
  /// Nodes allocated or releasing when the outlook was published; above
  /// m_outlook_capacity the system is overcommitted after a reduction.
  num_nodes_t m_outlook_occupied = 0;
  /// First scheduled capacity change not yet applied, or null when none.
  const Capacity_Change *m_outlook_changes_begin = nullptr;
  /// One past the last scheduled capacity change.
  const Capacity_Change *m_outlook_changes_end = nullptr;
  /// Furthest ahead of the current time a projected reservation may start
  /// and still be planned (Slurm bf_window); unlimited by default.
  tdiff_t m_planning_horizon = std::numeric_limits<tdiff_t>::max();

  struct Estimated_Start {
    sim_time_t start;
    job_no_t job_id;
    bool reserved;
  };
  // Sparse, reusable storage for values the live planner already computes.
  // Queue entries carry no prediction fields. This is not checkpoint state.
  std::vector<Estimated_Start> m_estimated_starts;
  std::optional<sim_time_t> m_estimated_at;

  void begin_start_time_plan(sim_time_t current_time) {
    m_estimated_starts.clear();
    m_estimated_at = current_time;
  }

  // Keep recording inline even when the vector-growth path is present.
  [[gnu::always_inline]] inline void
  record_estimated_start(job_no_t job_id, sim_time_t start, bool reserved) {
    if (!std::isfinite(start) ||
        start == std::numeric_limits<sim_time_t>::max()) {
      return;
    }
    if (!m_estimated_starts.empty() &&
        m_estimated_starts.back().job_id == job_id) {
      m_estimated_starts.back() = {start, job_id, reserved};
    } else {
      m_estimated_starts.push_back({start, job_id, reserved});
    }
  }

public:
  /**
   * @brief Construct a scheduler for the supplied cluster capacity.
   * @param[in] total_nodes Total nodes available to schedule.
   * @param[in] bf_policy Backfilling policy to apply during scheduling.
   */
  SchedulerBase(num_nodes_t total_nodes, BackfillPolicy bf_policy)
      : m_total_nodes(total_nodes), m_backfill_policy(bf_policy),
        m_fcfs_reservation_time(0) {}

  /** @brief Destroy a scheduler through its polymorphic base interface. */
  virtual ~SchedulerBase() = default;

  /**
   * @brief Set the grid used for projected reservation timestamps.
   * @param[in] resolution Seconds between planning timestamps; zero retains
   * exact projected release times.
   */
  void set_planning_time_resolution(tdiff_t resolution) {
    m_planning_time_resolution = resolution;
  }

  /**
   * @brief Set how far ahead reservations are planned.
   * @details
   * A waiting job whose projected reservation starts more than this many
   * seconds after the current time is not planned in that pass: it does not
   * block backfill candidates and consumes neither a hybrid reservation-depth
   * slot nor a backfill queue-depth scan slot. It keeps its queue position
   * and is re-evaluated on the next pass. Under EASY only the head holds a
   * reservation, so only the head is exempted. Applies to event-driven and
   * periodic scheduling alike.
   * @param[in] horizon Look-ahead in seconds; numeric_limits<tdiff_t>::max()
   * leaves planning unbounded.
   * @throws std::invalid_argument if the horizon is negative or not finite.
   */
  void set_planning_horizon(tdiff_t horizon) {
    if (!std::isfinite(horizon) || horizon < 0.0) {
      throw std::invalid_argument(
          "planning_horizon must be finite and nonnegative");
    }
    m_planning_horizon = horizon;
  }

  /**
   * @brief Check whether a projected reservation is in the planning window.
   * @param[in] reservation_time Projected reservation start timestamp.
   * @param[in] current_time Timestamp at which planning occurs.
   * @return true when the reservation starts no more than the planning
   * horizon after current_time.
   */
  bool reservation_within_horizon(sim_time_t reservation_time,
                                  sim_time_t current_time) const {
    if (reservation_time == std::numeric_limits<sim_time_t>::max()) {
      return false;
    }
    return static_cast<tdiff_t>(reservation_time - current_time) <=
           m_planning_horizon;
  }

  /**
   * @brief Enqueue an already-validated job in this scheduler's wait queue.
   * @details
   * This does not read or create a Trace job record and does not start the
   * job. Simulation::submit_job() supplies the copied scheduling fields;
   * a subsequent schedule() call chooses any job that can start.
   *
   * @param[in] job_id Existing Trace job identifier.
   * @param[in] submit_time Arrival time used for eligibility and ordering.
   * @param[in] run_time_estimate Time-limit estimate used for reservations.
   * @param[in] nodes_requested Requested node count.
   * @see Simulation::submit_job()
   * @see Trace::insert_job()
   */
  virtual void insert_job(job_no_t job_id, sim_time_t submit_time,
                          tdiff_t run_time_estimate,
                          num_nodes_t nodes_requested) = 0;

  /**
   * @brief Select wait-queue jobs that may start at current_time.
   * @details
   * The returned identifiers are still existing Trace records. The caller
   * records each selected start through Trace::insert_job(). Implementations
   * that use a resource-time reservation plan should build it once per call,
   * use it to determine the FCFS reservation, then update that same plan as
   * backfill candidates are admitted.
   *
   * @param[in] free_nodes Nodes currently available to allocate.
   * @param[in] running_jobs Running jobs with start time, runtime, and nodes.
   * @param[in] current_time Simulation time at which eligibility is evaluated.
   * @return Vector of existing Trace job identifiers selected to start.
   * @see insert_job()
   * @see Trace::insert_job()
   */
  virtual std::vector<job_no_t> schedule(num_nodes_t free_nodes,
                                         const running_jobs_t &running_jobs,
                                         sim_time_t current_time) = 0;

  /**
   * @brief Run an event-driven FCFS pass without a periodic backfill scan.
   * @details Periodic scheduler variants override this so ordinary arrivals,
   * completions, and capacity changes still dispatch FCFS work immediately;
   * the full schedule() method is reserved for periodic backfill ticks.
   * Non-periodic schedulers use their normal schedule() behavior.
   * @param[in] free_nodes Nodes currently available for allocation.
   * @param[in] running_jobs Jobs already running at this instant.
   * @param[in] current_time Current simulation time.
   * @return Job IDs selected from the FCFS front.
   */
  virtual std::vector<job_no_t>
  schedule_event(num_nodes_t free_nodes, const running_jobs_t &running_jobs,
                 sim_time_t current_time) {
    return schedule(free_nodes, running_jobs, current_time);
  }

  /**
   * @brief Advance this scheduler's internal eligibility tracking.
   * @details
   * This is an explicit command that advances eligibility tracking to
   * current_time. Idempotent and cheap to call redundantly
   * (implementations early-return if current_time hasn't advanced past
   * what's already tracked) - schedule() calls this at its own top
   * unconditionally, so it remains safe to call regardless of whether
   * a caller has already synced.
   *
   * This is the ONLY place eligibility tracking advances. The query
   * methods below (active_job_count, get_next_arrival_time,
   * has_eligible_jobs) do NOT sync themselves - they trust that
   * whoever cares about a particular current_time has already called
   * sync_to(current_time) first. Simulation::advance_to() calls this
   * explicitly immediately after each of its two m_current_time
   * assignments, closing the one gap where schedule() itself might not
   * run in the same step (see Simulation::advance_to() for the traced
   * justification).
   * @param[in] current_time Simulation time through which arrivals become
   * eligible.
   */
  virtual void sync_to(sim_time_t current_time) = 0;

  /**
   * @brief Count jobs that have arrived but remain unscheduled.
   * @details
   * Counts jobs that are WAITING to be scheduled (arrived but not yet
   * scheduled), as of whatever time this scheduler was last synced to
   * via sync_to(). Does not take a time parameter and does not sync
   * itself - see sync_to() above for why that's safe here.
   *
   * @return Number of waiting jobs (arrived but not yet scheduled).
   */
  virtual size_t active_job_count() const = 0;

  /**
   * @brief Return the earliest future arrival known to this scheduler.
   * @details
   * Call sync_to() first. Returns std::numeric_limits<sim_time_t>::max()
   * when no future arrival remains.
   * @return Earliest future arrival as sim_time_t, or the maximum value.
   */
  virtual sim_time_t get_next_arrival_time() = 0;

  /**
   * @brief Report whether at least one already-arrived job is waiting.
   * @details
   * Call sync_to() first; this query does not update eligibility itself.
   * @return true when at least one eligible, unscheduled job exists.
   */
  virtual bool has_eligible_jobs() = 0;

  /**
   * @brief Return identifiers of jobs still owned by the wait queue.
   * @details This is an on-demand checkpoint view, not persistent duplicate
   * state. Scheduled/removed entries are excluded and future arrivals remain
   * included.
   * @return Pending job identifiers in implementation-defined order.
   */
  virtual std::vector<job_no_t> pending_job_ids() const = 0;

  /**
   * @brief Report whether a request could ever be satisfied by this scheduler.
   * @details
   * Simulation::submit_job() already rejects requests larger than the total
   * capacity. A scheduler that partitions that capacity can decline requests
   * no partition can hold, so that they take the same rejection path instead
   * of entering the wait queue and blocking the jobs behind them. The default
   * accepts every request.
   * @param[in] nodes_requested Requested node count.
   * @return true when some future state could start the request.
   */
  virtual bool can_ever_run(num_nodes_t nodes_requested) const {
    (void)nodes_requested;
    return true;
  }

  /**
   * @brief Return the current FCFS-head reservation time.
   * @return Earliest projected time the FCFS head can start, in sim_time_t.
   */
  sim_time_t get_fcfs_reservation_time() const {
    return m_fcfs_reservation_time;
  }

  /**
   * @brief Sum requested-node time for jobs currently waiting.
   * @details Implementations compute this on demand from their existing queue
   * records. The default reports that prediction is unsupported, avoiding any
   * storage or scheduling-path overhead for schedulers that do not opt in.
   */
  virtual std::optional<tdiff_t> waiting_resource_area() const {
    return std::nullopt;
  }

  /**
   * @brief Estimate waiting-queue drain time after the FCFS shadow time.
   * @details Uses waiting_resource_area() and projected running-job releases.
   * No prediction state is maintained between calls.
   */
  tdiff_t prediction_horizon(const running_jobs_t &running_jobs,
                             sim_time_t current_time, double utilization) const;

protected:
  /**
   * Return total size of the wait queue (all jobs, ALL states).
   *
   * Counts ALL jobs in the queue:
   * - Future arrivals (submit_time > current_time)
   * - Waiting jobs (submit_time <= current_time AND !removed)
   * - Scheduled jobs (submit_time <= current_time AND removed)
   *
   * Internal utility only - no external caller needs the "including
   * scheduled jobs" count. Use active_job_count() for the number of
   * jobs actually waiting to be scheduled.
   *
   * @return Total jobs in the wait queue as size_t, including removed entries.
   */
  virtual size_t wait_queue_size() const = 0;

  /**
   * @brief Time from a job's start until its nodes are reusable, as planned.
   * @details The time-limit estimate, with the projected release timestamp
   * rounded to the planning grid once the start time is known. A configured
   * node release delay is deliberately not included: production schedulers
   * plan at the time limit and discover cleanup time when nodes fail to
   * return. Actual execution is not rounded.
   * @param[in] start_time Planned job-start timestamp.
   * @param[in] run_time_estimate Time-limit estimate of the job.
   * @return Planned release offset in seconds.
   */
  tdiff_t release_estimate(sim_time_t start_time,
                           tdiff_t run_time_estimate) const {
    sim_time_t release_time = start_time + run_time_estimate;
    if (m_planning_time_resolution != 0.0) {
      release_time = std::ceil(release_time / m_planning_time_resolution) *
                     m_planning_time_resolution;
    }
    return release_time - start_time;
  }

  /**
   * @brief Find where an EASY candidate scan stops under a queue depth.
   * @details Implements --backfill_queue_depth for the index-based EASY scans:
   * the blocked head consumes one slot when its reservation is planned
   * (inside the planning horizon), and every waiting job behind it that the
   * scan visits consumes one, whether or not it can start. Lazily removed
   * entries are skipped and consume nothing. Because a candidate is counted
   * before it is tested, the end index can be computed before the scan, so
   * the scan loop itself is unchanged and an unlimited depth costs nothing.
   * @tparam Queue Random-access container whose entries have a `removed` flag.
   * @param[in] queue Wait queue in scheduling order.
   * @param[in] begin First candidate index (the entry after the head).
   * @param[in] end One past the last eligible index.
   * @param[in] queue_depth Jobs examined per pass, head included; zero is
   * unlimited.
   * @param[in] head_planned Whether the blocked head's reservation is inside
   * the planning horizon and therefore consumes a slot.
   * @return Index at which the candidate scan stops, in [begin, end].
   */
  template <typename Queue>
  static size_t queue_depth_scan_end(const Queue &queue, size_t begin,
                                     size_t end, size_t queue_depth,
                                     bool head_planned) {
    if (queue_depth == 0) {
      return end;
    }
    size_t remaining = queue_depth - (head_planned ? 1 : 0);
    size_t i = begin;
    for (; i < end && remaining > 0; ++i) {
      if (!queue[i].removed) {
        --remaining;
      }
    }
    return i;
  }

  /**
   * @brief Publish the scheduled capacity outlook used by reservations.
   * @details
   * The simulation calls this before every scheduling pass. Projections
   * start from the current scheduled capacity and the nodes occupied now,
   * and apply the scheduled changes in [begin, end) in time order as signed
   * deltas alongside running-job releases. The range must stay valid until
   * the next call; it is not copied.
   * @param[in] current_capacity Scheduled capacity in effect now.
   * @param[in] occupied_nodes Nodes allocated or releasing now.
   * @param[in] begin First scheduled change not yet applied.
   * @param[in] end One past the last scheduled change.
   */
  void set_capacity_outlook(num_nodes_t current_capacity,
                            num_nodes_t occupied_nodes,
                            const Capacity_Change *begin,
                            const Capacity_Change *end) {
    m_outlook_capacity = current_capacity;
    m_outlook_occupied = occupied_nodes;
    m_outlook_changes_begin = begin;
    m_outlook_changes_end = end;
  }

  /**
   * @brief Project when the first FCFS job can obtain its requested nodes.
   * @details
   * Starts with currently free nodes and walks a timeline of projected
   * running-job releases merged with the scheduled capacity changes
   * published by set_capacity_outlook(): a release adds its nodes, a
   * capacity reduction subtracts, an increase adds. Events sharing a
   * timestamp are netted before the request is tested, as the simulation
   * applies them before scheduling at that time. While the system is
   * overcommitted after a reduction, the walk starts from the negative
   * slack rather than from zero free nodes. The result is used both for the
   * head reservation and to decide whether a backfill candidate can run
   * without delaying that reservation.
   * @param[in] nodes_needed Nodes requested by that job.
   * @param[in] free_nodes Nodes available at current_time.
   * @param[in] running_jobs Active jobs with the metadata needed to project
   * release times.
   * @param[in] current_time Time from which to project releases.
   * @param[in] projected_starts Jobs selected during this scheduler pass that
   * are not yet present in running_jobs.
   * @return Earliest projected start time for the FCFS head, in sim_time_t,
   * or the maximum sim_time_t when no release or scheduled change known to
   * the outlook ever satisfies the request.
   */
  sim_time_t
  calculate_fcfs_reservation(num_nodes_t nodes_needed, num_nodes_t free_nodes,
                             const running_jobs_t &running_jobs,
                             sim_time_t current_time,
                             const projected_starts_t &projected_starts = {});

  /**
   * @brief Build the free-node profile shared by reservation searches.
   * @details Merges running-job releases and scheduled capacity changes into
   * a resource-time timeline beginning at current_time. Callers can use the
   * returned timeline first to find a protected reservation and then extend
   * that same timeline while admitting backfill jobs, avoiding a duplicate
   * reconstruction of the release profile within one scheduling pass. Release
   * and capacity events are first collected in contiguous storage, then
   * coalesced into the returned ordered profile; no intermediate delta map is
   * allocated.
   * @param[in] free_nodes Nodes available at current_time.
   * @param[in] running_jobs Active jobs used to project release times.
   * @param[in] current_time Time from which releases are projected.
   * @param[in] projected_starts Jobs selected in this pass but not yet
   * published to running_jobs. Their releases are included without copying
   * the authoritative running-job map.
   * @return Free-node timeline initialized at current_time.
   */
  Reservation_Timeline
  reservation_timeline(num_nodes_t free_nodes,
                       const running_jobs_t &running_jobs,
                       sim_time_t current_time,
                       const projected_starts_t &projected_starts = {}) const;
};

/**
 * @brief Construct the scheduler selected by the configured policies.
 * @details
 * Selects an FCFS implementation when FCFS is requested (including the
 * queue implementation and overflow settings), the hybrid FCFS
 * implementation when required by the backfill policy, or the SJF/LJF
 * priority scheduler. Scheduling fields are copied into each implementation's
 * wait queue as jobs are submitted; the scheduler does not retain a Trace.
 * @param[in] total_nodes Total nodes available to schedule.
 * @param[in] initial_job_count Number of initially known jobs, used only to
 * select the default circular-queue capacity.
 * @param[in] backfill_policy Backfilling policy.
 * @param[in] priority_policy Job-selection policy.
 * @param[in] queue_impl Wait-queue implementation for FCFS scheduling.
 * @param[in] block_size Block size when queue_impl selects a block queue.
 * @param[in] wait_queue_capacity Initial circular-queue capacity; zero selects
 * a default.
 * @param[in] wait_queue_overflow Behavior when a circular queue is full.
 * @param[in] reservation_depth Waiting jobs that hold reservations under
 * HYBRID backfilling; must be positive with HYBRID and is ignored otherwise.
 * @param[in] backfill_queue_depth Eligible waiting jobs, head included,
 * examined per backfill pass by every scheduler; zero is unlimited.
 * @param[in] backfill_interval Seconds between additional periodic backfill
 * evaluations; zero retains event-driven evaluation only. The planning
 * horizon is not a construction parameter; set it with
 * SchedulerBase::set_planning_horizon().
 * @param[in] dispatch_interval Fixed periodic FCFS/backfill tick interval; zero
 * disables. Mutually exclusive with positive backfill_interval.
 * @param[in] dispatch_queue_depth Entire candidate prefix per dispatch wake,
 * including FCFS starts; zero is unlimited. Requires dispatch_interval > 0.
 * @param[in] dispatch_on_events Also request the FCFS/backfill pass on job
 * arrival batches without changing the timer cadence; requires
 * dispatch_interval > 0.
 * @param[in] per_pass_accounting Use event-driven placement passes with
 * per-pass Q/R counters and guarded reservation-only restarts.
 * @param[in] diagnostic_no_reservations Select the diagnostic periodic model
 * that considers later fitting jobs without protecting future capacity.
 * @param[in,out] resource_diagnostics Optional counter owner, which must
 * outlive the returned scheduler. Only CapacityModel-enabled traversals observe
 * fitting/reservations; other policies retain their existing resource calls.
 * @return Owning pointer to a SchedulerBase implementation.
 * @throws std::invalid_argument if HYBRID is requested with a zero depth or
 * with a non-FCFS priority policy.
 */
std::unique_ptr<SchedulerBase> create_scheduler(
    num_nodes_t total_nodes, size_t initial_job_count,
    BackfillPolicy backfill_policy, PriorityPolicy priority_policy,
    QueueImplementation queue_impl = QueueImplementation::CIRCULAR,
    size_t block_size = 128, size_t wait_queue_capacity = 0,
    CircularOverflowPolicy wait_queue_overflow = CircularOverflowPolicy::GROW,
    size_t reservation_depth = 0, size_t backfill_queue_depth = 0,
    tdiff_t backfill_interval = 0.0, tdiff_t dispatch_interval = 0.0,
    size_t dispatch_queue_depth = 0, bool dispatch_on_events = false,
    bool per_pass_accounting = false, bool diagnostic_no_reservations = false,
    ResourceDiagnostics *resource_diagnostics = nullptr);

/**@}*/

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_BASE_HPP
