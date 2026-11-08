"""Measure a complete license-enabled Lassen validation without interventions."""
import argparse
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import sys
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--outdir', type=Path, required=True)
parser.add_argument('--trace', type=Path, required=True)
args = parser.parse_args()
command = [sys.executable, str(Path(__file__).with_name('event_trace.py')),
           '--bf-licenses', 'on', '--outdir', str(args.outdir), '--trace', str(args.trace)]
started = time.monotonic()
subprocess.run(command, check=True, env=dict(os.environ, SLURM_LICENSE_STATE_ONLY='1'))
elapsed = time.monotonic() - started
usage = resource.getrusage(resource.RUSAGE_CHILDREN)
directory = args.outdir / 'lassen-prefix2000-hybrid-r32-q64'
report = json.loads((directory / 'report.json').read_text())
result = {'command': command, 'wall_seconds': elapsed,
          'children_user_seconds': usage.ru_utime, 'children_system_seconds': usage.ru_stime,
          'children_peak_rss_kib': usage.ru_maxrss,
          'native_driver_seconds': report['slurm_metrics']['process_elapsed_seconds'],
          'native_driver_scope': report['timing_scope'],
          'full_process_scope': 'Complete adapter process, including native startup/exit, DR_EVT comparison and plots',
          'logging': 'info from startup through completion', 'interventions': [],
          'note': 'One instrumented validation run; elapsed time includes the adapter and observer, not only scheduler computation'}
(directory / 'performance.json').write_text(json.dumps(result, indent=2) + '\n')
shutil.copyfile(__file__, directory / 'run_performance.py')
print(json.dumps(result, indent=2), flush=True)
