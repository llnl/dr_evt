"""Native sched-simple with read-only Fiction synchronization and snapshots.

The inherited allocation, FIFO scheduling, and forecast methods are unchanged.
Load only for validation; the extra RPCs let Fiction wait before advancing time.
"""
import importlib.util
import hashlib
import os
from pathlib import Path

from flux.brokermod import request_handler

native_path = Path(os.environ['FLUX_PREFIX']) / 'lib/flux/modules/sched-simple.py'
spec = importlib.util.spec_from_file_location('native_sched_simple', native_path)
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)


class ObservedSimpleScheduler(native.SimpleScheduler):
    def __init__(self, handle, *args):
        super().__init__(handle, *args)
        self._validation_requests = []
        self._validation_query = None
        self._validation_idle = handle.idle_watcher_create()
        self._validation_check = handle.check_watcher_create(self._settle)
        self._validation_check.start()

    def _busy(self):
        return (self._sched_pending or self._sched_generator is not None
                or self._forecast_pending or self._forecast_generator is not None)

    @request_handler('sched.quiescent', prefix=False)
    def _quiescent(self, msg):
        self._validation_requests.append(msg)
        self._validation_idle.start()

    def _settle(self, *_):
        if not self._validation_requests or self._validation_query or self._busy():
            return
        self._validation_idle.stop()
        # This round trip fences preceding alloc replies and resource frees.
        self._validation_query = self.handle.rpc('job-manager.alloc-query')
        self._validation_query.then(self._manager_reply)

    def _manager_reply(self, future):
        response = future.get()
        self._validation_query = None
        pending = len(self._queue)
        full = self.queue_depth != 'unlimited' and pending >= self.queue_depth
        if (not self._busy() and response['alloc_pending'] == pending
                and (response['queue_length'] == 0 or full)):
            # With a full request window, upstream jobs may remain queued.
            # They cannot run before the blocked FIFO head, so time may advance.
            running = len(self.resources.impl._job_state)
            for msg in self._validation_requests:
                self.handle.respond(msg, {'status': 0, 'alloc_current': running})
            self._validation_requests.clear()
        else:
            self._validation_idle.start()

    @request_handler('sched.validation-snapshot', prefix=False)
    def _snapshot(self, msg):
        pool = self.resources.impl
        self.handle.respond(msg, {
            'all': {'R': pool.to_dict()},
            'allocated': {'R': pool.copy_allocated().to_dict()},
            'jobs': {str(jobid): {'R': allocation.to_dict()}
                     for jobid, (_, allocation) in pool._job_state.items()},
            'pending': len(self._queue), 'busy': self._busy(),
            'native_scheduler_path': str(native_path),
            'native_scheduler_sha256': hashlib.sha256(native_path.read_bytes()).hexdigest(),
            'validation_wrapper_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        })


def mod_main(handle, *args):
    ObservedSimpleScheduler(handle, *args).run()
