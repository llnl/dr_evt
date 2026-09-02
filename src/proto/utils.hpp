/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
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
/// DR_EVT-specific prefix, via Abseil's absl::LogSink registry.
class ProtoLogSink : public absl::LogSink {
public:
  void Send(const absl::LogEntry& entry) override
  {
    std::cerr << "DR_EVT proto: "
              << entry.text_message_with_prefix_and_newline();
  }
};

/// RAII guard: registers a ProtoLogSink on construction, unregisters it
/// on destruction, so it's removed on every exit path (return,
/// exception, or normal completion).
class ScopedProtoLogSink {
public:
  ScopedProtoLogSink() { absl::AddLogSink(&m_sink); }
  ~ScopedProtoLogSink() { absl::RemoveLogSink(&m_sink); }
  ScopedProtoLogSink(const ScopedProtoLogSink&) = delete;
  ScopedProtoLogSink& operator=(const ScopedProtoLogSink&) = delete;
private:
  ProtoLogSink m_sink;
};
#endif // DR_EVT_HAS_ABSL_LOG_SINK

template<typename T>
bool read_prototext(const std::string& file_name, const bool is_binary,
                    T& dr_evt_proto_params)
{
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  std::ifstream input(file_name, std::ios::in | std::ios::binary);

#if defined(DR_EVT_HAS_ABSL_LOG_SINK)
  ScopedProtoLogSink dr_evt_log_sink_guard;
#endif
  // Without this, Protobuf's own default logging still reaches
  // stderr, just without this custom message format.

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
