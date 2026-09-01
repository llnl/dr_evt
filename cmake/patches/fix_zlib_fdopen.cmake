# Patches gRPC's bundled zlib (third_party/zlib/zutil.h) to stop
# stubbing out fdopen() as NULL on modern macOS.
#
# Root cause (confirmed against the actual file, via a real build
# failure): zutil.h's `#if defined(MACOS) || defined(TARGET_OS_MAC)`
# block assumes TARGET_OS_MAC means specifically classic, pre-OS X Mac
# OS - where fdopen() genuinely didn't exist, since it had no POSIX
# libc - and stubs it out as `#define fdopen(fd,mode) NULL`. But
# TARGET_OS_MAC (from Apple's own <TargetConditionals.h>) is actually
# defined as 1 on ALL Apple platforms, including modern macOS, where
# fdopen() is a completely normal, real POSIX libc function. The stub
# macro then "poisons" every subsequent use of the identifier fdopen -
# including the system <stdio.h>'s own function declaration - turning
# `FILE *fdopen(int, const char *)` into the syntactically invalid
# `FILE *NULL(int, const char *)`.
#
# Fix: remove the stub definition entirely. It should never fire on any
# modern system where fdopen genuinely exists as a real libc function -
# unlike the classic Mac OS case this code was actually written for.

set(_target_file "third_party/zlib/zutil.h")
file(READ "${_target_file}" _content)

set(_search_line "#        define fdopen(fd,mode) NULL /* No fdopen() */")
string(FIND "${_content}" "${_search_line}" _search_pos)

if (_search_pos EQUAL -1)
  message(WARNING "fix_zlib_fdopen.cmake: expected line not found - "
                   "zlib's zutil.h may have changed; this patch had no effect.")
else()
  string(REPLACE
    "${_search_line}"
    "/* fdopen() stub removed by dr_evt's fix_zlib_fdopen.cmake - it is a real, working libc function on any modern platform this stub's own guard (TARGET_OS_MAC) fires on. */"
    _patched_content "${_content}")
  file(WRITE "${_target_file}" "${_patched_content}")
  message(STATUS "Patched zlib's zutil.h to stop stubbing out fdopen()")
endif()
