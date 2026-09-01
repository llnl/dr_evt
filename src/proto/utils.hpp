/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef  DR_EVT_PROTO_UTILS_HPP
#define  DR_EVT_PROTO_UTILS_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#if !defined(DR_EVT_HAS_PROTOBUF)
#error DR_EVT requires protocol buffer
#endif

#include <string>
#include <iostream>
#include <fstream>
#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/message.h>

#if defined(DR_EVT_HAS_ABSL_LOG_SINK)
#include "absl/log/log_sink.h"
#include "absl/log/log_sink_registry.h"
#endif

namespace dr_evt {
/** \addtogroup dr_evt_proto
 *  @{ */

#if defined(DR_EVT_HAS_ABSL_LOG_SINK)
/// Redirects Protobuf's own internal parse diagnostics (malformed
/// fields, unknown fields, type mismatches) to stderr with a
/// DR_EVT-specific prefix. Protobuf's internal logging has used
/// Abseil's absl::LogSink mechanism since Protobuf's own migration
/// away from its now-removed SetLogHandler/LogLevel API (see
/// DR_EVT_HAS_PROTOBUF_LOG_HANDLER's own detection logic in the root
/// CMakeLists.txt) - hooking into Abseil's own, generic log-sink
/// registry works regardless of Protobuf version, unlike the removed,
/// Protobuf-specific SetLogHandler API this replaces.
class ProtoLogSink : public absl::LogSink {
public:
  void Send(const absl::LogEntry& entry) override
  {
    std::cerr << "DR_EVT proto: "
              << entry.text_message_with_prefix_and_newline();
  }
};

/// RAII guard: registers a ProtoLogSink on construction, unregisters it
/// on destruction - guarantees it's always unregistered on every exit
/// path (early return, exception, or normal completion) without
/// needing a matching absl::RemoveLogSink() call at each one, which is
/// easy to miss on future edits and wouldn't run at all if an
/// exception unwound through the function instead of an explicit
/// return.
class ScopedProtoLogSink {
public:
  ScopedProtoLogSink() { absl::AddLogSink(&m_sink); }
  ~ScopedProtoLogSink() { absl::RemoveLogSink(&m_sink); }
  ScopedProtoLogSink(const ScopedProtoLogSink&) = delete;
  ScopedProtoLogSink& operator=(const ScopedProtoLogSink&) = delete;
private:
  ProtoLogSink m_sink;
};
#elif defined(DR_EVT_HAS_PROTOBUF_LOG_HANDLER)
// Older Protobuf generations logged via their own SetLogHandler API
// instead of Abseil's - kept only as a fallback for a Protobuf old
// enough to have SetLogHandler but built without the newer,
// Abseil-based logging DR_EVT_HAS_ABSL_LOG_SINK detects (preferred
// when both happen to be available, see the #if chain above).
void pbuf_log_collector(
       google::protobuf::LogLevel level,
       const char* filename,
       int line,
       const std::string& message);
#endif // DR_EVT_HAS_ABSL_LOG_SINK / DR_EVT_HAS_PROTOBUF_LOG_HANDLER

template<typename T>
bool read_prototext(const std::string& file_name, const bool is_binary,
                    T& dr_evt_proto_params)
{
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  std::ifstream input(file_name, std::ios::in | std::ios::binary);

#if defined(DR_EVT_HAS_ABSL_LOG_SINK)
  ScopedProtoLogSink dr_evt_log_sink_guard;
#elif defined(DR_EVT_HAS_PROTOBUF_LOG_HANDLER)
  google::protobuf::SetLogHandler(pbuf_log_collector);
#endif
  // Without either mechanism (neither the modern Abseil-based one nor
  // the removed, older SetLogHandler one), Protobuf's own internal
  // parse diagnostics still reach stderr via its current default
  // logging path - just without this custom message format.

  if (!input) {
    std::cerr << file_name << ": File not found!" << std::endl;
    return false;
  }
  if (is_binary) {
    if (!dr_evt_proto_params.ParseFromIstream(&input)) {
      std::cerr << "Failed to parse DR_EVT_Params in binary-formatted input file: "
                << file_name << std::endl;
      return false;
    }
  } else {
    google::protobuf::io::IstreamInputStream istrm(&input);
    if (!google::protobuf::TextFormat::Parse(&istrm, &dr_evt_proto_params)) {
      std::cerr << "Failed to parse DR_EVT_Params in text-formatted input file: "
                << file_name << std::endl;
      return false;
    }
  }
  return true;
}

/**@}*/
} // end of namespace dr_evt
#endif //  DR_EVT_PROTO_UTILS_HPP
