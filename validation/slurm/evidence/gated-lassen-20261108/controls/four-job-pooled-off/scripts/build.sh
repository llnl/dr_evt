#!/usr/bin/env bash
set -euo pipefail
mkdir -p /workspace/slurm-modern-source /workspace/slurm-modern-build /results
exec > /results/build.log 2>&1
if test ! -f /workspace/slurm-modern-source/configure; then
    git -c safe.directory=/slurm-modern-source -C /slurm-modern-source archive HEAD |
        tar -xf - -C /workspace/slurm-modern-source
fi
cd /workspace/slurm-modern-build
if test ! -f Makefile; then
    /workspace/slurm-modern-source/configure --prefix=/workspace/slurm-modern-install \
        --sysconfdir=/workspace/slurm-modern-etc --enable-multiple-slurmd \
        --disable-x11 --with-hdf5=no --with-hwloc=no
fi
make -C src -j"${BUILD_JOBS:-4}"
make -C src install
git -c safe.directory=/slurm-modern-source -C /slurm-modern-source rev-parse HEAD > /results/slurm-revision.txt
/workspace/slurm-modern-install/sbin/slurmctld -V > /results/slurm-version.txt
python3 /dr_evt_source/validation/flux/build_dr_evt.py
