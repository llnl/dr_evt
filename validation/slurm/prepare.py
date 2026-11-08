"""Apply build/runtime fixes to an exported simulator, recording every edit."""
import hashlib
import json
from pathlib import Path
import subprocess

root = Path('/workspace/slurm-source')
edits = []
contents = {}


def source_text(name):
    if name not in contents:
        contents[name] = subprocess.check_output(
            ['git', '-c', 'safe.directory=/slurm-source', '-C', '/slurm-source',
             'show', 'HEAD:' + name], text=True)
    return contents[name]


def replace(name, old, new, reason):
    path = root / name
    before = source_text(name)
    if before.count(old) != 1:
        raise RuntimeError(f'Expected one patch target in {name}: {old!r}')
    after = before.replace(old, new)
    contents[name] = after
    edits.append({'file': name, 'reason': reason, 'before': old, 'after': new,
                  'source_sha256': hashlib.sha256(before.encode()).hexdigest(),
                  'patched_sha256': hashlib.sha256(after.encode()).hexdigest()})


replace('src/common/sim/sim_funcs.c', '"/lib/x86_64-linux-gnu/libc.so.6",',
        '"/lib/aarch64-linux-gnu/libc.so.6",\n                       "/lib/x86_64-linux-gnu/libc.so.6",',
        'Find libc on the ARM Linux build host.')
replace('src/common/sim/sim_funcs.c', 'libc_paths[4]', 'libc_paths[5]',
        'Allow the additional ARM libc path.')
replace('src/common/sim/sim_funcs.c',
        'int gettimeofday(struct timeval *tv, struct timezone *tz)',
        'int gettimeofday(struct timeval *tv, void *tz)',
        'Match the modern glibc declaration.')
replace('src/common/sim/sim_funcs.c', 'static int clock_ticking=0;', '''static int clock_ticking=0;
static int validation_controlled_clock(void)
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = getenv("SLURM_VALIDATION_CONTROLLED_CLOCK") != NULL;
    return enabled;
}''', 'Add an opt-in clock control for zero simulated scheduler execution cost.')
replace('src/common/sim/sim_funcs.c',
        'extern void sim_resume_clock()\n{\n',
        '''extern void sim_resume_clock()
{
    if (validation_controlled_clock()) {
        clock_ticking = 0;
        return;
    }
''', 'Keep host execution from advancing simulation time in controlled-clock mode.')
replace('src/common/sim/sim_funcs.c',
        '*(sim_utime) = *(sim_utime) + 10;',
        'if (!validation_controlled_clock()) *(sim_utime) = *(sim_utime) + 10;',
        'Keep clock reads side-effect-free in controlled-clock mode.')
replace('src/common/sim/sim_jobtraces.c', 'while(feof(trace_file)==0)',
        'while (fgetc(trace_file) != EOF && fseek(trace_file, -1, SEEK_CUR) == 0)',
        'Do not create an extra job by reading past the end of a valid trace.')
path = root / 'src/plugins/sched/backfill/backfill.c'
before = source_text('src/plugins/sched/backfill/backfill.c')
position = before.index('extern int sim_backfill_agent(void)')
old = before[position:]
new = old.replace('bool load_config;', 'static bool config_loaded = false;', 1)
new = new.replace('if(load_config == true){', 'if (!config_loaded || config_flag) {', 1)
new = new.replace('load_config = false;', '''config_loaded = true;
        info("VALIDATION_BACKFILL interval=%d table_limit=%d partition_depth=%d reservations=%d resolution=%d horizon=%d",
             backfill_interval, max_backfill_job_cnt, max_backfill_job_per_part, bf_job_part_count_reserve,
             backfill_resolution, backfill_window);''', 1)
new = new.replace('if(next_backfill_utime<now){',
                  'if(next_backfill_utime <= now){', 1)
replace('src/plugins/sched/backfill/backfill.c', old, new,
        'Load native backfill configuration on first use and reconfiguration; log effective settings. Wake at the deadline, so controlled time does not introduce a one-step delay.')
observer = Path('/validation-slurm/observer.h').read_text()
(root / 'src/slurmctld/validation_observer.h').write_text(observer)
edits.append({'file': 'src/slurmctld/validation_observer.h',
              'reason': 'Read-only native resource and node-ownership snapshots.',
              'patched_sha256': hashlib.sha256(observer.encode()).hexdigest()})
replace('src/slurmctld/simulator.c', '\nextern int sim_controller()',
        '\n#include "validation_observer.h"\n\nextern int sim_controller()',
        'Include the read-only observer in the simulator controller.')
replace('src/slurmctld/simulator.c', '\n\t\t//do outputs\n',
        '\n\t\tvalidation_snapshot();\n\n\t\t//do outputs\n',
        'Capture state after the event/main/backfill batch without requesting a pass.')
revision = subprocess.check_output(['git', '-c', 'safe.directory=/slurm-source',
                                   '-C', '/slurm-source', 'rev-parse', 'HEAD'], text=True).strip()
Path('/results/slurm-source.json').write_text(json.dumps(
    {'revision': revision, 'overlays': edits}, indent=2) + '\n')
for name, content in contents.items():
    path = root / name
    if path.read_text() != content:
        path.write_text(content)
