import os, json, subprocess, time, statistics, hashlib
from pathlib import Path
root=Path('/private/tmp/dr_evt-plan-profile-20261109')
trace='/Users/yeom2/work/dr_evt/tests/test_traces/scale/huge_10000jobs.csv'
versions={'baseline':root/'before-build','original':root/'after-original','inline':root/'after-build'}
report={'change':'Force-inline record_estimated_start(); no behavior or layout changes','repetitions_per_process':50,'runs_per_version':5,'cases':[],'libraries':{k:hashlib.sha256((v/'libdr_evt.dylib').read_bytes()).hexdigest() for k,v in versions.items()}}
for policy in ('easy','hybrid'):
 case={'policy':policy,'runs':[]}
 for n in range(6):
  order=list(versions) if n%2==0 else list(reversed(versions))
  for name in order:
   path=versions[name];folder=root/'inline-measurements'/policy/f'{n:02d}-{name}';folder.mkdir(parents=True,exist_ok=True)
   env=os.environ.copy();env['DYLD_LIBRARY_PATH']=str(path)
   command=[str(path/'profile_simulation'),trace,'50',policy]
   with (folder/'stdout.log').open('w') as log:
    start=time.perf_counter();p=subprocess.Popen(command,cwd=folder,env=env,stdout=log,stderr=subprocess.STDOUT)
    _,status,usage=os.wait4(p.pid,0);elapsed=time.perf_counter()-start;p.returncode=os.waitstatus_to_exitcode(status)
   assert p.returncode==0,(folder,(folder/'stdout.log').read_text())
   assert (folder/'stdout.log').read_text().strip().endswith('jobs_started=500000')
   if n:case['runs'].append({'version':name,'run':n,'wall_seconds':elapsed,'user_seconds':usage.ru_utime,'peak_rss_bytes':usage.ru_maxrss})
  print(policy,n,'done',flush=True)
 case['summary']={name:{key:statistics.median(row[key] for row in case['runs'] if row['version']==name) for key in ('wall_seconds','user_seconds','peak_rss_bytes')} for name in versions}
 report['cases'].append(case);(root/'inline.json').write_text(json.dumps(report,indent=2))
 print(policy,case['summary'],flush=True)
