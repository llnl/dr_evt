#!/usr/bin/env bash
set -euo pipefail
mkdir -p /workspace/slurm-event /results
exec > /results/event-build.log 2>&1
gcc -shared -fPIC -O2 /dr_evt_source/validation/slurm/licenses/clock.c \
    -o /workspace/slurm-event/libvalidation_clock.so -ldl -pthread
python3 - <<'PY'
from pathlib import Path
import subprocess, json
name='src/plugins/sched/backfill/backfill.c'
before=subprocess.check_output(['git','-c','safe.directory=/slurm-modern-source','-C','/slurm-modern-source','show','HEAD:'+name],text=True)
after=before.replace('extern void *backfill_agent(void *args)', 'extern void validation_enter_backfill(void);\nextern void validation_leave_backfill(void);\n\nextern void *backfill_agent(void *args)',1)
old='\t\tlock_slurmctld(all_locks);\n\t\tvalidate_all_reservations(true, false);'
assert after.count(old)==1
after=after.replace(old,'\t\tvalidation_enter_backfill();\n'+old,1)
old='\t\tunlock_slurmctld(all_locks);\n\n\t\tslurm_mutex_lock(&check_bf_running_lock);'
assert after.count(old)==1
after=after.replace(old,'\t\tunlock_slurmctld(all_locks);\n\t\tvalidation_leave_backfill();\n\n\t\tslurm_mutex_lock(&check_bf_running_lock);',1)
path = Path('/workspace/slurm-modern-source/'+name)
if path.read_text() != after:
    path.write_text(after)
Path('/results/event-overlay.json').write_text(json.dumps({'file':name,'reason':'Serialize virtual event batches with native backfill cycles; no allocation or reservation algorithm changes.','before':before,'after':after},indent=2)+'\n')
PY
make -C /workspace/slurm-modern-build/src/plugins/sched/backfill -j4
cp /workspace/slurm-modern-build/src/plugins/sched/backfill/.libs/sched_backfill.so /workspace/slurm-event/
# Leave the shared native source ready for an ordinary, uninstrumented rebuild.
git -c safe.directory=/slurm-modern-source -C /slurm-modern-source \
    show HEAD:src/plugins/sched/backfill/backfill.c > /workspace/slurm-modern-source/src/plugins/sched/backfill/backfill.c
gcc -O2 -I/workspace/slurm-modern-build -I/workspace/slurm-modern-source \
    /dr_evt_source/validation/slurm/licenses/client.c -o /workspace/slurm-event/client \
    -L/workspace/slurm-modern-build/src/api/.libs -lslurmfull -L/workspace/slurm-event -lvalidation_clock \
    -Wl,-rpath,/workspace/slurm-modern-build/src/api/.libs -Wl,-rpath,/workspace/slurm-event
