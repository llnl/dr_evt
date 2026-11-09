"""Measure existing status-query regression and the new cached-query cost."""
import argparse
import json
from pathlib import Path
import statistics
from compare import run


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--runs', type=int, default=9)
    args = p.parse_args()
    root = args.root.resolve()
    report = dict(scope='C++ query loop only; setup and initial live planning excluded from query time',
                  workload='100 nodes, one running 99-node job until t=1000; waiting jobs request all 100 nodes for 10 seconds',
                  policy='Event-driven hybrid, R=32, Q=64; one initial planning wake, no state changes between queries',
                  cases=[])
    for count, repetitions in [(1000, 1000), (10000, 100)]:
        result = dict(jobs=count, repetitions=repetitions, runs=[])
        choices = [('before', 'fresh'), ('after', 'fresh'), ('after', 'cached')]
        for number in range(args.runs+1):
            for tag, mode in choices if number % 2 == 0 else reversed(choices):
                folder = root / 'query-measurements' / str(count) / f'{number:02d}-{tag}-{mode}'
                row = run([str(root/(tag+'-build')/'benchmark_queries'), str(count),
                           str(repetitions), mode], folder)
                query = json.loads((folder / 'stdout.log').read_text().strip().splitlines()[-1])
                assert query['checksum'] == count * repetitions
                if number:
                    result['runs'].append(dict(version=tag, mode=mode, repetition=number,
                                              query_seconds=query['seconds'], **row))
        result['summary'] = {}
        for tag, mode in choices:
            rows = [r for r in result['runs'] if r['version']==tag and r['mode']==mode]
            result['summary'][tag+'-'+mode] = {
                'median_microseconds_per_query': statistics.median(r['query_seconds'] for r in rows)*1e6/repetitions,
                'median_peak_rss_bytes': statistics.median(r['peak_rss_bytes'] for r in rows),
                'query_seconds_range': [min(r['query_seconds'] for r in rows), max(r['query_seconds'] for r in rows)]}
        report['cases'].append(result)
        (root/'queries.json').write_text(json.dumps(report,indent=2))
        print(count,result['summary'],flush=True)


if __name__ == '__main__':
    main()
