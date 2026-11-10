import os, json, statistics, subprocess, hashlib
from pathlib import Path
root=Path('/private/tmp/dr_evt-plan-profile-20261109')
versions={'original':root/'query-original','optimized':root/'after-build'}
report={'scope':'Cached query loop only, setup excluded','runs':9,'libraries':{k:hashlib.sha256((v/'libdr_evt.dylib').read_bytes()).hexdigest() for k,v in versions.items()},'cases':[]}
for count,repetitions,coverage in [(1000,10000,64),(10000,5000,64),(10000,5000,0),(10000,1000,10000)]:
 case={'jobs':count,'repetitions':repetitions,'coverage':coverage,'runs':[]}
 for n in range(10):
  for name in versions if n%2==0 else reversed(list(versions)):
   path=versions[name];folder=root/'query-sort-measurements'/f'{count}-{coverage}'/f'{n:02d}-{name}';folder.mkdir(parents=True,exist_ok=True)
   env=os.environ.copy();env['DYLD_LIBRARY_PATH']=str(path)
   result=subprocess.run([str(root/'benchmark_query_sort'),str(count),str(repetitions),'cached',str(coverage)],env=env,cwd=folder,capture_output=True,text=True,check=True)
   row=json.loads(result.stdout.strip().splitlines()[-1]);assert row['checksum']==count*repetitions
   if n:case['runs'].append({'version':name,'run':n,'seconds':row['seconds']})
 case['summary']={name:{'microseconds_per_query':statistics.median(r['seconds'] for r in case['runs'] if r['version']==name)*1e6/repetitions,'range_seconds':[min(r['seconds'] for r in case['runs'] if r['version']==name),max(r['seconds'] for r in case['runs'] if r['version']==name)]} for name in versions}
 report['cases'].append(case);(root/'query-sort.json').write_text(json.dumps(report,indent=2))
 print(count,coverage,case['summary'],flush=True)
