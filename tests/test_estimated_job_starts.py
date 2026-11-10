"""Exercise cached live-planner starts through the Python bindings."""
import pathlib
import tempfile
import unittest

import dr_evt


class EstimatedStartsTests(unittest.TestCase):
    def test_status_cache_reuse_can_be_disabled(self):
        with tempfile.TemporaryDirectory() as directory:
            trace = pathlib.Path(directory) / "empty.csv"
            trace.write_text("job_submit_time,num_nodes,time_limit\n")
            params = dr_evt.SimParams()
            params.infile = str(trace)
            params.total_nodes = 4
            params.run_time_mode = dr_evt.RunTimeMode.LIMIT
            params.backfill_policy = dr_evt.BackfillPolicy.HYBRID
            params.reservation_depth = 1
            sim = dr_evt.Simulation(params)
            sim.initialize_trace(0)
            queue = "pbatch" if dr_evt.legacy_queue_input else "1"
            sim.append_job(0, 3, queue, 100)
            head = sim.append_job(0, 4, queue, 10)
            later = sim.append_job(0, 4, queue, 10)
            sim.advance_to(0)
            self.assertEqual(sim.get_job_statuses([later])[0].expected_start_time, 100)
            self.assertEqual(sim.get_job_statuses(
                [later], use_cached_predictions=False)[0].expected_start_time, 110)
            protected = sim.get_job_statuses(
                [later, head, later], use_cached_unprotected_predictions=False)
            self.assertEqual([job.expected_start_time for job in protected],
                             [110, 100, 110])
            self.assertEqual(protected[1].start_time_kind,
                             dr_evt.StartTimeKind.RESERVED)
            self.assertEqual(protected[0].start_time_kind,
                             dr_evt.StartTimeKind.ESTIMATED)
            self.assertEqual(sim.get_job_statuses(
                [later], use_cached_predictions=False,
                use_cached_unprotected_predictions=False)[0].expected_start_time, 110)
            self.assertEqual(sim.get_job_statuses([later])[0].expected_start_time, 100)

    def test_cached_predictions_do_not_trigger_planning(self):
        with tempfile.TemporaryDirectory() as directory:
            trace = pathlib.Path(directory) / "empty.csv"
            trace.write_text("job_submit_time,num_nodes,time_limit\n")
            params = dr_evt.SimParams()
            params.infile = str(trace)
            params.total_nodes = 4
            params.run_time_mode = dr_evt.RunTimeMode.LIMIT
            sim = dr_evt.Simulation(params)
            sim.initialize_trace(0)
            queue = "pbatch" if dr_evt.legacy_queue_input else "1"
            sim.append_job(0, 3, queue, 100)
            head = sim.append_job(0, 4, queue, 10)
            later = sim.append_job(0, 4, queue, 10)
            initial = sim.get_start_times_by_est_caches()
            self.assertIsNone(initial.estimated_at)
            self.assertTrue(all(job.expected_start_time is None for job in initial.jobs))
            sim.advance_to(0)
            cached = sim.get_start_times_by_est_caches()
            self.assertEqual(cached.estimated_at, 0)
            self.assertEqual([job.job_idx for job in cached.jobs], [head, later])
            self.assertEqual(cached.jobs[0].expected_start_time, 100)
            self.assertEqual(cached.jobs[0].start_time_kind, dr_evt.StartTimeKind.RESERVED)
            self.assertIsNone(cached.jobs[1].expected_start_time)
            self.assertEqual(sim.get_job_statuses([later])[0].expected_start_time, 110)
            self.assertIsNone(sim.get_start_times_by_est_caches().jobs[1].expected_start_time)
            self.assertEqual(sim.get_current_time(), 0)


if __name__ == "__main__":
    unittest.main()
