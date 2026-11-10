"""Evaluate query latency, coverage, and error against independently drained simulations."""
import argparse
import csv
import gzip
import hashlib
import json
import math
from pathlib import Path
import platform
import statistics
import subprocess


def quantile(values, fraction):
    values = sorted(values)
    return values[math.ceil(fraction * (len(values) - 1))] if values else None


def metrics(rows):
    known = [r for r in rows if r['predicted_start'] != ''
             and math.isfinite(float(r['predicted_start']))]
    errors = [float(r['predicted_start']) - float(r['true_start']) for r in known]
    absolute = [abs(v) for v in errors]
    return dict(observations=len(rows), predictions=len(known),
                coverage=len(known)/len(rows) if rows else None,
                mae_seconds=statistics.mean(absolute) if absolute else None,
                median_absolute_error_seconds=statistics.median(absolute) if absolute else None,
                p90_absolute_error_seconds=quantile(absolute, .9),
                bias_seconds=statistics.mean(errors) if errors else None,
                within_one_second=sum(v <= 1 for v in absolute)/len(absolute) if absolute else None,
                too_early_fraction=sum(v < -1 for v in errors)/len(errors) if errors else None,
                too_late_fraction=sum(v > 1 for v in errors)/len(errors) if errors else None)


def read_rows(directory, filename, optional=False):
    path = directory/filename
    if path.exists():
        with path.open() as stream:
            rows = list(csv.DictReader(stream))
    compressed = directory/(filename+'.gz')
    if not path.exists() and compressed.exists():
        with gzip.open(compressed, 'rt') as stream:
            rows = list(csv.DictReader(stream))
    if not path.exists() and not compressed.exists():
        if optional:
            return []
        raise FileNotFoundError(path)
    # Retain old raw evidence verbatim while accepting its historical CSV key.
    for row in rows:
        if 'planned_at' in row and 'estimated_at' not in row:
            row['estimated_at'] = row['planned_at']
    return rows


def normalized_timing(timings, counts):
    total_microseconds = sum(float(r['seconds_per_query'])*1e6 for r in timings)
    total_predictions = sum(counts.get(r['snapshot'], 0) for r in timings)
    per_checkpoint = [float(r['seconds_per_query'])*1e6/counts[r['snapshot']]
                      for r in timings if counts.get(r['snapshot'], 0)]
    return dict(
        query_microseconds_sum=total_microseconds,
        predicted_job_observations=total_predictions,
        zero_prediction_checkpoints=sum(not counts.get(r['snapshot'], 0) for r in timings),
        amortized_microseconds_per_predicted_job=total_microseconds/total_predictions
                                               if total_predictions else None,
        median_microseconds_per_predicted_job=statistics.median(per_checkpoint)
                                             if per_checkpoint else None,
        p90_microseconds_per_predicted_job=quantile(per_checkpoint, .9))


