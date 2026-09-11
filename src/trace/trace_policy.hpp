/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file trace_policy.hpp
 * @brief Compile-time storage policies for standard and experimental traces.
 */

#ifndef DR_EVT_TRACE_TRACE_POLICY_HPP
#define DR_EVT_TRACE_TRACE_POLICY_HPP

#include "trace/job_record.hpp"
#include <boost/circular_buffer.hpp>
#include <string>
#include <vector>

namespace dr_evt {

class Data_Columns;

/** Values carried by one job in the Pcon experiment. */
struct Pcon_Values {
  double avgpcon = 0.0;
  double minpcon = 0.0;
  double maxpcon = 0.0;
};

/**
 * Experimental record stored by value. Inheritance lets the scheduling core
 * continue to consume Job_Record references while the concrete buffer retains
 * the additional fields. No pointer or dynamically-sized payload is added to
 * an individual record.
 */
class Pcon_Job_Record : public Job_Record {
public:
  using Job_Record::Job_Record;

  Pcon_Job_Record(Job_Record &&job, const Pcon_Values &pcon = {})
      : Job_Record(std::move(job)), m_pcon(pcon) {}

  const Pcon_Values &pcon() const { return m_pcon; }

private:
  Pcon_Values m_pcon;
};

/** Standard resource-history entry; preserves the existing representation. */
struct Standard_Resource_Sample {
  epoch_t time;
  num_nodes_t allocated;
};

/** Resource-history entry emitted by the Pcon experiment. */
struct Pcon_Resource_Sample {
  epoch_t time;
  num_nodes_t allocated;
  Pcon_Values pcon;
};

struct Standard_Trace_Policy {
  using record_type = Job_Record;
  using job_store_type = boost::circular_buffer<record_type>;
  using resource_sample_type = Standard_Resource_Sample;

  static int load_records(const std::string &fname, const Data_Columns &dcols,
                          std::vector<record_type> &records,
                          num_jobs_t max_count);
  static record_type make_record(const epoch_t &submit_time,
                                 num_nodes_t num_nodes, job_queue_t queue,
                                 timeout_t limit_time) {
    return record_type(submit_time, num_nodes, queue, limit_time);
  }

  static resource_sample_type sample(const epoch_t &time,
                                     num_nodes_t allocated) {
    return {time, allocated};
  }
  static const char *resource_columns() { return ""; }
  static std::string resource_values(const resource_sample_type &) {
    return "";
  }
  void on_start(const record_type &) {}
  void on_finish(const record_type &) {}
};

struct Pcon_Trace_Policy {
  using record_type = Pcon_Job_Record;
  using job_store_type = boost::circular_buffer<record_type>;
  using resource_sample_type = Pcon_Resource_Sample;

  static int load_records(const std::string &fname, const Data_Columns &dcols,
                          std::vector<record_type> &records,
                          num_jobs_t max_count);
  static record_type make_record(const epoch_t &submit_time,
                                 num_nodes_t num_nodes, job_queue_t queue,
                                 timeout_t limit_time) {
    return record_type(Job_Record(submit_time, num_nodes, queue, limit_time));
  }

  resource_sample_type sample(const epoch_t &time,
                              num_nodes_t allocated) const {
    return {time, allocated, m_current};
  }
  static const char *resource_columns() { return ",avgpcon,minpcon,maxpcon"; }
  static std::string resource_values(const resource_sample_type &sample) {
    return "," + std::to_string(sample.pcon.avgpcon) + "," +
           std::to_string(sample.pcon.minpcon) + "," +
           std::to_string(sample.pcon.maxpcon);
  }
  void on_start(const record_type &job) {
    const auto &pcon = job.pcon();
    m_current.avgpcon += pcon.avgpcon;
    m_current.minpcon += pcon.minpcon;
    m_current.maxpcon += pcon.maxpcon;
  }
  void on_finish(const record_type &job) {
    const auto &pcon = job.pcon();
    m_current.avgpcon -= pcon.avgpcon;
    m_current.minpcon -= pcon.minpcon;
    m_current.maxpcon -= pcon.maxpcon;
  }

private:
  Pcon_Values m_current;
};

} // namespace dr_evt

#endif // DR_EVT_TRACE_TRACE_POLICY_HPP
