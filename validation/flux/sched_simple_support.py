"""Opt-in Fiction loader for native sched-simple plus validation observation."""
from pathlib import Path
import json


def activate():
    from flux_fiction._adapters.flux import modules

    def reload_simple(handle, config_source=None, fluxion_variant=None):
        config = modules._load_config_object(config_source)
        loaded = modules.get_loaded_modules(handle)
        resource = next(item['path'] for item in loaded if item['name'] == 'resource')
        for name in ('sched-fluxion-qmanager', 'sched-fluxion-feasibility',
                     'sched-fluxion-resource', 'sched-simple', 'sched_simple'):
            if any(item['name'] == name for item in loaded):
                handle.rpc('module.remove', {'name': name}).get()
        handle.rpc('module.remove', {'name': 'resource'}).get()
        # Native sched-simple supplies feasibility.check itself.
        config.setdefault('ingest', {}).setdefault('validator', {})['plugins'] = [
            'feasibility', 'jobspec']
        handle.rpc('config.load', config).get()
        handle.rpc('module.load', {'path': resource,
                                  'args': ['noverify', 'monitor-force-up']}).get()
        wrapper = Path(__file__).with_name('sched_simple.py')
        depth = config['sched-simple']['queue-depth']
        handle.rpc('module.load', {'path': str(wrapper),
                                  'args': [f'queue-depth={depth}']}).get()
        # A handled RPC proves the native scheduler completed startup.
        readiness = handle.rpc('sched.validation-snapshot').get()
        provenance = {key: value for key, value in readiness.items()
                      if key.startswith(('native_scheduler_', 'validation_wrapper_'))}
        provenance['loaded_modules'] = [item['name'] for item in modules.get_loaded_modules(handle)]
        Path(config_source).with_name('sched-simple-provenance.json').write_text(
            json.dumps(provenance, indent=2) + '\n')
        modules._reload_job_ingest_config(handle, config, True)
        modules.start_all_queues(handle)

    modules.reload_modules = reload_simple