def summarize(directory):
    rows = read_rows(directory, 'predictions.csv')
    timings = read_rows(directory, 'timings.csv')
    shared_timings = read_rows(directory, 'shared_timings.csv', optional=True)
    shared_query = {}
    for api in ('cached_filtered', 'fresh_shared', 'fresh_shared_depth', 'exact_shared', 'extended_shared'):
        selected = [r for r in shared_timings if r['api'] == api]
        latency = [float(r['seconds_per_query'])*1e6 for r in selected]
        shared_query[api] = dict(
            snapshots=len(selected),
            shared_jobs_range=[min(int(r['shared_jobs']) for r in selected),
                               max(int(r['shared_jobs']) for r in selected)] if selected else None,
            median_query_microseconds=statistics.median(latency) if latency else None,
            p90_query_microseconds=quantile(latency, .9),
            **normalized_timing(selected, {r['snapshot']: int(r['shared_jobs'])
                                           for r in selected}))
    summary = {}
    methods = ['cached', 'fresh', 'exact']
    if any(r['api'] == 'extended' for r in rows):
        methods.append('extended')
    known_sets = {api: {(r['snapshot'], r['job_idx']) for r in rows
                       if r['api'] == api and r['predicted_start'] != ''
                       and math.isfinite(float(r['predicted_start']))}
                  for api in methods}
    common = set.intersection(*known_sets.values())
    for api in methods + ['fresh_depth']:
        selected = [r for r in rows if r['api'] == api]
        selected_timings = [r for r in timings if r['api'] == api]
        latency = [float(r['seconds_per_query'])*1e6 for r in selected_timings]
        shared = [r for r in selected if (r['snapshot'], r['job_idx']) in common]
        cached_subset = [r for r in selected
                         if (r['snapshot'], r['job_idx']) in known_sets['cached']]
        counts = {}
        for r in selected:
            if r['predicted_start'] != '' and math.isfinite(float(r['predicted_start'])):
                counts[r['snapshot']] = counts.get(r['snapshot'], 0) + 1
        summary[api] = dict(**metrics(selected),
                            common_with_cached=metrics(cached_subset),
                            common_across_methods=metrics(shared),
                            normalized_query_timing=normalized_timing(selected_timings, counts),
                            median_query_microseconds=statistics.median(latency),
                            p90_query_microseconds=quantile(latency, .9),
                            by_kind={kind: metrics([r for r in selected if r['kind'] == kind])
                                     for kind in ('0','1','2')})
    actual = read_rows(directory, 'ground_truth.csv')
    changes = {}
    for row in actual:
        start, end = float(row['start']), float(row['end'])
        assert start >= float(row['submit'])
        assert abs(end-start-float(row['actual_run_time'])) < 1e-6
        for timestamp, delta in ((start, int(row['nodes'])), (end, -int(row['nodes']))):
            timestamp = round(timestamp, 6)
            changes[timestamp] = changes.get(timestamp, 0) + delta
    allocated, peak = 0, 0
    for timestamp, delta in sorted(changes.items()):
        allocated += delta
        assert allocated >= 0
        peak = max(peak, allocated)
    assert allocated == 0
    return dict(apis=summary, shared_query_timings=shared_query,
                snapshots=len({r['snapshot'] for r in rows}),
                pending_range=[min(int(r['pending']) for r in timings), max(int(r['pending']) for r in timings)],
                published_max_start_error=None, jobs=len(actual), peak_allocated_nodes=peak,
                average_wait_seconds=statistics.mean(float(r['start'])-float(r['submit']) for r in actual),
                median_actual_to_limit=statistics.median(float(r['actual_run_time'])/float(r['time_limit']) for r in actual),
                plan_age_range=[min(float(r['time'])-float(r['estimated_at']) for r in rows if r['estimated_at']),
                                max(float(r['time'])-float(r['estimated_at']) for r in rows if r['estimated_at'])]
                               if any(r['estimated_at'] for r in rows) else None)


