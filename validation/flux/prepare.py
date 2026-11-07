"""Export committed Flux sources and install an auditable validation overlay."""
import hashlib
import difflib
import json
from pathlib import Path
import subprocess

ROOT = Path('/workspace')
HERE = Path('/validation')


def git(name, *args):
    return subprocess.check_output(
        ['git', '-c', f'safe.directory=/sources/{name}', '-C', f'/sources/{name}', *args]
    )


def replace_once(path, old, new):
    text = path.read_text()
    if text.count(old) != 1:
        raise RuntimeError(f'Unsupported source layout in {path}: {old!r}')
    path.write_text(text.replace(old, new, 1))


manifest = {
    'revisions': {name: git(name, 'rev-parse', 'HEAD').decode().strip()
                  for name in ('flux-core', 'flux-sched', 'flux-fiction')},
    'core_version': git('flux-core', 'describe', '--always').decode().strip().removeprefix('v'),
    'extension_sha256': hashlib.sha256((HERE / 'quiescence.hpp').read_bytes()).hexdigest(),
    'overlay_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
}
stamp = ROOT / 'sources.json'
if stamp.exists():
    if json.loads(stamp.read_text()) != manifest:
        raise RuntimeError('Sources/extension changed. Use a fresh build volume.')
else:
    for name in manifest['revisions']:
        dest = ROOT / name
        dest.mkdir(exist_ok=False)
        subprocess.run(['tar', '-xf', '-', '-C', str(dest)],
                       input=git(name, 'archive', 'HEAD'), check=True)
    sched = ROOT / 'flux-sched'
    module = sched / 'qmanager/modules/qmanager.cpp'
    replace_once(module, 'fluxion_resource_interface_t::~fluxion_resource_interface_t ()',
                 '#include "quiescence.hpp"\n\n'
                 'fluxion_resource_interface_t::~fluxion_resource_interface_t ()')
    replace_once(module, '    if ((rc = flux_reactor_run (flux_get_reactor (h), 0)) < 0)',
                 '    {\n'
                 '        quiescence_service_t quiescence (ctx.get ());\n'
                 '        if (quiescence.start () < 0) {\n'
                 '            qmanager_destroy (ctx);\n'
                 '            return -1;\n'
                 '        }\n'
                 '        if ((rc = flux_reactor_run (flux_get_reactor (h), 0)) < 0)')
    replace_once(module, '        flux_log_error (h, "%s: flux_reactor_run", __FUNCTION__);',
                 '            flux_log_error (h, "%s: flux_reactor_run", __FUNCTION__);')
    replace_once(module, '    qmanager_destroy (ctx);\n    return rc;\n}\n\nextern "C"',
                 '    }\n    qmanager_destroy (ctx);\n    return rc;\n}\n\nextern "C"')
    base = sched / 'qmanager/policies/base/queue_policy_base.hpp'
    replace_once(base, '    /*! Return true if this queue has become schedulable since',
                 '    // Validation-only counts; reservations remain pending.\n'
                 '    size_t quiescence_pending_count () const\n'
                 '    {\n'
                 '        return m_pending.size () + m_pending_provisional.size ()\n'
                 '               + m_blocked.size ();\n'
                 '    }\n'
                 '    size_t quiescence_running_count () const { return m_running.size (); }\n\n'
                 '    /*! Return true if this queue has become schedulable since')
    (module.parent / 'quiescence.hpp').write_bytes((HERE / 'quiescence.hpp').read_bytes())
    stamp.write_text(json.dumps(manifest, indent=2) + '\n')
    (ROOT / 'flux-core.version').write_text(manifest['core_version'] + '\n')
Path('/results/sources.json').write_text(json.dumps(manifest, indent=2) + '\n')
patch = []
for relative in ('qmanager/modules/qmanager.cpp',
                 'qmanager/policies/base/queue_policy_base.hpp',
                 'qmanager/modules/quiescence.hpp'):
    original = '' if relative.endswith('quiescence.hpp') else git(
        'flux-sched', 'show', f'HEAD:{relative}').decode()
    patched = (ROOT / 'flux-sched' / relative).read_text()
    patch.extend(difflib.unified_diff(original.splitlines(True), patched.splitlines(True),
                                    fromfile=f'a/{relative}', tofile=f'b/{relative}'))
Path('/results/fluxion-quiescence.patch').write_text(''.join(patch))
print(json.dumps(manifest, indent=2), flush=True)
