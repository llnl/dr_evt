#!/usr/bin/env bash
set -euo pipefail

jobs=${BUILD_JOBS:-4}
mkdir -p /results /workspace/install
exec > >(tee /results/build-and-test.log) 2>&1

# Export committed sources only, leaving the mounted checkouts untouched.
# A changed revision/extension requires a new build volume.
python3 /validation/prepare.py
cd /workspace/flux-core
export FLUX_VERSION=$(cat /workspace/flux-core.version)
if test ! -x configure; then ./autogen.sh; fi
if test ! -f Makefile; then
    ./configure --prefix=/workspace/install --disable-docs
fi
make -j"$jobs"
make install
eval "$(flux env)"

cmake -S /workspace/flux-sched -B /workspace/flux-sched-build \
    -DFLUX_SCHED_VER=0.0.0 -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/workspace/install \
    -DCMAKE_PREFIX_PATH=/workspace/install -DENABLE_DOCS=OFF
cmake --build /workspace/flux-sched-build -j"$jobs"
cmake --install /workspace/flux-sched-build

cd /workspace/flux-fiction
python3 -m pip install --break-system-packages --no-build-isolation --no-deps \
    --config-settings=setup-args=-Dflux_prefix=/workspace/install \
    --config-settings=build-dir=/workspace/fiction-build -e .
eval "$(flux env)"
timeout 120 flux start python3 /validation/test_quiescent.py
export FLUX_FICTION_JOBTAP_SO=/workspace/fiction-build/src/emu-jobtap.so
python3 /validation/test_fiction.py
python3 /validation/build_dr_evt.py
python3 /validation/test_paired.py "$(cat /workspace/dr_evt-simulator)"
