"""Build the committed simulator in the container, separate from reference exports."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path('/workspace')
SOURCE = Path('/dr_evt_source')


def git(*args):
    return subprocess.check_output(
        ['git', '-c', 'safe.directory=/dr_evt_source', '-C', str(SOURCE), *args])


revision = git('rev-parse', 'HEAD').decode().strip()
working_tree = os.environ.get('DR_EVT_VALIDATION_WORKING_TREE') == '1'
archive = git('archive', 'HEAD')
source_hashes = {}
if working_tree:
    # Snapshot tracked working files only; ignore external clones and raw results.
    import io
    import tarfile
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode='w') as snapshot:
        for name in sorted(git('ls-files', '-z').decode().split('\0')):
            if not name or not (SOURCE / name).is_file():
                continue
            source_hashes[name] = hashlib.sha256((SOURCE / name).read_bytes()).hexdigest()
            snapshot.add(SOURCE / name, arcname=name, recursive=False)
    archive = buffer.getvalue()
snapshot_id = hashlib.sha256(json.dumps(source_hashes, sort_keys=True).encode()).hexdigest() if working_tree else revision
source = ROOT / 'dr_evt-sources' / snapshot_id
build = ROOT / 'dr_evt-builds' / snapshot_id
if not source.exists():
    source.mkdir(parents=True)
    subprocess.run(['tar', '-xf', '-', '-C', str(source)],
                   input=archive, check=True)
flags = ['-DCMAKE_BUILD_TYPE=Release', '-DDR_EVT_WITH_SER20=OFF',
         '-DDR_EVT_WITH_UNIT_TESTING=OFF', '-DDR_EVT_BUILD_PYTHON=OFF',
         '-DDR_EVT_ENABLE_PROTOBUF=OFF', '-DDR_EVT_ENABLE_GRPC=OFF',
         '-DDR_EVT_LEGACY_RESERVATION_BARRIER=OFF']
subprocess.run(['cmake', '-S', str(source), '-B', str(build), *flags], check=True)
subprocess.run(['cmake', '--build', str(build), '--target', 'sim-bin', '-j4'], check=True)
binary = build / 'simulator'
manifest = {'revision': revision, 'tree': git('rev-parse', 'HEAD^{tree}').decode().strip(),
            'working_tree_snapshot': working_tree, 'source_sha256': source_hashes,
            'configure_flags': flags, 'binary': str(binary),
            'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}
Path('/results/dr_evt-sources.json').write_text(json.dumps(manifest, indent=2) + '\n')
Path('/workspace/dr_evt-simulator').write_text(str(binary) + '\n')
