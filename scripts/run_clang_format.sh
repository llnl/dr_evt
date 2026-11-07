#!/usr/bin/env bash

set -euo pipefail

compiler=${CXX:-c++}
compiler_version=$($compiler --version 2>/dev/null || true)

if [[ "${compiler_version,,}" != *clang* ]]; then
    if ! type module >/dev/null 2>&1; then
        for module_init in /etc/profile.d/modules.sh \
                           /usr/share/lmod/lmod/init/bash; do
            if [[ -r "$module_init" ]]; then
                # shellcheck source=/dev/null
                source "$module_init"
                break
            fi
        done
    fi

    if ! type module >/dev/null 2>&1; then
        echo "The default compiler is not Clang and the module command is unavailable." >&2
        exit 1
    fi

    module --latest load clang
fi

if ! command -v clang-format >/dev/null 2>&1; then
    echo "clang-format was not found in PATH." >&2
    exit 1
fi

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
repo_root=$(cd "$script_dir/.." && pwd -P)

mapfile -d '' source_files < <(
    find "$repo_root/src" "$repo_root/tests" -type f \
        \( -name '*.hpp' -o -name '*.cpp' -o -name '*.tpp' -o -name '*.h' -o -name '*.c' \) \
        -print0 | sort -z
)

if (( ${#source_files[@]} == 0 )); then
    echo "No C or C++ source files found under src and tests."
    exit 0
fi

echo "Formatting ${#source_files[@]} files with $(clang-format --version)"
clang-format -i "${source_files[@]}"
