#!/usr/bin/env bash
set -euo pipefail
DR_EVT_SOURCE_ROOT=${DR_EVT_SOURCE_ROOT:-/dr_evt_source}
mkdir -p /workspace/slurm-event /results
exec > /results/event-build.log 2>&1
gcc -shared -fPIC -O2 "$DR_EVT_SOURCE_ROOT/validation/slurm/licenses/clock.c" \
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
# Backfill gating alone allows main dispatch between simultaneous completion
# RPCs. Gate _schedule before it acquires controller locks so every release and
# arrival in the batch is visible first. The clock library makes this opt-in;
# the native allocation and reservation algorithms are retained.
name = 'src/slurmctld/job_scheduler.c'
before = subprocess.check_output(['git','-c','safe.directory=/slurm-modern-source','-C','/slurm-modern-source','show','HEAD:'+name],text=True)
after = before.replace('#include "src/slurmctld/licenses.h"', '#include "src/slurmctld/licenses.h"\n#include <dlfcn.h>', 1)
old = '\t\tjob_cnt = _schedule(full_queue);'
assert after.count(old) == 1
after = after.replace(old, '\t\tvoid (*validation_enter)(void) = dlsym(RTLD_DEFAULT, "validation_enter_dispatch");\n'
    '\t\tvoid (*validation_leave)(void) = dlsym(RTLD_DEFAULT, "validation_leave_dispatch");\n'
    '\t\tif (validation_enter) validation_enter();\n' + old + '\n'
    '\t\tif (validation_leave) validation_leave();', 1)
Path('/workspace/slurm-modern-source/'+name).write_text(after)
Path('/results/dispatch-overlay.json').write_text(json.dumps({'file':name,'reason':'Opt-in main-dispatch gate outside equal-time RPC batches; native selection and reservation rules retained.','before':before,'after':after},indent=2)+'\n')
# There are no compute daemons in the trace-driven adapter. Suppress the
# controller's node ping agent only while the validation clock is active;
# otherwise failed network probes mark correctly registered virtual nodes
# NO_RESPOND and remove them from scheduling availability.
name = 'src/slurmctld/ping_nodes.c'
before = subprocess.check_output(['git','-c','safe.directory=/slurm-modern-source','-C','/slurm-modern-source','show','HEAD:'+name],text=True)
after = before.replace('#include <pthread.h>', '#include <pthread.h>\n#include <dlfcn.h>', 1)
old = 'void ping_nodes (void)\n{'
assert after.count(old) == 1
after = after.replace(old, old + '\n\tint (*validation_clock_active)(void) =\n\t\tdlsym(RTLD_DEFAULT, "validation_clock_active");\n\tif (validation_clock_active && validation_clock_active())\n\t\treturn;', 1)
Path('/workspace/slurm-modern-source/'+name).write_text(after)
Path('/results/liveness-overlay.json').write_text(json.dumps({'file':name,'reason':'Detect the preloaded trace validation clock by exported symbol and disable compute-daemon health probes; the adapter has no slurmd processes and uses native registration/completion RPCs.','before':before,'after':after},indent=2)+'\n')
# The adapter drives execution and completion directly, so there are no
# slurmd processes to receive launch or termination transport messages. Drop
# only those two messages while validation is active. Native controller state
# transitions and scheduling have already occurred, and the adapter supplies
# the corresponding batch-complete and per-node epilog RPCs.
name = 'src/slurmctld/agent.c'
before = subprocess.check_output(['git','-c','safe.directory=/slurm-modern-source','-C','/slurm-modern-source','show','HEAD:'+name],text=True)
after = before.replace('#include <unistd.h>', '#include <unistd.h>\n#include <dlfcn.h>', 1)
old = 'void agent_queue_request(agent_arg_t *agent_arg_ptr)\n{\n\tqueued_request_t *queued_req_ptr = NULL;'
assert after.count(old) == 1
new = old + '\n\tint (*validation_clock_active)(void) =\n\t\tdlsym(RTLD_DEFAULT, "validation_clock_active");\n\tif (validation_clock_active && validation_clock_active() &&\n\t    ((agent_arg_ptr->msg_type == REQUEST_BATCH_JOB_LAUNCH) ||\n\t     (agent_arg_ptr->msg_type == REQUEST_TERMINATE_JOB))) {\n\t\tpurge_agent_args(agent_arg_ptr);\n\t\treturn;\n\t}'
after = after.replace(old, new, 1)
Path('/workspace/slurm-modern-source/'+name).write_text(after)
Path('/results/transport-overlay.json').write_text(json.dumps({'file':name,'reason':'The trace adapter replaces compute execution: suppress only batch-launch and termination network messages that otherwise target absent slurmd processes; native controller scheduling and state transitions remain active.','before':before,'after':after},indent=2)+'\n')
PY
make -C /workspace/slurm-modern-build/src/plugins/sched/backfill -j4
cp /workspace/slurm-modern-build/src/plugins/sched/backfill/.libs/sched_backfill.so /workspace/slurm-event/
make -C /workspace/slurm-modern-build/src/slurmctld -j4
cp /workspace/slurm-modern-build/src/slurmctld/.libs/slurmctld /workspace/slurm-modern-install/sbin/slurmctld
# Leave the shared native source ready for an ordinary, uninstrumented rebuild.
git -c safe.directory=/slurm-modern-source -C /slurm-modern-source \
    show HEAD:src/plugins/sched/backfill/backfill.c > /workspace/slurm-modern-source/src/plugins/sched/backfill/backfill.c
git -c safe.directory=/slurm-modern-source -C /slurm-modern-source \
    show HEAD:src/slurmctld/job_scheduler.c > /workspace/slurm-modern-source/src/slurmctld/job_scheduler.c
git -c safe.directory=/slurm-modern-source -C /slurm-modern-source \
    show HEAD:src/slurmctld/ping_nodes.c > /workspace/slurm-modern-source/src/slurmctld/ping_nodes.c
git -c safe.directory=/slurm-modern-source -C /slurm-modern-source \
    show HEAD:src/slurmctld/agent.c > /workspace/slurm-modern-source/src/slurmctld/agent.c
gcc -O2 -I/workspace/slurm-modern-build -I/workspace/slurm-modern-source \
    "$DR_EVT_SOURCE_ROOT/validation/slurm/licenses/client.c" -o /workspace/slurm-event/client \
    -L/workspace/slurm-modern-build/src/api/.libs -lslurmfull -L/workspace/slurm-event -lvalidation_clock \
    -Wl,-rpath,/workspace/slurm-modern-build/src/api/.libs -Wl,-rpath,/workspace/slurm-event
