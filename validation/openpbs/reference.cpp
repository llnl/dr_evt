/**
 * @file reference.cpp
 * @brief Replay jobs through the unmodified OpenPBS scheduler library.
 *
 * Linker wrappers emulate server I/O, job execution, and time. Native job
 * selection, vnode placement, and future-capacity planning remain unchanged.
 * Each job owns a private vnode and consumes units from a shared server pool.
 * The input CSV contains id, arrival, units, actual duration, and time limit;
 * times are integer seconds relative to the replay's fixed epoch.
 */
#include "fifo.h"
#include "globals.h"
#include "multi_threading.h"
#include <attribute.h>
#include <libpbs.h>
#include <log.h>
#include <pbs_config.h>
#include <pbs_ifl.h>
#include <pbs_internal.h>
#include <sched_cmds.h>

#include "native_resource_definitions.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

/** @brief Input request and emulated execution state for one job. */
struct InputJob {
  int id;          ///< One-based position in stable arrival order.
  long submit;     ///< Arrival time in seconds relative to epoch.
  long units;      ///< Requested shared-capacity units.
  long duration;   ///< Actual execution duration in seconds.
  long limit;      ///< Requested walltime used by native forecasts, in seconds.
  long start = -1; ///< Actual start relative to epoch; -1 means not started.
  bool done = false;      ///< Whether actual execution has completed.
  std::string assignment; ///< Native exec_vnode string recorded at launch.
};
static std::vector<InputJob> jobs; ///< All jobs in stable arrival order.
static long now =
    1700000000; ///< Frozen absolute time during each native cycle.
static long epoch = 1700000000; ///< Absolute origin for relative input times.
static long capacity;           ///< Total server-wide capacity.
static int protection;          ///< Native backfill protection depth.
static std::ofstream
    events; ///< Launch, completion, forecast, and capacity log.

/** @brief Name a private vnode. @param job Job owning the vnode. */
static std::string vnode(const InputJob &job) {
  return "private" + std::to_string(job.id);
}
/** @brief Construct a native PBS identifier. @param job Input job. */
static std::string jobid(const InputJob &job) {
  return std::to_string(job.id) + ".validation";
}
/** @brief Check live occupancy. @param job Job whose state is examined. */
static bool running(const InputJob &job) { return job.start >= 0 && !job.done; }
/** @brief Sum capacity consumed by all currently running jobs. */
static long allocated() {
  long used = 0;
  for (const auto &job : jobs)
    if (running(job))
      used += job.units;
  return used;
}
/** @brief Allocate a status object freed by libpbs. @param name Object name. */
static batch_status *object(const std::string &name) {
  auto *result = static_cast<batch_status *>(calloc(1, sizeof(batch_status)));
  result->name = strdup(name.c_str());
  return result;
}
/**
 * @brief Append an attribute to a native status object.
 * @param object Status object owning the attribute list.
 * @param name Native attribute name.
 * @param value Attribute value copied into libpbs-owned storage.
 * @param resource Resource name, or nullptr for a non-resource attribute.
 */
static void attr(batch_status *object, const char *name,
                 const std::string &value, const char *resource = nullptr) {
  auto *a = static_cast<attrl *>(calloc(1, sizeof(attrl)));
  a->name = strdup(name);
  a->value = strdup(value.c_str());
  a->resource = resource ? strdup(resource) : nullptr;
  auto **tail = &object->attribs;
  while (*tail)
    tail = &(*tail)->next;
  *tail = a;
}
/**
 * @brief Append @p entry and advance @p tail to its next link.
 * @param tail End link of the native status list, updated in place.
 * @param entry Status object whose ownership transfers to the list.
 */
static void append(batch_status **&tail, batch_status *entry) {
  *tail = entry;
  tail = &entry->next;
}
/** @brief Return frozen replay time. @param out Optional result destination. */
extern "C" time_t __wrap_time(time_t *out) {
  if (out)
    *out = now;
  return now;
}
/** @brief Supply shared server capacity, live allocation, and protection depth.
 */