def write_normalized_report(output, report):
    observable = report.get('observable_prediction', False)
    lines = [
        '# Query cost per predicted job and start-time errors', '',
        'All methods query the full eligible pending queue.', '',
        '**Cost** is summed mean query time divided by finite returned '
        'predictions, including query costs at zero-prediction checkpoints. '
        '**Mean Absolute Error (MAE)** averages absolute differences between '
        'estimated and reference start times. '
        '**Common MAE** uses the intersection of finite predictions across '
        'methods, matched by job and checkpoint; **Own MAE** uses each method\'s '
        'finite predictions. Errors compare estimates with independent '
        'reference execution. A job contributes once per checkpoint. '
        'Undefined metrics are shown as —.', '',
        ('**Side simulation** uses only submitted jobs and time-limit estimates '
        'for unfinished durations and completion events. Timing includes input '
        'preparation and replay. Actual durations establish observed history '
        'and reference execution; the control executes for time limits. '
        '`evaluate_observable.cpp` enforces this restriction before calling '
        'the lookahead API.' if observable else
        '**Side simulation:** `get_start_times_by_lookahead()` clones and replays '
        'the configured scheduler. It knows all supplied actual durations and '
        'future arrivals in the actual-runtime experiment. Its zero error '
        'demonstrates replay fidelity with that information. The limit control '
        'changes execution durations to time limits for both the reference and '
        'the replay; it does not measure time-limit-only replay predictions '
        'against actual-runtime execution.'), '',
        '**Cached** = `get_start_times_by_est_caches()`; **Forecast without backfill** = '
        '`get_job_statuses(ids, 0, false)`; **Side simulation** = '
        '`get_start_times_by_lookahead(ids)`. The forecast uses full '
        'time-limit windows and respects existing protected reservations, but does not '
        'simulate future backfill admissions.', '']
    if any('extended' in r['apis'] for r in report['results']):
        lines += ['**Cache-first** = `get_job_statuses(ids)`: reuse cached '
                  'starts and forecast missing starts, including reuse of unprotected '
                  'cached estimates. `get_job_statuses(ids, 0, true, false)` instead '
                  'preserves compatible protected windows and forecasts unprotected '
                  'jobs; that option is not measured in these tables.', '']
    names = {
        'fiction-fcfs': 'Flux FCFS / no backfill',
        'fiction-hybrid': 'Flux-style pass accounting',
        'slurm-protected': 'Slurm-style periodic protection',
        'slurm-no-reservations': 'Slurm license-off diagnostic',
        'openpbs-fcfs': 'OpenPBS, ordinary forecast',
        'openpbs-grouped': 'OpenPBS, 1s event grouping',
        'easy': 'EASY', 'event-hybrid': 'Event hybrid',
        'event-conservative': 'Event conservative',
        'fixed-dispatch': 'Fixed dispatch, 30s'}
    def number(value, digits):
        return f'{value:.{digits}f}' if value is not None else '—'
    for mode, heading in [('actual', 'Actual execution durations'),
                          ('limit', 'Execution duration equals time limit')]:
        lines += ['## '+heading, '',
                  '| Configuration | API | Coverage | µs / predicted job | Common-set MAE (min) | Own-set MAE (min) |',
                  '| --- | --- | ---: | ---: | ---: | ---: |']
        for result in report['results']:
            if result['execution'] != mode:
                continue
            apis = [('cached', 'Cached'), ('fresh', 'Forecast without backfill')]
            if 'extended' in result['apis']:
                apis.append(('extended', 'Cache-first'))
            apis.append(('exact', 'Side simulation'))
            for api, label in apis:
                data = result['apis'][api]
                common = data['common_across_methods']['mae_seconds']
                own = data['mae_seconds']
                lines.append('| {} | {} | {:.2f}% | {} | {} | {} |'.format(
                    names[result['case']], label, data['coverage']*100,
                    number(data['normalized_query_timing']['amortized_microseconds_per_predicted_job'], 3),
                    number(common/60 if common is not None else None, 2),
                    number(own/60 if own is not None else None, 2)))
        hybrid = next(r for r in report['results']
                      if r['case'] == 'event-hybrid' and r['execution'] == mode)
        lines += ['', 'Event hybrid: {} common job/checkpoint observations; '
                  '{} cached predictions and {} per full-coverage method.'.format(
                      hybrid['apis']['cached']['common_across_methods']['predictions'],
                      hybrid['apis']['cached']['predictions'],
                      hybrid['apis']['fresh']['predictions']), '']
    if observable:
        control = next(r for r in report['results']
                       if r['case'] == 'event-hybrid' and r['execution'] == 'limit')
        cached = control['apis']['cached']
        protected = cached['by_kind']['1']
        unprotected = cached['by_kind']['2']
        common = control['apis']['fresh']['common_across_methods']
        cache_explanation = (
            'For the event-hybrid time-limit control, the {:.2f} versus {:.2f} '
            'minutes common-set MAE compares the same {} job/checkpoint observations. '
            'The cached results comprise {} protected reservations with {:.2f} minutes '
            'MAE and {} unprotected estimates with {:.2f} minutes MAE. '
            'Unprotected cached estimates record when enough nodes become available, '
            'without placing every earlier waiting job’s full execution window; '
            'they can predict starts too early. That behavior is consistent with '
            'Slurm’s `squeue --start`. The forecast without backfill '
            'places those windows in queue order, which closely matches execution '
            'for this common set when durations equal time limits. Cached lookup '
            'does not provide a complete forecast of future backfill decisions.'.format(
                common['mae_seconds']/60,
                cached['common_across_methods']['mae_seconds']/60,
                common['predictions'], protected['predictions'],
                protected['mae_seconds']/60, unprotected['predictions'],
                unprotected['mae_seconds']/60))
        cache_first_explanation = (
            'Cache-first reuses accurate protected reservations and unprotected '
            'cached estimates. In this control, the latter account for its {:.2f}-minute '
            'common-set MAE, versus {:.2f} minutes when cache reuse is disabled. '
            'It refreshes cached starts before the query time, reducing error from '
            'the cache-only result of {:.2f} minutes, but retains other optimistic '
            'estimates. Accurate reservations therefore do not make the entire '
            'cache accurate. These tables measure default cache-first reuse of both '
            'protected and unprotected starts, not the protected-only option.'.format(
                control['apis']['extended']['common_across_methods']['mae_seconds']/60,
                common['mae_seconds']/60,
                cached['common_across_methods']['mae_seconds']/60)
            if 'extended' in control['apis'] else '')
        lines += ['## Interpretation', '', cache_explanation, '',
                  cache_first_explanation, '',
                  'The forecast holds resources for full time limits. '
                  'Early completions therefore create accumulating timing error. '
                  'When execution durations equal those limits, its duration assumptions '
                  'match the reference, but omitted future backfill and dispatch rules '
                  'still cause error. The two conditions use different observation sets.', '',
                  'Disabling cache reuse recomputes reserved starts too. Recomputing '
                  'can change those values because it builds a new plan from current '
                  'state. However, a cached reservation often reflects the earliest '
                  'fit after running jobs finish and earlier protected jobs are '
                  'accounted for. The forecast can find the same fit without simulating '
                  'future backfill, so omitting backfill does not always produce a '
                  'noticeable difference.', '',
                  'Cache-first is an inexpensive default when durations are uncertain. '
                  'Replay is useful with accurate estimates or for policy-sensitive '
                  'studies. These results establish neither a worst-case bound nor '
                  'an accuracy threshold for choosing replay.', '']
    lines += ['## Reproduction', '',
              '`summary.json` records metrics and prediction counts. '
              'Raw measurements are generated locally.', '',
              'Build the benchmark following '
              '`validation/performance/estimated-starts/prediction-evaluation/README.md`, '
              'then regenerate the evaluation:', '', '```bash',
              'python3 validation/performance/estimated-starts/prediction-evaluation/run.py \\',
              '  /tmp/evaluate_predictions --repo "$PWD" --observable \\',
              '  --outdir /tmp/dr_evt_prediction_results \\',
              '  --workdir /tmp/dr_evt_prediction_runs', '```', '']
    (output/'NORMALIZED_RESULTS.md').write_text('\n'.join(lines))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('binary', type=Path)
    p.add_argument('--repo', type=Path, required=True)
    p.add_argument('--outdir', type=Path, required=True)
    p.add_argument('--workdir', type=Path, required=True)
    p.add_argument('--resume', action='store_true')
    p.add_argument('--reanalyze', action='store_true',
                   help='Recompute metrics from saved evidence without executing the simulator')
    p.add_argument('--observable', action='store_true',
                   help='Record the information restriction used by evaluate_observable.cpp')
    args = p.parse_args()
    if args.reanalyze:
        output = args.outdir.resolve()
        report = json.loads((output/'summary.json').read_text())
        for result in report['results']:
            updated = summarize(output/'evidence'/(result['case']+'-'+result['execution']))
            updated.pop('published_max_start_error')
            result.update(updated)
        report['normalized_timing_definition'] = (
            'Sum of mean query times for one call at each checkpoint divided by the '
            'sum of finite returned predictions at those checkpoints. Includes query '
            'cost at zero-prediction checkpoints; undefined if no predictions exist.')
        report['common_error_definition'] = (
            'Intersection of finite predictions from cached, unrestricted forecast without backfill, and '
            'exact side replay, matched by checkpoint and job ID. Own-method error '
            'uses all finite predictions returned by that method.')
        (output/'summary.json').write_text(json.dumps(report, indent=2)+'\n')
        write_normalized_report(output, report)
        print('Reanalyzed', len(report['results']), 'datasets in', output)
        return
    repo, output, work = args.repo.resolve(), args.outdir.resolve(), args.workdir.resolve()
    binary = args.binary.resolve()
    output.mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True, exist_ok=True)
    published = json.loads((repo/'docs/_static/scheduler-validation/performance/cases.json').read_text())
    cases = list(published)
    common_options = ['--total_nodes','756','--priority_policy','fcfs','--queue_impl','circular',
                      '--trace_format','simple','--timestamp_format','epoch','--msec_output',
                      '--reservation_depth','32','--backfill_queue_depth','64']
    for name, extras in [('easy', ['--backfill_policy','easy']),
                         ('event-hybrid', ['--backfill_policy','hybrid']),
                         ('event-conservative', ['--backfill_policy','conservative','--priority_policy','fcfs_conservative']),
                         ('fixed-dispatch', ['--backfill_policy','hybrid','--backfill_queue_depth','0',
                                             '--dispatch_interval','30','--dispatch_queue_depth','64'])]:
        cases.append(dict(name=name, jobs=published[0]['jobs'], command_options=common_options+extras))
    report = dict(platform=platform.platform(), machine=platform.machine(),
                  binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                  library_sha256=hashlib.sha256((repo/'build/libdr_evt.dylib').read_bytes()).hexdigest(),
                  base_revision=subprocess.check_output(['git','rev-parse','HEAD'], cwd=repo, text=True).strip(),
                  observable_prediction=args.observable,
                  scope='Actual-runtime execution and a control with supplied durations explicitly set to time limits. Forecasts use limits. Each query is compared with an independent actual continuation from the same checkpoint; all arrivals and actual durations are known to exact replay.',
                  exact_caveat='Exact lookahead can use actual durations and known future arrivals. It is an oracle continuation, not a time-limit-only production prediction.',
                  results=[])
    if args.observable:
        report['scope'] = (
            'At each checkpoint only already-submitted jobs are loaded. '
            'Cached and forecast without backfill queries use the observed live state. Prediction '
            'replay replaces all unfinished durations and running completion '
            'events with time-limit estimates, excludes future submissions, '
            'and is compared against an independent actual continuation. '
            'Actual durations only establish observed past state and ground truth.')
        report['exact_caveat'] = (
            'Side query times include input-state sanitization and the existing '
            'exact API call; this currently entails two checkpoint clones. '
            'The native exact API itself replays the durations in its input '
            'state and does not enforce this information boundary automatically.')
    if args.resume:
        saved=json.loads((output/'summary.json').read_text())
        assert saved['binary_sha256']==report['binary_sha256']
        assert saved['library_sha256']==report['library_sha256']
        report=saved
    for case in cases:
        for mode in ('actual', 'limit'):
            completed=[r for r in report['results'] if r['case']==case['name'] and r['execution']==mode]
            if completed:
                assert completed[0]['options']==case['command_options']
                continue
            name = case['name']+'-'+mode
            directory=work/name
            directory.mkdir(parents=True,exist_ok=True)
            with (directory/'jobs.csv').open('w',newline='') as f:
                writer=csv.writer(f,lineterminator='\n')
                writer.writerow(['job_submit_time','num_nodes','time_limit','actual_run_time'])
                writer.writerows(case['jobs'])
            command=[str(binary), str(directory/'jobs.csv'), str(directory), mode]+case['command_options']
            with (directory/'run.log').open('w') as log:
                subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
            result=dict(case=case['name'],execution=mode,options=case['command_options'],
                        input_sha256=hashlib.sha256((directory/'jobs.csv').read_bytes()).hexdigest(),
                        **summarize(directory))
            assert result['peak_allocated_nodes'] <= 756
            if mode=='actual' and 'expected_schedule' in case:
                observed=list(csv.DictReader((directory/'ground_truth.csv').open()))
                difference=max(max(abs(float(row['start'])-expected[0]),abs(float(row['end'])-expected[1]))
                               for row,expected in zip(observed,case['expected_schedule']))
                assert difference < 1e-6, (name, difference)
                result['published_max_start_error']=difference
            result['query_isolation_and_independent_continuation_match_passed']=True
            report['results'].append(result)
            (output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
            evidence=output/'evidence'/name
            evidence.mkdir(parents=True,exist_ok=True)
            for filename in ('ground_truth.csv','predictions.csv','timings.csv','shared_timings.csv'):
                with gzip.GzipFile(filename=str(evidence/(filename+'.gz')),mode='wb',mtime=0) as compressed:
                    compressed.write((directory/filename).read_bytes())
            (evidence/'command.json').write_text(json.dumps(command,indent=2)+'\n')
            print(name, result['snapshots'],
                  {api:{key:result['apis'][api][key] for key in ('coverage','mae_seconds','median_query_microseconds')}
                   for api in ('cached','fresh','exact')},flush=True)
    write_normalized_report(output, report)


if __name__ == '__main__':
    main()
