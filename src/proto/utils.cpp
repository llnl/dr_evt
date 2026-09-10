/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file utils.cpp
 * @brief Protobuf reflection and diagnostic utility implementation.
 */

#include "proto/utils.hpp"
#include "utils/exception.hpp"
#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>
#include <iostream>
#include <string>

namespace dr_evt {

#if !defined(DR_EVT_HAS_ABSL_LOG_SINK) &&                                      \
    defined(DR_EVT_HAS_PROTOBUF_LOG_HANDLER)
// Matches utils.hpp's own #if/#elif chain: the modern, Abseil-based
// ProtoLogSink (preferred when available) is fully defined inline in
// the header, so there's nothing to define here in that case - this
// definition only exists as a fallback for a Protobuf old enough to
// have SetLogHandler but built without Abseil's newer logging.
/** @brief Forward legacy Protobuf diagnostics to standard error.
 * @param[in] level Protobuf severity.
 * @param[in] filename Protobuf source filename.
 * @param[in] line Source line number.
 * @param[in] message Diagnostic text. */
void pbuf_log_collector(google::protobuf::LogLevel level, const char *filename,
                        int line, const std::string &message) {
  std::string errmsg = std::to_string(static_cast<int>(level)) + ' ' +
                       std::string{filename} + ' ' + std::to_string(line) +
                       ' ' + message;
  std::cerr << errmsg << std::endl;
}
#endif // !DR_EVT_HAS_ABSL_LOG_SINK && DR_EVT_HAS_PROTOBUF_LOG_HANDLER

/** @brief Locate the selected field descriptor for a named protobuf oneof.
 * @param[in] msg Message to inspect.
 * @param[in] oneof_name Oneof declaration name.
 * @return Selected field descriptor, or nullptr if no field is selected.
 * @throws dr_evt exception when the named oneof does not exist. */
google::protobuf::FieldDescriptor const *
get_oneof_field_desc(const google::protobuf::Message &msg,
                     const std::string &oneof_name) {
  auto desc = msg.GetDescriptor();
  auto oneof_handle = desc->FindOneofByName(oneof_name);
  if (oneof_handle == nullptr) {
    std::string err_str = "Unable to identify the type 'oneof " + oneof_name +
                          "' in message {" + desc->DebugString() + "}\n";
    DR_EVT_THROW(err_str);
  }
  auto reflex = msg.GetReflection();

  return reflex->GetOneofFieldDescriptor(msg, oneof_handle);
}

/** @brief Report whether a protobuf oneof has a selected field.
 * @param[in] msg Message to inspect.
 * @param[in] oneof_name Oneof declaration name.
 * @return true when a field is selected.
 * @throws dr_evt exception when the named oneof does not exist. */
bool has_oneof(google::protobuf::Message const &msg,
               std::string const &oneof_name) {
  return (get_oneof_field_desc(msg, oneof_name) != nullptr);
}

/** @brief Return the message value selected in a protobuf oneof.
 * @param[in] msg Message to inspect.
 * @param[in] oneof_name Oneof declaration name.
 * @return Const reference to the selected nested protobuf message.
 * @throws dr_evt exception when no field is selected or it is not a message. */
const google::protobuf::Message &
get_oneof_message(const google::protobuf::Message &msg,
                  const std::string &oneof_name) {
  auto oneof_field = get_oneof_field_desc(msg, oneof_name);
  if (oneof_field == nullptr) {
    std::string err_str = "The value of 'oneof " + oneof_name +
                          "' has not been set in message {" +
                          msg.DebugString() + "}\n";
    DR_EVT_THROW(err_str);
  }

  if (oneof_field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
    DR_EVT_THROW("Oneof field is not of message type.");
  }

  return msg.GetReflection()->GetMessage(msg, oneof_field);
}

} // end of namespace dr_evt
