"""Longer in-process simulation loops to reduce launch/output timing noise."""
from pathlib import Path
import json
import statistics
from compare import run

root = Path('/private/tmp/dr_evt-plan-profile-20261109')
trace = '/Users/yeom2/work/dr_evt/tests/test_traces/scale/huge_10000jobs.csv'
report = dict(scope='50 complete 10,000-job simulations per process; trace parsing and scheduling included; final CSV export and CLI startup excluded',
              policy='Circular queue, R=32, Q=64, total nodes=795, requested-limit runtime',
              execution='One warmup per binary and policy, then five alternating AB/BA paired runs',
              cases=[])
for policy in ('easy', 'hybrid'):
    result = dict(policy=policy, repetitions=50, runs=[])
    for number in range(6):
        for tag in ('before', 'after') if number % 2 == 0 else ('after', 'before'):
            folder=root/'core-measurements'/policy/f'{number:02d}-{tag}'
            row=run([str(root/(tag+'-build')/'profile_simulation'),trace,'50',policy],folder)
            assert (folder/'stdout.log').read_text().strip().endswith('jobs_started=500000')
            if number:
                result['runs'].append(dict(version=tag,repetition=number,**row))
        print(policy,number,'done',flush=True)
    result['summary']={}
    for tag in ('before','after'):
        rows=[r for r in result['runs'] if r['version']==tag]
        result['summary'][tag]={key:statistics.median(r[key] for r in rows)
                               for key in ('wall_seconds','user_seconds','system_seconds','peak_rss_bytes')}
        result['summary'][tag]['wall_range']=[min(r['wall_seconds'] for r in rows),max(r['wall_seconds'] for r in rows)]
    report['cases'].append(result)
    (root/'core.json').write_text(json.dumps(report,indent=2))
    print(policy,result['summary'],flush=True)
