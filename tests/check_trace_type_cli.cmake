if (NOT DEFINED SIMULATOR)
  message(FATAL_ERROR "SIMULATOR is required")
endif()
if (NOT DEFINED INPUT)
  message(FATAL_ERROR "INPUT is required")
endif()
if (NOT DEFINED WORK_DIR)
  message(FATAL_ERROR "WORK_DIR is required")
endif()

set(PCON_OUTPUT "${WORK_DIR}/test_trace_type_pcon.csv")
set(PCON_RESOURCES "${WORK_DIR}/test_trace_type_pcon_resources.csv")
set(STANDARD_OUTPUT "${WORK_DIR}/test_trace_type_standard.csv")
set(STANDARD_RESOURCES "${WORK_DIR}/test_trace_type_standard_resources.csv")

file(REMOVE
  "${PCON_OUTPUT}"
  "${PCON_RESOURCES}"
  "${STANDARD_OUTPUT}"
  "${STANDARD_RESOURCES}")

execute_process(
  COMMAND "${SIMULATOR}"
    --trace_type pcon
    --trace_format simple
    --timestamp_format epoch
    --run_time_mode limit
    --total_nodes 4
    --outfile "${PCON_OUTPUT}"
    --resource_trace "${PCON_RESOURCES}"
    "${INPUT}"
  RESULT_VARIABLE PCON_RESULT
  OUTPUT_VARIABLE PCON_STDOUT
  ERROR_VARIABLE PCON_STDERR)

if (NOT PCON_RESULT EQUAL 0)
  message(FATAL_ERROR
    "Pcon simulator invocation failed (${PCON_RESULT})\n"
    "stdout:\n${PCON_STDOUT}\n"
    "stderr:\n${PCON_STDERR}")
endif()

file(READ "${PCON_RESOURCES}" PCON_CONTENTS)
string(FIND "${PCON_CONTENTS}"
  "time,free_nodes,allocated_nodes,avgpcon,minpcon,maxpcon"
  PCON_HEADER_POS)
if (PCON_HEADER_POS EQUAL -1)
  message(FATAL_ERROR
    "Pcon resource trace does not contain Pcon columns:\n${PCON_CONTENTS}")
endif()

# The same Pcon-shaped input must fail under the default standard trace type:
# standard parsing must not silently acquire knowledge of experimental fields.
# Use a standard-shaped temporary input to verify default dispatch separately.
set(STANDARD_INPUT "${WORK_DIR}/test_trace_type_standard_input.csv")
file(WRITE "${STANDARD_INPUT}"
  "job_submit_time,num_nodes,q_id,time_limit\n"
  "0,2,1,3\n"
  "0,1,1,1\n")

execute_process(
  COMMAND "${SIMULATOR}"
    --trace_format simple
    --timestamp_format epoch
    --run_time_mode limit
    --total_nodes 4
    --outfile "${STANDARD_OUTPUT}"
    --resource_trace "${STANDARD_RESOURCES}"
    "${STANDARD_INPUT}"
  RESULT_VARIABLE STANDARD_RESULT
  OUTPUT_VARIABLE STANDARD_STDOUT
  ERROR_VARIABLE STANDARD_STDERR)

if (NOT STANDARD_RESULT EQUAL 0)
  message(FATAL_ERROR
    "Standard simulator invocation failed (${STANDARD_RESULT})\n"
    "stdout:\n${STANDARD_STDOUT}\n"
    "stderr:\n${STANDARD_STDERR}")
endif()

file(READ "${STANDARD_RESOURCES}" STANDARD_CONTENTS)
string(FIND "${STANDARD_CONTENTS}"
  "time,free_nodes,allocated_nodes\n"
  STANDARD_HEADER_POS)
if (STANDARD_HEADER_POS EQUAL -1)
  message(FATAL_ERROR
    "Standard resource trace has an unexpected header:\n${STANDARD_CONTENTS}")
endif()
string(FIND "${STANDARD_CONTENTS}" "avgpcon" STANDARD_PCON_POS)
if (NOT STANDARD_PCON_POS EQUAL -1)
  message(FATAL_ERROR
    "Standard resource trace unexpectedly contains Pcon columns:\n"
    "${STANDARD_CONTENTS}")
endif()

execute_process(
  COMMAND "${SIMULATOR}" --trace_type invalid "${STANDARD_INPUT}"
  RESULT_VARIABLE INVALID_RESULT
  OUTPUT_QUIET
  ERROR_QUIET)
if (INVALID_RESULT EQUAL 0)
  message(FATAL_ERROR "Invalid --trace_type value unexpectedly succeeded")
endif()

file(REMOVE "${STANDARD_INPUT}")
