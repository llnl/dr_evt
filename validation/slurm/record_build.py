"""Record the installed native binary and the validation implementation."""
import hashlib
import json
from pathlib import Path
import platform
import subprocess

path = Path('/results/slurm-source.json')
manifest = json.loads(path.read_text())
binary = Path('/workspace/slurm-install/sbin/slurmctld')
manifest.update(
    version='17.11.7', architecture=platform.machine(),
    binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
    configure_flags=['--enable-simulator', '--enable-front-end', '--disable-x11',
                     '--with-hdf5=no', '--with-hwloc=no'],
    requested_cflags='-O2 -g -fcommon',
    effective_cflags=next(line.strip() for line in Path('/workspace/slurm-build/Makefile').read_text().splitlines()
                         if line.startswith('CFLAGS =')),
    compiler=subprocess.check_output(['gcc', '--version'], text=True).splitlines()[0],
    harness_sha256={file.name: hashlib.sha256(file.read_bytes()).hexdigest()
                    for file in Path('/validation-slurm').iterdir() if file.is_file()})
path.write_text(json.dumps(manifest, indent=2) + '\n')