extern "C" batch_status *__wrap_pbs_statserver(int, attrl *, const char *) {
  auto *s = object("validation");
  attr(s, ATTR_rescavail, std::to_string(capacity), "capacity");
  attr(s, ATTR_rescassn, std::to_string(allocated()), "capacity");
  attr(s, ATTR_backfill_depth, std::to_string(protection));
  return s;
}
/** @brief Supply a single enabled execution queue. */
extern "C" batch_status *__wrap_pbs_statque(int, const char *, attrl *,
                                            const char *) {
  auto *q = object("workq");
  attr(q, ATTR_qtype, "Execution");
  attr(q, ATTR_enable, "True");
  attr(q, ATTR_start, "True");
  return q;
}
/** @brief Supply private vnodes for arrived jobs that have not completed. */
extern "C" batch_status *__wrap_pbs_statvnode(int, const char *, attrl *,
                                              const char *) {
  batch_status *head = nullptr;
  auto **tail = &head;
  for (const auto &job : jobs) {
    // Future/finished private vnodes cannot serve any other job. Register
    // them with arrival and retire them after completion to bound snapshots.
    if (job.submit + epoch > now || job.done)
      continue;
    auto *n = object(vnode(job));
    attr(n, ATTR_NODE_state, "free");
    attr(n, ATTR_NODE_Mom, vnode(job));
    attr(n, ATTR_NODE_ntype, "PBS");
    attr(n, ATTR_NODE_Sharing, "default_shared");
    attr(n, ATTR_NODE_License, "l");
    attr(n, ATTR_rescavail, "1", "ncpus");
    attr(n, ATTR_rescavail, "1gb", "mem");
    attr(n, ATTR_rescavail, vnode(job), "host");
    attr(n, ATTR_rescavail, vnode(job), "vnode");
    attr(n, ATTR_rescavail, "linux", "arch");
    attr(n, ATTR_rescassn, running(job) ? "1" : "0", "ncpus");
    if (running(job))
      attr(n, ATTR_NODE_jobs, jobid(job));
    append(tail, n);
  }
  return head;
}
/** @brief Supply queued/running jobs, requests, queue ranks, and execution
 * state. */
extern "C" batch_status *__wrap_pbs_selstat(int, attropl *, attrl *,
                                            const char *) {
  batch_status *head = nullptr;
  auto **tail = &head;
  for (const auto &job : jobs) {
    if (job.submit + epoch > now || job.done)
      continue;
    auto *j = object(jobid(job));
    attr(j, ATTR_N, jobid(job));
    attr(j, ATTR_q, "workq");
    attr(j, ATTR_state, running(job) ? "R" : "Q");
    attr(j, ATTR_substate, running(job) ? "42" : "10");
    attr(j, ATTR_euser, "validation");
    attr(j, ATTR_egroup, "validation");
    attr(j, ATTR_qrank, std::to_string(job.id));
    attr(j, ATTR_etime, std::to_string(epoch + job.submit));
    attr(j, ATTR_SchedSelect, "1:ncpus=1:vnode=" + vnode(job));
    attr(j, ATTR_l, "1:ncpus=1:vnode=" + vnode(job), "select");
    attr(j, ATTR_l, "free", "place");
    attr(j, ATTR_l, "1", "ncpus");
    attr(j, ATTR_l, std::to_string(job.limit), "walltime");
    attr(j, ATTR_l, std::to_string(job.units), "capacity");
    if (running(job)) {
      attr(j, ATTR_stime, std::to_string(epoch + job.start));
      attr(j, ATTR_execvnode, job.assignment);
      attr(j, ATTR_used, std::to_string(now - epoch - job.start), "walltime");
    }
    append(tail, j);
  }
  pbs_errno = 0;
  return head;
}
/** @brief Report no administrative reservations; backfill forecasts stay
 * native. */
extern "C" batch_status *__wrap_pbs_statresv(int, const char *, attrl *,
                                             const char *) {
  pbs_errno = 0;
  return nullptr;
}
/** @brief Select OpenPBS's finest forecast granularity, opt_backfill_fuzzy=off.
 */
extern "C" batch_status *__wrap_pbs_statsched(int, attrl *, const char *) {
  auto *s = object("default");
  attr(s, ATTR_opt_backfill_fuzzy, "off");
  return s;
}
/** @brief Supply native resource definitions plus job-wide consumable capacity.
 */
extern "C" batch_status *__wrap_pbs_statrsc(int, const char *, attrl *,
                                            const char *) {
  batch_status *head = nullptr;
  auto **tail = &head;
  for (const auto &definition : native_definitions) {
    auto *r = object(definition.name);
    attr(r, ATTR_RESC_TYPE, std::to_string(definition.type));
    attr(r, ATTR_RESC_FLAG, std::to_string(definition.flags));
    append(tail, r);
  }
  auto *r = object("capacity");
  attr(r, ATTR_RESC_TYPE, std::to_string(ATR_TYPE_LONG));
  attr(r, ATTR_RESC_FLAG, std::to_string(ATR_DFLAG_RASSN));
  append(tail, r);
  return head;
}
/**
 * @brief Validate and acknowledge a native launch, then record actual
 * occupancy.
 * @param id Native job identifier.
 * @param exec Native vnode assignment; must select the job's private vnode.
 * @return Zero on a valid launch.
 * @throws std::runtime_error For unknown jobs, duplicate/premature launches,
 *         incorrect vnode assignments, or capacity overflow.
 */
extern "C" int __wrap_pbs_asyrunjob(int, const char *id, const char *exec,
                                    const char *) {
  for (auto &job : jobs)
    if (jobid(job) == id) {
      if (job.start >= 0 || job.submit + epoch > now ||
          allocated() + job.units > capacity)
        throw std::runtime_error("invalid or over-capacity native launch");
      job.assignment = exec;
      if (job.assignment != "(" + vnode(job) + ":ncpus=1)")
        throw std::runtime_error(
            "native launch did not use its private vnode: " + job.assignment);
      job.start = now - epoch;
      events << "start," << now - epoch << ',' << job.id << ',' << allocated()
             << '\n';
      return 0;
    }
  throw std::runtime_error("unknown native launch");
}
/**
 * @brief Record native estimated start times without altering scheduling state.
 * @param id Native job identifier associated with the update.
 * @param attrs Native attribute updates; only estimated.start_time is logged.
 * @return Zero to acknowledge the update.
 */
