#!/usr/bin/env bash
set -euo pipefail
source_root=/dr_evt_source/external/openpbs
revision=$(git -c safe.directory="$source_root" -C "$source_root" rev-parse HEAD)
native_source=/workspace/openpbs-sources/$revision
native_build=/workspace/openpbs-builds/$revision
mkdir -p "$native_source" "$native_build" /results
if test ! -f "$native_source/configure"; then
    git -c safe.directory="$source_root" -C "$source_root" archive HEAD | tar -xf - -C "$native_source"
    (cd "$native_source" && bash autogen.sh) > /results/autogen.log 2>&1
fi
if test ! -f "$native_build/Makefile"; then
    (cd "$native_build" && "$native_source/configure" --prefix=/workspace/openpbs-install \
        --with-pbs-server-home=/workspace/openpbs-home --with-python=/usr) > /results/configure.log 2>&1
fi
# Build the native scheduler and its libraries; no database or MOM is started.
make -C "$native_build/src/include" -j4 > /results/openpbs-build.log 2>&1
make -C "$native_build/src/lib" -j4 >> /results/openpbs-build.log 2>&1
make -C "$native_build/src/scheduler" -j4 >> /results/openpbs-build.log 2>&1
python3 /dr_evt_source/validation/openpbs/link.py "$native_source" "$native_build"
mkdir -p /results/scripts
cp /dr_evt_source/validation/openpbs/*.py /dr_evt_source/validation/openpbs/*.cpp \
    /dr_evt_source/validation/openpbs/*.sh /dr_evt_source/validation/openpbs/Dockerfile \
    /dr_evt_source/validation/openpbs/Doxyfile /results/scripts/
cp /workspace/native_resource_definitions.hpp /results/scripts/
(cd /dr_evt_source/validation/openpbs && python3 -m unittest test_compare) > /results/audit-tests.log 2>&1
python3 /dr_evt_source/validation/flux/build_dr_evt.py > /results/dr_evt-build.log 2>&1
python3 /dr_evt_source/validation/openpbs/compare.py \
    --native /workspace/openpbs-reference \
    --simulator "$(cat /workspace/dr_evt-simulator)" \
    --trace /dr_evt_source/lassen_traces/lassen-756-01.csv \
    --dr-priority "${OPENPBS_VALIDATION_DR_PRIORITY:-fcfs}" \
    --max-jobs "${OPENPBS_VALIDATION_MAX_JOBS:-2000}" --outdir /results
