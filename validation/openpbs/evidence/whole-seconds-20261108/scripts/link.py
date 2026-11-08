"""Link the native scheduler archive with a server-I/O adapter, without patches."""
import hashlib
import io
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tarfile
import xml.etree.ElementTree as ET

source, build = map(Path, sys.argv[1:])
adapter = Path('/dr_evt_source/validation/openpbs/reference.cpp')
scheduler = build / 'src/scheduler'
archive = subprocess.check_output(['git', '-c', 'safe.directory=/dr_evt_source/external/openpbs',
                                   '-C', '/dr_evt_source/external/openpbs', 'archive', source.name,
                                   'src/scheduler'])
source_hashes = {}
with tarfile.open(fileobj=io.BytesIO(archive)) as snapshot:
    for entry in snapshot:
        if entry.isfile() and Path(entry.name).suffix in ('.cpp', '.h'):
            expected = snapshot.extractfile(entry).read()
            assert (source / entry.name).read_bytes() == expected, entry.name
            source_hashes[entry.name] = hashlib.sha256(expected).hexdigest()
# Use the native server's resource metadata, including host-vs-server flags.
definitions_file = source / 'src/lib/Libattr/master_resc_def_all.xml'
definitions = []
wanted = {'cput', 'mem', 'walltime', 'soft_walltime', 'ncpus', 'arch', 'host',
          'vnode', 'aoe', 'eoe', 'min_walltime', 'max_walltime', 'preempt_targets',
          'select', 'place', 'nodect'}
for entry in ET.parse(definitions_file).findall('attributes'):
    name = entry.findtext('member_name', '').strip('"')
    if name in wanted:
        definitions.append('{' + ','.join([json.dumps(name), entry.findtext('member_at_type'),
                                          entry.findtext('member_at_flags')]) + '}')
assert len(definitions) == len(wanted)
header = Path('/workspace/native_resource_definitions.hpp')
header.write_text('static const struct { const char *name; int type; int flags; } native_definitions[] = {\n' + ',\n'.join(definitions) + '\n};\n')
# Reuse the build system's exact native link dependencies and compile flags.
command = subprocess.check_output(
    ['make', '-C', str(scheduler), '-n', '-W', str(source / 'src/scheduler/pbs_sched.cpp'), 'pbs_sched'], text=True)
compile_line = next(line for line in command.splitlines()
                    if ' -c -o pbs_sched-pbs_sched.o ' in line)
link_line = next(line for line in command.splitlines()
                 if '--mode=link' in line and ' -o pbs_sched ' in line)
compile_args = shlex.split(compile_line)
# Automake adds a dependency-tracking suffix using shell control operators.
compile_args = compile_args[:compile_args.index('&&')] if '&&' in compile_args else compile_args
compile_args[compile_args.index('-o') + 1] = '/workspace/openpbs-adapter.o'
compile_args = compile_args[:compile_args.index('-o') + 2] + [str(adapter)]
compile_args.append('-I/workspace')
subprocess.run(compile_args, cwd=scheduler, check=True)
link_args = shlex.split(link_line)
link_args[link_args.index('pbs_sched')] = '/workspace/openpbs-reference'
link_args[link_args.index('pbs_sched-pbs_sched.o')] = '/workspace/openpbs-adapter.o'
wrapped = ['time', 'pbs_statserver', 'pbs_statque', 'pbs_statvnode', 'pbs_selstat',
           'pbs_statresv', 'pbs_statsched', 'pbs_statrsc', 'pbs_asyrunjob', 'pbs_asyalterjob',
           'pbs_geterrmsg', '_Z19get_sched_cmd_noblkiP9sched_cmd']
link_args.extend('-Wl,--wrap=' + name for name in wrapped)
subprocess.run(link_args, cwd=scheduler, check=True)
manifest = {
    'revision': source.name,
    'source_patches': [],
    'scheduler_source_sha256': source_hashes,
    'configure_arguments': subprocess.check_output([str(build / 'config.status'), '--config'],
                                                    cwd=build, text=True).strip(),
    'wrapped_symbols': wrapped,
    'adapter_sha256': hashlib.sha256(adapter.read_bytes()).hexdigest(),
    'native_library_sha256': hashlib.sha256((scheduler / 'libpbs_sched.a').read_bytes()).hexdigest(),
    'resource_definitions_sha256': hashlib.sha256(header.read_bytes()).hexdigest(),
    'resource_definitions_source_sha256': hashlib.sha256(definitions_file.read_bytes()).hexdigest(),
    'binary_sha256': hashlib.sha256(Path('/workspace/.libs/openpbs-reference').read_bytes()).hexdigest(),
    'libpbs_sha256': hashlib.sha256((build / 'src/lib/Libpbs/.libs/libpbs.so').read_bytes()).hexdigest(),
    'compile_command': compile_args,
    'link_command': link_args,
}
Path('/results/openpbs-build.json').write_text(json.dumps(manifest, indent=2) + '\n')