extern "C" int __wrap_pbs_asyalterjob(int, const char *id, attrl *attrs,
                                      const char *) {
  for (auto *a = attrs; a; a = a->next)
    if (std::string(a->name) == ATTR_estimated && a->resource &&
        std::string(a->resource) == "start_time")
      events << "plan," << now - epoch << ',' << id << ','
             << atol(a->value) - epoch << '\n';
  return 0;
}
/** @brief Report no emulated server error. */
extern "C" char *__wrap_pbs_geterrmsg(int) { return nullptr; }
/** @brief Report no pending interruption while a native cycle runs. */
extern "C" int __wrap__Z19get_sched_cmd_noblkiP9sched_cmd(int, sched_cmd *) {
  return 0;
}

/**
 * @brief Replay arrivals and actual completions through native scheduling
 * cycles.
 * @param argc Must be six, including the executable name.
 * @param argv Executable, input CSV, total capacity, protection depth, schedule
 *             output CSV, and event output CSV.
 * @return Zero after all jobs complete; one after a reported validation error.
 *
 * All arrivals and completions at one timestamp precede its scheduling cycle.
 * The next cycle occurs at the next arrival or actual completion. Native
 * forecasts use requested walltime, so early completion triggers replanning.
 */
int main(int argc, char **argv) {
  try {
    if (argc != 6)
      throw std::runtime_error(
          "usage: reference input capacity R output events");
    capacity = std::stol(argv[2]);
    protection = std::stoi(argv[3]);
    std::ifstream input(argv[1]);
    if (!input)
      throw std::runtime_error("cannot open input");
    std::string line;
    while (std::getline(input, line)) {
      std::replace(line.begin(), line.end(), ',', ' ');
      std::istringstream row(line);
      InputJob job;
      if (!(row >> job.id >> job.submit >> job.units >> job.duration >>
            job.limit))
        throw std::runtime_error("invalid input row");
      if (job.submit < 0 || job.units < 1 || job.units > capacity ||
          job.duration < 1 || job.limit < job.duration)
        throw std::runtime_error("invalid input values");
      jobs.push_back(job);
    }
    if (jobs.empty())
      throw std::runtime_error("empty input");
    events.open(argv[5]);
    set_msgdaemonname(const_cast<char *>("openpbs-validation"));
    char directory[4096];
    if (!getcwd(directory, sizeof(directory)))
      throw std::runtime_error("getcwd failed");
    std::string logfile = std::string(directory) + "/native.log";
    if (log_open(const_cast<char *>(logfile.c_str()), directory))
      throw std::runtime_error("native log_open failed");
    log_event_mask = new long(SCHED_LOG_DFLT);
    pbs_conf.pbs_exec_path = strdup("/usr");
    pbs_conf.pbs_home_path = strdup(".");
    dflt_sched = 1;
    sc_name = "default";
    if (!set_validate_sched_attrs(1))
      throw std::runtime_error("native scheduler attributes failed");
    if (schedinit(1))
      throw std::runtime_error("native schedinit failed");
    size_t completed = 0;
    while (completed != jobs.size()) {
      bool arrivals = false;
      for (auto &job : jobs) {
        if (running(job) && epoch + job.start + job.duration == now) {
          job.done = true;
          ++completed;
          events << "end," << now - epoch << ',' << job.id << ',' << allocated()
                 << '\n';
        }
        if (job.submit + epoch == now)
          arrivals = true;
      }
      if (completed == jobs.size())
        break;
      sched_cmd command{};
      command.cmd = arrivals ? SCH_SCHEDULE_NEW : SCH_SCHEDULE_TERM;
      bool active =
          std::any_of(jobs.begin(), jobs.end(), [](const InputJob &job) {
            return job.submit + epoch <= now && !job.done;
          });
      int rc = active ? scheduling_cycle(1, &command) : 0;
      if (rc == -1)
        throw std::runtime_error("native scheduling cycle failed");
      events << "snapshot," << now - epoch << ",0," << allocated() << '\n';
      // Recompute after launches; execution ends at actual duration, while
      // native calendars predict release from requested walltime.
      long next = std::numeric_limits<long>::max();
      for (const auto &job : jobs) {
        if (job.submit + epoch > now)
          next = std::min(next, job.submit + epoch);
        if (running(job))
          next = std::min(next, epoch + job.start + job.duration);
      }
      if (next == std::numeric_limits<long>::max())
        throw std::runtime_error("native queue stalled; inspect native.log");
      now = next;
    }
    std::ofstream output(argv[4]);
    output << "job_id,start,end\n";
    for (const auto &job : jobs)
      output << job.id << ',' << job.start << ',' << job.start + job.duration
             << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
