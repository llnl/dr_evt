import sys,json
sys.path.insert(0,'/dr_evt_source/validation/openpbs')
from pathlib import Path
from types import SimpleNamespace
from compare import experiment
args=SimpleNamespace(outdir=Path('/results/spacing-control'),native=Path('/workspace/openpbs-reference'),simulator=Path(Path('/workspace/dr_evt-simulator').read_text().strip()))
jobs=json.loads(Path('/results/validation/lassen-prefix2000-r32/workload.json').read_text())
jobs=[(a*2,n,l*2,d*2) for a,n,l,d in jobs]
r=experiment(args,'lassen-prefix2000-r32-spacing2',jobs,756,32)
assert r['exact_match'],r['different_jobs']
