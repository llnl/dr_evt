# Private CMake injection for the unpublished EASYPower experiment.
# Load with -DCMAKE_PROJECT_INCLUDE=<this-file>; the public CMake files remain
# unaware of these targets and sources.
if(NOT TARGET easypower-experiment-bin)
  add_executable(easypower-experiment-bin
    "${CMAKE_SOURCE_DIR}/experimental/fugaku-power/easypower_experiment.cpp"
    "${CMAKE_SOURCE_DIR}/src/sim/scheduler_easy_power.cpp")
  target_include_directories(easypower-experiment-bin PUBLIC
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_SOURCE_DIR}/src")
  target_compile_features(easypower-experiment-bin PRIVATE cxx_std_20)
  target_link_libraries(easypower-experiment-bin PRIVATE dr_evt)
  set_target_properties(easypower-experiment-bin PROPERTIES
    OUTPUT_NAME easypower_experiment)
endif()
