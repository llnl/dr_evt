# Setup gRPC (and, transitively, Protobuf) for the DR_EVT client/server.
#
# Mirrors the standard gRPC-recommended CMake pattern (see
# https://github.com/grpc/grpc/blob/master/examples/cpp/cmake/common.cmake):
# silently check whether gRPC and Protobuf are already installed; if not,
# fetch and build gRPC from source (which bundles a compatible Protobuf).

find_package(Threads REQUIRED)

option(protobuf_MODULE_COMPATIBLE TRUE)

# gRPC first, then Protobuf - and only search Protobuf independently if
# gRPC was actually found. find_package() is idempotent, so whichever
# Protobuf is found first gets "locked in" for the rest of this configure.
# Searching gRPC first lets its own CMake config pull in the Protobuf it
# was actually built against; skipping the Protobuf search entirely when
# gRPC isn't found avoids handing FetchContent's own, bundled gRPC build
# a separate, possibly mismatched Protobuf found beforehand.
find_package(gRPC CONFIG QUIET)

if (gRPC_FOUND)
  find_package(Protobuf CONFIG QUIET)

  if (NOT Protobuf_FOUND)
    # Debian/Ubuntu's protobuf-compiler-grpc ships a CMake config, but the
    # separate libprotobuf-dev package doesn't - fall back to CMake's own
    # bundled FindProtobuf.cmake (MODULE mode), which finds the library/
    # headers/protoc directly and creates the same protobuf::libprotobuf /
    # protobuf::protoc targets CONFIG mode would have.
    find_package(Protobuf MODULE QUIET)
  endif (NOT Protobuf_FOUND)
endif (gRPC_FOUND)

if (NOT Protobuf_FOUND OR NOT gRPC_FOUND)
  set(DR_EVT_GRPC_FETCHCONTENT ON)
else()
  message(STATUS "Found gRPC: ${gRPC_VERSION} (gRPC_DIR: ${gRPC_DIR})")
endif()

