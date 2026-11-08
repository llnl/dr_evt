"""Opt-in native resource snapshots before Fiction advances simulated time."""
import os

if os.environ.get('DR_EVT_FICTION_SCHEDULER') == 'sched-simple':
    from sched_simple_support import activate
    activate()

if os.environ.get('DR_EVT_OBSERVE_RESOURCES') == '1':
    import hashlib
    import json
    from pathlib import Path

    import flux
    from flux.job import JobID
    from flux_fiction._core.engine import Simulation

    original = Simulation.quiescent_cb

    def snapshot_then_advance(self, reply_epoch, bucket_time, span_id=None):
        """Read allocation maps after starts/releases, excluding reservations."""
        if (reply_epoch == self.current_epoch
                and bucket_time == float(self.current_time)
                and not self.failed_reason):
            # A separate connection avoids dispatching Fiction callbacks while
            # synchronous diagnostic queries are outstanding.
            handle = flux.Flux()
            simple = os.environ.get('DR_EVT_FICTION_SCHEDULER') == 'sched-simple'
            snapshot = handle.rpc('sched.validation-snapshot').get() if simple else None

            def find(criteria):
                return handle.rpc('sched-fluxion-resource.find', {
                    'criteria': criteria, 'format': 'rv1_nosched'}).get()

            record = {'time': bucket_time, 'epoch': reply_epoch,
                      'allocated': snapshot['allocated'] if simple else find('sched-now=allocated'),
                      'all': snapshot['all'] if simple else find('status=up or status=down'),
                      'jobs': []}
            for jobid, job in self.job_map.items():
                if ('STARTED' in job.state_transitions
                        and 'COMPLETED' not in job.state_transitions):
                    record['jobs'].append({
                        'job': job.trace_index, 'jobid': str(jobid),
                        'allocation': (snapshot['jobs'][str(int(JobID(jobid)))] if simple else
                                       find(f'jobid-alloc={jobid}'))})
            record['observer_sha256'] = hashlib.sha256(
                Path(__file__).read_bytes()).hexdigest()
            output = Path(self.output_dir) / 'resource-manager-snapshots.jsonl'
            with output.open('a') as stream:
                stream.write(json.dumps(record) + '\n')
        return original(self, reply_epoch, bucket_time, span_id)

    Simulation.quiescent_cb = snapshot_then_advance
