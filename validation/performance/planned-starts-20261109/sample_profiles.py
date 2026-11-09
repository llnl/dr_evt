"""Sample repeated simulations separately from uninstrumented timing runs."""
from pathlib import Path
import subprocess

root = Path('/private/tmp/dr_evt-plan-profile-20261109')
trace = '/Users/yeom2/work/dr_evt/tests/test_traces/scale/huge_10000jobs.csv'
profiles = root / 'profiles'
profiles.mkdir(exist_ok=True)
for policy in ('easy', 'hybrid'):
    for tag in ('before', 'after'):
        name = policy + '-' + tag
        with (profiles/(name+'.log')).open('w') as log:
            process = subprocess.Popen([str(root/(tag+'-build')/'profile_simulation'),
                                        trace, '200', policy], cwd=profiles,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                sample = subprocess.run(['/usr/bin/sample', str(process.pid), '5', '1',
                                         '-mayDie', '-file', str(profiles/(name+'.txt'))],
                                        capture_output=True, text=True, timeout=25)
                (profiles/(name+'-sampler.log')).write_text(sample.stdout+sample.stderr)
                if sample.returncode:
                    print(sample.stdout+sample.stderr, flush=True)
                    raise SystemExit(sample.returncode)
                print(name, 'profile saved', flush=True)
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