if (DR_EVT_GRPC_FETCHCONTENT)
  message(STATUS "gRPC and/or Protobuf not found as installed packages - "
                 "building gRPC (with its own bundled Protobuf) via "
                 "FetchContent instead.")

  include(FetchContent)
  set(ABSL_ENABLE_INSTALL ON)
  set(gRPC_INSTALL ON)

  # Pinned to 1.51.1, not a newer release: this is the version this
  # project's gRPC code was actually written and verified against. A
  # newer release bundles a substantially newer Protobuf generation
  # internally - bumping this pin should come with re-verifying the
  # generated code and API usage against whatever Protobuf that release
  # bundles, not just assuming compatibility.
  #
  # CMAKE_POLICY_VERSION_MINIMUM: gRPC's source tree bundles third-party
  # dependencies (e.g. c-ares) whose CMakeLists.txt still specify a
  # cmake_minimum_required() below 3.5 - recent CMake removed
  # compatibility for that entirely. This applies policies as if 3.5 had
  # been requested for those subprojects, matching what CMake's own
  # resulting error message suggests.
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

  # gRPC's bundled Abseil (its hardware-accelerated random number
  # generator, third_party/abseil-cpp/absl/random) has a real, confirmed
  # bug on Apple builds: absl/copts/AbseilConfigureCopts.cmake
  # unconditionally emits BOTH x86_64 and arm64 SIMD flags (-msse4.1,
  # -maes) on every Apple build - not just genuine universal (multi-
  # arch) ones - via `-Xarch_<arch> <flag>` pairs meant to let a single
  # multi-arch compiler invocation apply each flag only to its own
  # architecture. On a plain single-architecture build, this doesn't
  # reliably suppress the other architecture's flag, and the compiler
  # rejects it outright once it knows the actual (sole) target.
  # Confirmed directly against the actual file after a real build
  # failure - not a guess. The real fix is the PATCH_COMMAND further
  # below, which restricts that loop to the architecture(s) actually
  # being built. This block just gives that patch (and Abseil's own
  # code) an explicit CMAKE_OSX_ARCHITECTURES value to work with, rather
  # than leaving it unset and relying on a CMAKE_SYSTEM_PROCESSOR
  # fallback - without overriding an explicit user choice (e.g. a
  # genuine universal x86_64+arm64 build).
  if (APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    execute_process(COMMAND uname -m OUTPUT_VARIABLE DR_EVT_HOST_ARCH
                     OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CMAKE_OSX_ARCHITECTURES "${DR_EVT_HOST_ARCH}")
    unset(DR_EVT_HOST_ARCH)
  endif()

  # This project builds shared libraries by default (BUILD_SHARED_LIBS
  # ON at the top of CMakeLists.txt), which would otherwise propagate
  # into gRPC's own FetchContent-built dependencies (upb, Abseil, etc.).
  # On macOS, building gRPC's bundled upb as a shared library fails to
  # link ("undefined symbols") - macOS requires every symbol resolved at
  # shared-library build time, unlike Linux's .so. Force static here,
  # for gRPC's own dependencies only, and restore this project's own
  # setting immediately after so dr_evt's own libraries are unaffected.
  set(DR_EVT_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)

  # This project's own -Wall -Wextra etc. (set globally in
  # SetupCXX.cmake) would otherwise apply to gRPC's own FetchContent-
  # built source too. A trailing -w (suppress all warnings) overrides
  # the earlier flags - compilers process flags left-to-right - without
  # needing to know or parse exactly which warning flags were set
  # earlier. dr_evt's own code is unaffected: this only touches
  # CMAKE_CXX_FLAGS for the scope of gRPC's own build, restored right
  # after.
  set(DR_EVT_SAVED_CXX_FLAGS ${CMAKE_CXX_FLAGS})
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
  if (APPLE)
    set(DR_EVT_GRPC_PATCH_COMMAND
        ${CMAKE_COMMAND} -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
                         -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}
                         -P ${CMAKE_SOURCE_DIR}/cmake/patches/fix_abseil_randen_copts.cmake
        COMMAND ${CMAKE_COMMAND}
                -P ${CMAKE_SOURCE_DIR}/cmake/patches/fix_zlib_fdopen.cmake)
  else()
    set(DR_EVT_GRPC_PATCH_COMMAND "")
  endif()
  FetchContent_Declare(
    grpc
    GIT_REPOSITORY https://github.com/grpc/grpc.git
    GIT_TAG        v1.51.1
    PATCH_COMMAND  ${DR_EVT_GRPC_PATCH_COMMAND})
  FetchContent_MakeAvailable(grpc)
  unset(DR_EVT_GRPC_PATCH_COMMAND)
  set(CMAKE_CXX_FLAGS ${DR_EVT_SAVED_CXX_FLAGS})
  unset(DR_EVT_SAVED_CXX_FLAGS)
  set(BUILD_SHARED_LIBS ${DR_EVT_SAVED_BUILD_SHARED_LIBS})
  unset(DR_EVT_SAVED_BUILD_SHARED_LIBS)

  # FetchContent uses add_subdirectory under the hood, so gRPC's own
  # (non-namespaced) target names are directly usable here. Alias them
  # to the same protobuf::* / gRPC::* names find_package() would have
  # produced, so the rest of this project's CMake doesn't need to know
  # or care which discovery path was actually taken.
  if (NOT TARGET protobuf::libprotobuf)
    add_library(protobuf::libprotobuf ALIAS libprotobuf)
  endif()
  if (NOT TARGET protobuf::protoc)
    add_executable(protobuf::protoc ALIAS protoc)
  endif()
  if (NOT TARGET gRPC::grpc++)
    add_library(gRPC::grpc++ ALIAS grpc++)
  endif()
  if (NOT TARGET gRPC::grpc_cpp_plugin)
    add_executable(gRPC::grpc_cpp_plugin ALIAS grpc_cpp_plugin)
  endif()
else()
  if (NOT TARGET gRPC::grpc_cpp_plugin)
    message(FATAL_ERROR
      "gRPC found, but the gRPC::grpc_cpp_plugin target is missing - "
      "check that your gRPC installation includes the C++ plugin "
      "(on Debian/Ubuntu, this is part of protobuf-compiler-grpc).")
  endif()
endif()

set(DR_EVT_HAS_GRPC TRUE)

# This module provides Protobuf too, whenever gRPC is enabled - see
# CMakeLists.txt's root-level ordering, which skips SetupProtobuf.cmake's
# own, separate discovery/build in that case.
set(DR_EVT_HAS_PROTOBUF TRUE)
