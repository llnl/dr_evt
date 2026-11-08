#!/usr/bin/env bash
set -euo pipefail
mkdir -p /workspace/slurm-source /workspace/slurm-build /results
exec > >(tee /results/build.log) 2>&1
if test ! -f /workspace/slurm-source/configure; then
    git -c safe.directory=/slurm-source -C /slurm-source archive HEAD | tar -xf - -C /workspace/slurm-source
fi
python3 /validation-slurm/prepare.py
cd /workspace/slurm-build
if test ! -f Makefile; then
    CFLAGS='-O2 -g -fcommon' /workspace/slurm-source/configure \
        --prefix=/workspace/slurm-install --sysconfdir=/workspace/slurm-etc \
        --enable-simulator --enable-front-end --disable-x11 --with-hdf5=no --with-hwloc=no
fi
make -C src -j"${BUILD_JOBS:-4}"
make -C src install
python3 /dr_evt_source/validation/flux/build_dr_evt.py
python3 /validation-slurm/record_build.py
