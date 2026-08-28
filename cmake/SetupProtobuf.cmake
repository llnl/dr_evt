# Setup Protobuf using FetchContent or system installation
cmake_minimum_required(VERSION 3.16)

find_package(Threads REQUIRED)

# Try to find system-installed Protobuf first
option(protobuf_MODULE_COMPATIBLE TRUE)
find_package(Protobuf CONFIG QUIET)

if(NOT Protobuf_FOUND)
  # Protobuf not found, use FetchContent to download and build
  message(STATUS "Installing Protobuf via FetchContent.")
  include(FetchContent)

  FetchContent_Declare(
    protobuf
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG        v3.21.12
    SOURCE_SUBDIR  cmake)

  set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(protobuf)

  # Create alias for compatibility with find_package(Protobuf)
  if(NOT TARGET protobuf::libprotobuf)
    add_library(protobuf::libprotobuf ALIAS libprotobuf)
  endif()
  if(NOT TARGET protobuf::protoc)
    add_executable(protobuf::protoc ALIAS protoc)
  endif()

  # Set variables for use in the project
  set(_PROTOBUF_LIBPROTOBUF libprotobuf)
  if(CMAKE_CROSSCOMPILING)
    find_program(_PROTOBUF_PROTOC protoc)
  else()
    set(_PROTOBUF_PROTOC $<TARGET_FILE:protoc>)
  endif()

  set(DR_EVT_HAS_PROTOBUF TRUE CACHE BOOL "Protobuf available" FORCE)
  message(STATUS "Protobuf installed via FetchContent")
else()
  # Use system-installed Protobuf
  message(STATUS "Using system protobuf ${Protobuf_VERSION}")

  set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
  if(CMAKE_CROSSCOMPILING)
    find_program(_PROTOBUF_PROTOC protoc)
  else()
    set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
  endif()

  set(DR_EVT_HAS_PROTOBUF TRUE CACHE BOOL "Protobuf available" FORCE)
  message(STATUS "Using system Protobuf")
endif()
