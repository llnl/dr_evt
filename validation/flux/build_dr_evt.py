"""Build the committed simulator in the container, separate from reference exports."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path('/workspace')
SOURCE = Path('/dr_evt_source')


def git(*args):
    return subprocess.check_output(
        ['git', '-c', 'safe.directory=/dr_evt_source', '-C', str(SOURCE), *args])


revision = git('rev-parse', 'HEAD').decode().strip()
source = ROOT / 'dr_evt-sources' / revision
build = ROOT / 'dr_evt-builds' / revision
if not source.exists():
    source.mkdir(parents=True)
    subprocess.run(['tar', '-xf', '-', '-C', str(source)],
                   input=git('archive', 'HEAD'), check=True)
flags = ['-DCMAKE_BUILD_TYPE=Release', '-DDR_EVT_WITH_SER20=OFF',
         '-DDR_EVT_WITH_UNIT_TESTING=OFF', '-DDR_EVT_BUILD_PYTHON=OFF',
         '-DDR_EVT_ENABLE_PROTOBUF=OFF', '-DDR_EVT_ENABLE_GRPC=OFF',
         '-DDR_EVT_LEGACY_RESERVATION_BARRIER=OFF']
subprocess.run(['cmake', '-S', str(source), '-B', str(build), *flags], check=True)
subprocess.run(['cmake', '--build', str(build), '--target', 'sim-bin', '-j4'], check=True)
binary = build / 'simulator'
manifest = {'revision': revision, 'tree': git('rev-parse', 'HEAD^{tree}').decode().strip(),
            'configure_flags': flags, 'binary': str(binary),
            'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}
Path('/results/dr_evt-sources.json').write_text(json.dumps(manifest, indent=2) + '\n')
Path('/workspace/dr_evt-simulator').write_text(str(binary) + '\n')
