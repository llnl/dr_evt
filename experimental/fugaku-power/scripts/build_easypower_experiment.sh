#!/usr/bin/env bash
set -euo pipefail

# The Python module ships an unrelated Anaconda Boost build. Keep it out of
# this C++ configure/link step and select the Boost built for this compiler/MPI.
module unload python/3.13.2 2>/dev/null || true
module load gcc/13.3.1-magic openmpi/4.1.2 boost/1.86.0 cmake/3.30.5
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../../.." && pwd)
private_target="$repo_dir/experimental/fugaku-power/easypower_private_target.cmake"
EASYPOWER_BUILD_DIR=${EASYPOWER_BUILD_DIR:-$repo_dir/experimental/fugaku-power/build}
export EASYPOWER_BUILD_DIR
build_dir=$EASYPOWER_BUILD_DIR
build_jobs=${EASYPOWER_BUILD_JOBS:-4}
if [[ ! $build_jobs =~ ^[0-9]+$ ]] || ((build_jobs <= 0)); then
  echo "EASYPOWER_BUILD_JOBS must be a positive integer" >&2
  exit 2
fi

cmake \
  -S "$repo_dir" \
  -B "$build_dir" \
  -DDR_EVT_WITH_SER20=OFF \
  -DDR_EVT_WITH_UNIT_TESTING=OFF \
  -DDR_EVT_ENABLE_PROTOBUF=OFF \
  -DDR_EVT_ENABLE_GRPC=OFF \
  -DDR_EVT_BUILD_PYTHON=OFF \
  -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PROJECT_INCLUDE="$private_target" \
  "$@"
cmake --build "$build_dir" --target easypower-experiment-bin \
  --parallel "$build_jobs"

echo "$build_dir/easypower_experiment"
