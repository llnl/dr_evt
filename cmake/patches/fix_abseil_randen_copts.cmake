# Patches gRPC's bundled Abseil (third_party/abseil-cpp/absl/copts/
# AbseilConfigureCopts.cmake) to stop unconditionally emitting BOTH
# x86_64 and arm64 randen-hwaes flags (via -Xarch_<arch> <flag> pairs)
# on every Apple build, regardless of whether it's actually a universal
# (multi-arch) build.
#
# Root cause (confirmed against the actual file, via a real build
# failure): this file's first branch always does
#     foreach(_arch IN ITEMS "x86_64" "arm64")
#       ...list(APPEND ABSL_RANDOM_RANDEN_COPTS "-Xarch_${_arch}" "${_flag}")...
#     endforeach()
# The -Xarch_<arch> prefix is meant to let a single multi-arch (lipo)
# compiler invocation apply each flag only to its own architecture -
# but on a plain single-architecture build, this doesn't reliably
# suppress the other architecture's flag, and clang rejects
# "-msse4.1" outright when the actual (sole) target is arm64-apple-*.
#
# Fix: restrict the loop to the single architecture CMake actually
# knows about via CMAKE_OSX_ARCHITECTURES (falling back to
# CMAKE_SYSTEM_PROCESSOR if that's unset - e.g. a non-Xcode-generator
# build), rather than always trying both. A genuine universal
# (multi-arch) build sets CMAKE_OSX_ARCHITECTURES to more than one
# value, and is unaffected by this change other than dropping the
# hardcoded assumption in favor of what's actually configured.

# Path is relative to FetchContent's PATCH_COMMAND working directory,
# which is the fetched source root itself (gRPC's repo root here) - NOT
# this script's own directory (which is what CMAKE_CURRENT_LIST_DIR
# would resolve to, since this runs via `cmake -P`).
set(_target_file "third_party/abseil-cpp/absl/copts/AbseilConfigureCopts.cmake")
file(READ "${_target_file}" _content)

set(_arches "${CMAKE_OSX_ARCHITECTURES}")
if (NOT _arches)
  set(_arches "${CMAKE_SYSTEM_PROCESSOR}")
endif()
# Normalize to Abseil's own expected architecture name spelling.
list(TRANSFORM _arches REPLACE "^(aarch64|ARM64)$" "arm64")
list(TRANSFORM _arches REPLACE "^(AMD64|X86_64)$" "x86_64")
string(REPLACE ";" "\" \"" _arches_quoted "${_arches}")

set(_search_line "foreach(_arch IN ITEMS \"x86_64\" \"arm64\")")
string(FIND "${_content}" "${_search_line}" _search_pos)

if (_search_pos EQUAL -1)
  message(WARNING "fix_abseil_randen_copts.cmake: expected line not found - "
                   "Abseil's AbseilConfigureCopts.cmake may have changed; "
                   "this patch had no effect.")
else()
  string(REPLACE
    "${_search_line}"
    "foreach(_arch IN ITEMS \"${_arches_quoted}\")"
    _patched_content "${_content}")
  file(WRITE "${_target_file}" "${_patched_content}")
  message(STATUS "Patched Abseil's randen COPTS loop to use: ${_arches}")
endif()
