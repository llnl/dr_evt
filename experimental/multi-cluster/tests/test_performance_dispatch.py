#!/usr/bin/env python3
"""Unit tests for the Python/gRPC multi-cluster dispatcher policy."""

import io
import math
import pathlib
import random
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

# Production scripts live one directory above this test suite.
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import grpc_performance_dispatch as dispatch
import build_performance_tables as build_table
import build_prediction_baselines as baselines
import plot_relative_performance as performance_plot
import run_kt_scheduling_study as kt_study
import run_prediction_study as prediction_study
from grpc_performance_dispatch import (
    adjusted_time_limit,
    choose_system,
    evaluation_metrics,
    estimate_release_wait,
    estimate_wait,
    read_applications,
    read_arrivals,
    read_systems,
    read_workloads,
    sample_workload,
)


_FIXTURES = pathlib.Path(__file__).resolve().parent / "testdata"


def window(now, available, releases=(), shadow=-1):
    return SimpleNamespace(
        current_time=now,
        available_nodes=available,
        shadow_time=shadow,
        releases=[
            SimpleNamespace(time=time, nodes_released=nodes)
            for time, nodes in releases
        ],
    )


class FakeMessage:
    def __init__(self, **fields):
        self.__dict__.update(fields)


class FakeMessages:
    ClientMessage = FakeMessage
    InitRequest = FakeMessage
    AdvanceToRequest = FakeMessage
    GetBackfillWindowRequest = FakeMessage
    EstimateQueueHorizonRequest = FakeMessage
    JobAppendData = FakeMessage
    AppendJobsRequest = FakeMessage
    FinishSimulationRequest = FakeMessage


class FakeSession:
    instances = []

    def __init__(self, address, grpc, messages, service):
        del grpc, service
        self.address = address
        self.messages = messages
        self.capacity = 0
        self.current_time = 0
        self.submitted = []
        self.instances.append(self)

    def call(self, request):
        if hasattr(request, "init"):
            self.capacity = request.init.total_nodes
            self.run_time_mode = request.init.run_time_mode
            return SimpleNamespace()
        if hasattr(request, "advance_to"):
            self.current_time = request.advance_to.target_time
            return SimpleNamespace()
        if hasattr(request, "get_backfill_window"):
            return SimpleNamespace(
                get_backfill_window=window(self.current_time, self.capacity)
            )
        if hasattr(request, "estimate_queue_horizon"):
            return SimpleNamespace(
                estimate_queue_horizon=SimpleNamespace(horizon=0)
            )
        if hasattr(request, "append_jobs"):
            job = request.append_jobs.requests[0]
            self.submitted.append(job)
            return SimpleNamespace(
                append_jobs=SimpleNamespace(job_idx=[len(self.submitted) - 1])
            )
        if hasattr(request, "finish_simulation"):
            count = len(self.submitted)
            total_run_time = sum(job.actual_run_time for job in self.submitted)
            return SimpleNamespace(
                finish_simulation=SimpleNamespace(
                    statistics=SimpleNamespace(
                        jobs_submitted=count,
                        jobs_completed=count,
                        avg_turnaround_time=(total_run_time / count if count else 0),
                        avg_bounded_slowdown=(1.0 if count else 0.0),
                        makespan=0,
                    )
                )
            )
        raise AssertionError(f"unexpected request: {request.__dict__}")

    def close(self):
        pass


class PerformanceDispatchTests(unittest.TestCase):
    def setUp(self):
        self.systems = read_systems(_FIXTURES / "machines.csv")
        requirements = read_applications(_FIXTURES / "applications.csv")
        self.workloads = read_workloads(
            _FIXTURES / "ground_truth.csv",
            _FIXTURES / "prediction.csv",
            self.systems,
            requirements,
        )

    def test_hash_prefixed_input_headers_and_inferred_columns(self):
        self.assertEqual(
            [system["system_id"] for system in self.systems],
            ["dane", "mammoth", "tioga", "tuolumne", "matrix"],
        )
        self.assertEqual(self.systems[0]["cpu_column"], "dane")
        self.assertIsNone(self.systems[0]["gpu_column"])
        self.assertEqual(self.systems[-1]["cpu_column"], "matrix-cpu")
        self.assertEqual(self.systems[-1]["gpu_column"], "matrix-gpu")
        self.assertEqual(
            set(self.workloads), {"cpu-solver", "gpu-trainer", "portable-md"}
        )

    def test_arrivals_require_actual_runtime(self):
        jobs = read_arrivals(_FIXTURES / "job_stream.csv")
        self.assertEqual(jobs[0]["duration"], 80)
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "jobs.csv"
            path.write_text(
                "submit_time,num_nodes,time_limit\n0,1,10\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "actual_run_time or duration"):
                read_arrivals(path)

    def test_sampling_is_uniform_by_application_not_row_count(self):
        generator = random.Random(19)
        counts = {app: 0 for app in self.workloads}
        for _ in range(6000):
            counts[sample_workload(self.workloads, generator)["app"]] += 1
        self.assertTrue(all(1800 < count < 2200 for count in counts.values()))

    def test_applications_table_is_sampling_allowlist(self):
        requirements = read_applications(_FIXTURES / "applications.csv")
        requirements.pop("gpu-trainer")
        prediction = (_FIXTURES / "prediction.csv").read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "prediction.csv"
            path.write_text(
                "\n".join(
                    line
                    for line in prediction.splitlines()
                    if not line.startswith("gpu-trainer,")
                )
                + "\n",
                encoding="utf-8",
            )
            workloads = read_workloads(
                _FIXTURES / "ground_truth.csv", path, self.systems, requirements
            )
        self.assertEqual(set(workloads), {"cpu-solver", "portable-md"})

    def test_workload_retains_ground_truth_without_a_prediction(self):
        requirements = read_applications(_FIXTURES / "applications.csv")
        contents = (_FIXTURES / "prediction.csv").read_text(encoding="utf-8")
        contents = contents.replace("0.9,1.3", ",1.3", 1)
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "prediction.csv"
            path.write_text(contents, encoding="utf-8")
            workloads = read_workloads(
                _FIXTURES / "ground_truth.csv", path, self.systems, requirements
            )
        performance = workloads["cpu-solver"][0]["performance"][0]["CPU"]
        self.assertEqual(performance["ground_truth"], 1.0)
        self.assertIsNone(performance["predicted"])

    def test_workload_accepts_missing_prediction_row(self):
        requirements = read_applications(_FIXTURES / "applications.csv")
        contents = (_FIXTURES / "prediction.csv").read_text(encoding="utf-8")
        contents = "\n".join(
            line
            for line in contents.splitlines()
            if "--mesh-small" not in line
        ) + "\n"
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "prediction.csv"
            path.write_text(contents, encoding="utf-8")
            workloads = read_workloads(
                _FIXTURES / "ground_truth.csv", path, self.systems, requirements
            )

        performance = workloads["cpu-solver"][0]["performance"][0]["CPU"]
        self.assertEqual(performance["ground_truth"], 1.0)
        self.assertIsNone(performance["predicted"])

    def test_workload_builder_normalizes_quoted_comma_arguments(self):
        identity = build_table.normalized_identity(
            {
                "app": "Kripke.exe",
                "args": "--niter 10 --procs 4, 4, 2",
                "rank": "32",
            },
            "rank",
        )
        self.assertEqual(
            identity, ("kripke.exe", "--niter10--procs4,4,2", 32)
        )

    def test_workload_builder_uses_one_reference_for_actual_and_prediction(self):
        identity = ("solver", "--size8", 4)
        predictions = {
            identity: [
                {
                    "app": "solver",
                    "args": "--size8",
                    "ranks": "4",
                    "borax": "1",
                    "dane": "2",
                    "mammoth": "4",
                }
            ]
        }
        measurements = {
            identity: {"borax": 50.0, "dane": 100.0, "mammoth": 25.0}
        }
        ground_truth, predicted, skipped = build_table.build_rows(
            predictions,
            measurements,
            {"solver": "CPU-only"},
            ["dane", "mammoth"],
            ["borax", "dane", "mammoth"],
        )
        self.assertEqual(skipped, 0)
        self.assertEqual(float(ground_truth[0]["dane"]), 0.5)
        self.assertEqual(float(predicted[0]["dane"]), 2.0)
        self.assertEqual(float(ground_truth[0]["mammoth"]), 2.0)
        self.assertEqual(float(predicted[0]["mammoth"]), 4.0)

    def test_workload_builder_keeps_measured_modes_without_model_predictions(self):
        identity = ("solver", "--size8", 4)
        predictions = {
            identity: [
                {
                    "app": "solver",
                    "args": "--size8",
                    "ranks": "4",
                    "borax": "1",
                    "dane": "",
                    "mammoth": "4",
                }
            ]
        }
        measurements = {
            identity: {"borax": 50.0, "dane": 100.0, "mammoth": 25.0}
        }
        ground_truth, predicted, skipped = build_table.build_rows(
            predictions,
            measurements,
            {"solver": "CPU-only"},
            ["dane", "mammoth"],
            ["borax", "dane", "mammoth"],
        )
        self.assertEqual(skipped, 0)
        self.assertEqual(float(ground_truth[0]["dane"]), 0.5)
        self.assertEqual(float(ground_truth[0]["mammoth"]), 2.0)
        self.assertEqual(predicted[0]["dane"], "")
        self.assertEqual(float(predicted[0]["mammoth"]), 4.0)

    def test_workload_builder_uses_requirements_as_allowlist(self):
        excluded = ("excluded", "--case", 1)
        ground_truth, predicted, skipped = build_table.build_rows(
            {excluded: [{"dane": "1"}]},
            {excluded: {"dane": 1.0}},
            {},
            ["dane"],
        )
        self.assertEqual((ground_truth, predicted, skipped), ([], [], 0))

    def test_plot_system_alias_and_point_loading(self):
        with tempfile.TemporaryDirectory() as directory:
            actual = pathlib.Path(directory) / "actual.csv"
            predicted = pathlib.Path(directory) / "predicted.csv"
            actual.write_text(
                "App,Args,Ranks,tuolumne-gpu\n"
                'solver,"--grid 4, 4",8,1.25\n',
                encoding="utf-8",
            )
            predicted.write_text(
                "App,Args,Ranks,tuolumne-gpu\n"
                'solver,"--grid 4, 4",8,1.5\n', encoding="utf-8"
            )
            points = performance_plot.load_points(actual, predicted, ["tuolumne"])
        self.assertEqual(points, {"tuolumne-gpu": [(1.25, 1.5, "solver")]})

    def test_plot_skips_unpaired_prediction_cells(self):
        with tempfile.TemporaryDirectory() as directory:
            actual = pathlib.Path(directory) / "actual.csv"
            predicted = pathlib.Path(directory) / "predicted.csv"
            actual.write_text(
                "App,Args,Ranks,dane\nsolver,x,8,\n", encoding="utf-8"
            )
            predicted.write_text(
                "App,Args,Ranks,dane\nsolver,x,8,1.5\n", encoding="utf-8"
            )
            with patch("sys.stderr", new=io.StringIO()) as errors:
                points = performance_plot.load_points(actual, predicted)

        self.assertEqual(points, {"dane": []})
        self.assertIn("dane=1", errors.getvalue())

    def test_application_average_baseline_groups_by_app_rank_and_mode(self):
        rows = [
            {"App": "a", "Args": "x", "Ranks": "4", "dane": "1"},
            {"App": "a", "Args": "y", "Ranks": "4", "dane": "3"},
            {"App": "a", "Args": "z", "Ranks": "8", "dane": "9"},
        ]
        predicted = baselines.application_average_rows(rows, ["dane"])
        self.assertEqual([float(row["dane"]) for row in predicted], [2, 2, 9])

    def test_rajaperf_baseline_preserves_duplicate_kernel_positions(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "machine_rep.txt"
            path.write_text(
                "machine,kernel,kernel\nborax,4,12\ndane,2,3\n",
                encoding="utf-8",
            )
            values = baselines.rajaperf_speedups(path, ["dane"])
        self.assertEqual(values["dane"], 3.0)

    def test_rajaperf_baseline_predicts_modes_without_measurements(self):
        rows = [{"App": "a", "Args": "x", "Ranks": "1", "dane": ""}]
        predicted = baselines.rajaperf_rows(rows, ["dane"], {"dane": 2.0})
        self.assertEqual(float(predicted[0]["dane"]), 2.0)

    def test_wait_uses_cumulative_releases(self):
        snapshot = window(10, 2, ((15, 3), (21, 4)))
        self.assertEqual(estimate_release_wait(snapshot, 5), 5)
        self.assertEqual(estimate_release_wait(snapshot, 8), 11)
        self.assertEqual(estimate_release_wait(snapshot, 10), float("inf"))

    def test_wait_uses_shadow_and_queue_horizon(self):
        snapshot = window(10, 2, shadow=30)
        self.assertEqual(estimate_wait(snapshot, 8, 5, 40), 60)
        immediately_backfillable = window(10, 8, shadow=30)
        self.assertEqual(estimate_wait(immediately_backfillable, 8, 5, 40), 0)

    def test_backfill_can_end_exactly_at_shadow_time(self):
        snapshot = window(10, 2, shadow=30)
        self.assertEqual(estimate_wait(snapshot, 2, 20, 40), 0)
        self.assertEqual(estimate_wait(snapshot, 2, 20.001, 40), 60)

    def test_gpu_only_uses_gpu_enabled_machine(self):
        workload = self.workloads["gpu-trainer"][0]
        job = {
            "job_id": "large-gpu",
            "num_nodes": 100,
            "duration": 80,
            "limit_time": 120,
        }
        windows = [window(0, system["capacity"]) for system in self.systems]
        choice = choose_system(job, workload, self.systems, windows, [0] * 5)
        self.assertEqual(choice["system_id"], "tuolumne")
        self.assertEqual(choice["execution_mode"], "GPU")

    def test_large_job_excludes_smaller_machines(self):
        workload = self.workloads["cpu-solver"][0]
        job = {
            "job_id": "large-cpu",
            "num_nodes": 100,
            "duration": 80,
            "limit_time": 120,
        }
        windows = [window(0, system["capacity"]) for system in self.systems]
        choice = choose_system(job, workload, self.systems, windows, [0] * 5)
        self.assertIn(choice["system_id"], {"dane", "tuolumne"})

    def test_dispatch_respects_each_machine_capacity_boundary(self):
        workload = {
            **self.workloads["cpu-solver"][0],
            "performance": [
                {"CPU": {"ground_truth": 1.0, "predicted": 1.0}, "GPU": None},
                {"CPU": {"ground_truth": 2.0, "predicted": 2.0}, "GPU": None},
                {"CPU": {"ground_truth": 3.0, "predicted": 3.0}, "GPU": None},
                {"CPU": {"ground_truth": 0.9, "predicted": 0.9}, "GPU": None},
                {"CPU": {"ground_truth": 0.8, "predicted": 0.8}, "GPU": None},
            ],
        }
        windows = [window(0, system["capacity"]) for system in self.systems]

        def selected_system(nodes):
            job = {
                "job_id": f"capacity-{nodes}",
                "num_nodes": nodes,
                "duration": 80,
                "limit_time": 1000,
            }
            return choose_system(
                job, workload, self.systems, windows, [0] * len(self.systems)
            )["system_id"]

        self.assertEqual(selected_system(30), "tioga")
        self.assertEqual(selected_system(31), "mammoth")
        self.assertEqual(selected_system(64), "mammoth")
        self.assertEqual(selected_system(65), "dane")

    def test_gpu_portable_uses_faster_mode_on_gpu_system(self):
        workload = self.workloads["portable-md"][0]
        system = self.systems[-1]
        choice = choose_system(
            {
                "job_id": "portable",
                "num_nodes": 16,
                "duration": 80,
                "limit_time": 120,
            },
            {**workload, "performance": [workload["performance"][-1]]},
            [system],
            [window(0, system["capacity"])],
            [0],
        )
        self.assertEqual(choice["execution_mode"], "GPU")
        self.assertEqual(choice["ground_truth_relative_performance"], 1.3)
        self.assertEqual(choice["predicted_relative_performance"], 1.5)
        self.assertAlmostEqual(choice["estimated_duration"], 80 / 1.5)
        self.assertAlmostEqual(choice["actual_duration"], 80 / 1.3)
        self.assertAlmostEqual(choice["predicted_time_limit"], 120 / 1.5)
        self.assertAlmostEqual(choice["actual_time_limit"], 120 / 1.3)

    def test_gpu_portable_uses_feasible_mode_when_faster_mode_exceeds_limit(self):
        workload = {
            **self.workloads["portable-md"][0],
            "performance": [
                {
                    "CPU": {"ground_truth": 1.0, "predicted": 1.0},
                    "GPU": {"ground_truth": 0.5, "predicted": 4.0},
                }
            ],
        }
        system = self.systems[-1]
        choice = choose_system(
            {
                "job_id": "mode-cap",
                "num_nodes": 1,
                "duration": 80,
                "limit_time": 40,
            },
            workload,
            [system],
            [window(0, system["capacity"])],
            [0],
            max_time_limit=100,
        )
        self.assertEqual(choice["execution_mode"], "CPU")

    def test_prediction_drives_choice_but_ground_truth_drives_runtime(self):
        systems = self.systems[:2]
        workload = {
            **self.workloads["cpu-solver"][0],
            "performance": [
                {"CPU": {"ground_truth": 2.0, "predicted": 1.0}, "GPU": None},
                {"CPU": {"ground_truth": 0.5, "predicted": 4.0}, "GPU": None},
            ],
        }
        choice = choose_system(
            {
                "job_id": "prediction",
                "num_nodes": 8,
                "duration": 80,
                "limit_time": 1000,
            },
            workload,
            systems,
            [window(0, system["capacity"]) for system in systems],
            [0, 0],
        )
        self.assertEqual(choice["system_id"], "mammoth")
        self.assertEqual(choice["estimated_duration"], 20)
        self.assertEqual(choice["actual_duration"], 160)

    def test_rel_perf_only_chooses_fastest_system_that_is_available_now(self):
        systems = self.systems[:2]
        workload = {
            **self.workloads["cpu-solver"][0],
            "performance": [
                {"CPU": {"ground_truth": 1.0, "predicted": 4.0}, "GPU": None},
                {"CPU": {"ground_truth": 1.0, "predicted": 2.0}, "GPU": None},
            ],
        }
        choice = choose_system(
            {
                "job_id": "rel-perf-only-available",
                "num_nodes": 8,
                "duration": 80,
                "limit_time": 100,
            },
            workload,
            systems,
            [window(0, 0, ((20, 8),)), window(0, systems[1]["capacity"])],
            [0, 0],
            dispatch_policy="RelPerfOnly",
        )
        self.assertEqual(choice["system_id"], "mammoth")

    def test_rel_perf_only_chooses_fastest_system_when_all_are_full(self):
        systems = self.systems[:2]
        workload = {
            **self.workloads["cpu-solver"][0],
            "performance": [
                {"CPU": {"ground_truth": 1.0, "predicted": 4.0}, "GPU": None},
                {"CPU": {"ground_truth": 1.0, "predicted": 2.0}, "GPU": None},
            ],
        }
        choice = choose_system(
            {
                "job_id": "rel-perf-only-full",
                "num_nodes": 8,
                "duration": 80,
                "limit_time": 100,
            },
            workload,
            systems,
            [window(0, 0, ((20, 8),)), window(0, 0, ((2, 8),))],
            [0, 0],
            dispatch_policy="RelPerfOnly",
        )
        self.assertEqual(choice["system_id"], "dane")

    def test_wait_policy_chooses_shortest_wait_and_ignores_prediction(self):
        systems = self.systems[:2]
        workload = {
            **self.workloads["cpu-solver"][0],
            "performance": [
                {"CPU": {"ground_truth": 1.0, "predicted": 100.0}, "GPU": None},
                {"CPU": {"ground_truth": 1.0, "predicted": 0.1}, "GPU": None},
            ],
        }
        choice = choose_system(
            {
                "job_id": "wait-time-only",
                "num_nodes": 8,
                "duration": 80,
                "limit_time": 100,
            },
            workload,
            systems,
            [window(0, 0, ((20, 8),)), window(0, 0, ((2, 8),))],
            [0, 0],
            dispatch_policy="WaitTimeOnly",
        )

        self.assertEqual(choice["system_id"], "mammoth")
        self.assertEqual(choice["estimated_wait"], 2)
        self.assertEqual(choice["predicted_relative_performance"], 1.0)
        self.assertEqual(choice["estimated_duration"], 80)
        self.assertFalse(choice["prediction_fallback"])

    def test_missing_predictions_fall_back_to_shortest_wait(self):
        systems = self.systems[:2]
        workload = {
            **self.workloads["cpu-solver"][0],
            "performance": [
                {"CPU": {"ground_truth": 2.0, "predicted": None}, "GPU": None},
                {"CPU": {"ground_truth": 1.0, "predicted": None}, "GPU": None},
            ],
        }
        choice = choose_system(
            {
                "job_id": "prediction-fallback",
                "num_nodes": 8,
                "duration": 80,
                "limit_time": 100,
            },
            workload,
            systems,
            [window(0, 0, ((20, 8),)), window(0, 0, ((2, 8),))],
            [0, 0],
            dispatch_policy="turnaround",
        )

        self.assertEqual(choice["system_id"], "mammoth")
        self.assertEqual(choice["estimated_wait"], 2)
        self.assertEqual(choice["predicted_relative_performance"], 1.0)
        self.assertTrue(choice["prediction_fallback"])

    def test_available_prediction_prevents_wait_fallback(self):
        systems = self.systems[:2]
        workload = {
            **self.workloads["cpu-solver"][0],
            "performance": [
                {"CPU": {"ground_truth": 1.0, "predicted": 2.0}, "GPU": None},
                {"CPU": {"ground_truth": 1.0, "predicted": None}, "GPU": None},
            ],
        }
        choice = choose_system(
            {
                "job_id": "partial-prediction",
                "num_nodes": 8,
                "duration": 80,
                "limit_time": 100,
            },
            workload,
            systems,
            [window(0, 0, ((20, 8),)), window(0, systems[1]["capacity"])],
            [0, 0],
            dispatch_policy="turnaround",
        )

        self.assertEqual(choice["system_id"], "dane")
        self.assertFalse(choice["prediction_fallback"])

    def test_prediction_rejected_by_limit_does_not_trigger_fallback(self):
        systems = self.systems[:2]
        workload = {
            **self.workloads["cpu-solver"][0],
            "performance": [
                {"CPU": {"ground_truth": 2.0, "predicted": 0.5}, "GPU": None},
                {"CPU": {"ground_truth": 1.0, "predicted": None}, "GPU": None},
            ],
        }
        choice = choose_system(
            {
                "job_id": "prediction-over-limit",
                "num_nodes": 8,
                "duration": 80,
                "limit_time": 100,
            },
            workload,
            systems,
            [window(0, system["capacity"]) for system in systems],
            [0, 0],
            max_time_limit=100,
            dispatch_policy="turnaround",
        )

        self.assertIsNone(choice)

    def test_predicted_time_limit_is_doubled_and_capped(self):
        self.assertEqual(adjusted_time_limit(10, 35, 100), (40, 2))
        self.assertEqual(adjusted_time_limit(10, 120, 100), (100, 4))
        self.assertEqual(adjusted_time_limit(40, 40, 100), (40, 0))

    def test_actual_duration_wall_time_is_rounded_up(self):
        system = self.systems[0]
        choice = choose_system(
            {
                "job_id": "actual-duration-limit",
                "num_nodes": 8,
                "duration": 80.25,
                "limit_time": 10,
            },
            self.workloads["cpu-solver"][0],
            [system],
            [window(0, system["capacity"])],
            [0],
            max_time_limit=1000,
            wall_time_policy="actual-duration",
        )
        self.assertEqual(
            choice["submitted_time_limit"], math.ceil(choice["actual_duration"])
        )
        self.assertGreaterEqual(
            choice["submitted_time_limit"], choice["actual_duration"]
        )
        self.assertEqual(choice["time_limit_doublings"], 0)

    def test_runtime_limit_filters_systems_before_wait_comparison(self):
        systems = self.systems[:2]
        workload = {
            **self.workloads["cpu-solver"][0],
            "performance": [
                {"CPU": {"ground_truth": 0.5, "predicted": 4.0}, "GPU": None},
                {"CPU": {"ground_truth": 1.0, "predicted": 1.0}, "GPU": None},
            ],
        }
        job = {
            "job_id": "runtime-limit",
            "num_nodes": 8,
            "duration": 80,
            "limit_time": 20,
        }
        choice = choose_system(
            job,
            workload,
            systems,
            [window(0, system["capacity"]) for system in systems],
            [0, 0],
            max_time_limit=100,
        )
        self.assertEqual(choice["system_id"], "mammoth")
        self.assertEqual(choice["submitted_time_limit"], 80)
        self.assertEqual(choice["time_limit_doublings"], 2)
        self.assertIsNone(
            choose_system(
                job,
                workload,
                systems,
                [window(0, system["capacity"]) for system in systems],
                [0, 0],
                max_time_limit=50,
            )
        )

    def test_runtime_limit_filters_predicted_duration(self):
        systems = self.systems[:2]
        workload = {
            **self.workloads["cpu-solver"][0],
            "performance": [
                {"CPU": {"ground_truth": 2.0, "predicted": 0.5}, "GPU": None},
                {"CPU": {"ground_truth": 1.0, "predicted": 1.0}, "GPU": None},
            ],
        }
        job = {
            "job_id": "predicted-runtime-limit",
            "num_nodes": 8,
            "duration": 80,
            "limit_time": 20,
        }
        choice = choose_system(
            job,
            workload,
            systems,
            [window(0, system["capacity"]) for system in systems],
            [0, 0],
            max_time_limit=100,
        )

        self.assertEqual(choice["system_id"], "mammoth")
        self.assertEqual(choice["estimated_duration"], 80)

    def test_controller_uses_same_inputs_and_submits_both_scaled_times(self):
        args = SimpleNamespace(
            server=[f"server-{index}" for index in range(5)],
            jobs=_FIXTURES / "job_stream.csv",
            ground_truth=_FIXTURES / "ground_truth.csv",
            prediction=_FIXTURES / "prediction.csv",
            applications=_FIXTURES / "applications.csv",
            systems=_FIXTURES / "machines.csv",
            seed=19,
            server_infile=_FIXTURES / "job_stream.csv",
            backfill_policy="easy",
            priority_policy="fcfs",
            queue_impl="circular",
            prediction_utilization=1.0,
            max_time_limit=1000.0,
            dispatch_policy="turnaround",
            wall_time_policy="adapted-limit",
            session_name="test",
        )
        FakeSession.instances = []
        with patch.object(dispatch, "ServerSession", FakeSession):
            decisions, statistics, system_ids, dropped_jobs = dispatch.run_experiment(
                args, None, FakeMessages, None
            )

        self.assertEqual(len(decisions), 4)
        self.assertEqual(dropped_jobs, [])
        self.assertEqual(sum(stat.jobs_submitted for stat in statistics), 4)
        self.assertEqual(
            system_ids, [system["system_id"] for system in self.systems]
        )
        capacities = dict(zip(system_ids, (256, 64, 30, 256, 26)))
        for decision in decisions:
            self.assertLessEqual(
                decision["effective_nodes"], capacities[decision["system_id"]]
            )
        submitted = [
            job for session in FakeSession.instances for job in session.submitted
        ]
        self.assertEqual(len(submitted), 4)
        self.assertTrue(all(job.actual_run_time > 0 for job in submitted))
        self.assertCountEqual(
            [job.actual_run_time for job in submitted],
            [decision["actual_duration"] for decision in decisions],
        )
        self.assertCountEqual(
            [job.limit_time for job in submitted],
            [decision["submitted_time_limit"] for decision in decisions],
        )
        for decision in decisions:
            self.assertFalse(decision["prediction_fallback"])
            self.assertAlmostEqual(
                decision["actual_duration"],
                decision["duration"]
                / decision["ground_truth_relative_performance"],
            )
            self.assertAlmostEqual(
                decision["estimated_duration"],
                decision["duration"] / decision["predicted_relative_performance"],
            )
            self.assertAlmostEqual(
                decision["actual_time_limit"],
                decision["time_limit"]
                / decision["ground_truth_relative_performance"],
            )
        output = io.StringIO()
        dispatch.write_results(output, decisions)
        self.assertIn(",false,", output.getvalue())
        self.assertTrue(
            all(job.actual_run_time <= job.limit_time for job in submitted)
        )
        self.assertTrue(
            all(session.run_time_mode == "actual" for session in FakeSession.instances)
        )

        oversized = dict(dispatch.read_arrivals(args.jobs)[0])
        oversized["num_nodes"] = 300
        FakeSession.instances = []
        warning = io.StringIO()
        with (
            patch.object(dispatch, "ServerSession", FakeSession),
            patch.object(dispatch, "read_arrivals", return_value=[oversized]),
            patch("sys.stderr", warning),
        ):
            oversized_decisions, _, _, _ = dispatch.run_experiment(
                args, None, FakeMessages, None
            )
        self.assertEqual(oversized_decisions[0]["effective_nodes"], 256)
        self.assertIn(
            "warning: job_id=job-1 requested_nodes=300 effective_nodes=256 "
            "reason=exceeds_largest_system",
            warning.getvalue(),
        )

        args.max_time_limit = 1.0
        FakeSession.instances = []
        with patch.object(dispatch, "ServerSession", FakeSession):
            dropped_decisions, dropped_statistics, _, dropped_jobs = (
                dispatch.run_experiment(args, None, FakeMessages, None)
            )
        self.assertEqual(dropped_decisions, [])
        self.assertEqual(len(dropped_jobs), 4)
        self.assertEqual(
            sum(stat.jobs_submitted for stat in dropped_statistics), 0
        )
        self.assertTrue(
            all(not session.submitted for session in FakeSession.instances)
        )

    def test_evaluation_metrics_are_job_weighted(self):
        decisions = [
            {"actual_duration": 5.0, "ground_truth_relative_performance": 1.0},
            {"actual_duration": 10.0, "ground_truth_relative_performance": 2.0},
            {"actual_duration": 15.0, "ground_truth_relative_performance": 3.0},
        ]
        statistics = [
            SimpleNamespace(
                jobs_submitted=2,
                jobs_completed=2,
                avg_turnaround_time=20.0,
                avg_bounded_slowdown=2.0,
                makespan=30.0,
            ),
            SimpleNamespace(
                jobs_submitted=1,
                jobs_completed=1,
                avg_turnaround_time=50.0,
                avg_bounded_slowdown=4.0,
                makespan=60.0,
            ),
        ]
        metrics = evaluation_metrics(decisions, statistics)
        self.assertAlmostEqual(metrics["average_turnaround_time"], 30.0)
        self.assertAlmostEqual(metrics["average_bounded_slowdown"], 8.0 / 3.0)
        self.assertAlmostEqual(metrics["average_run_time"], 10.0)
        self.assertAlmostEqual(metrics["average_speedup"], 2.0)

        summary = io.StringIO()
        dispatch.write_summary(summary, statistics, ["first", "second"], decisions)
        self.assertIn("average_bounded_slowdown=2.6666667", summary.getvalue())

        with self.assertRaisesRegex(ValueError, "does not match dispatched"):
            evaluation_metrics(decisions[:2], statistics)

    def test_prediction_study_aggregates_full_policy_matrix(self):
        records = []
        for dispatch_policy in prediction_study.DISPATCH_POLICIES:
            for wall_time_policy in prediction_study.WALL_TIME_POLICIES:
                for case in ("ideal", "app_avg", "rajaperf"):
                    for run in range(1, 11):
                        records.append(
                            {
                                "dispatch_policy": dispatch_policy,
                                "wall_time_policy": wall_time_policy,
                                "case": case,
                                "run": run,
                                "jobs": 100,
                                "dropped_jobs": 0,
                                **{
                                    metric: float(run)
                                    for metric in prediction_study.METRICS
                                },
                            }
                        )
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory)
            summary = prediction_study.write_results(output, records)
            per_run = (output / "metrics_per_run.csv").read_bytes()
            complete_summary = (output / "summary.csv").read_bytes()
            aggregate_summary = (output / "summary_aggregate.csv").read_text()

        self.assertEqual(len(summary), 12)
        self.assertNotIn(b"\r\n", per_run)
        self.assertEqual(complete_summary, per_run)
        self.assertEqual(len(complete_summary.splitlines()), 121)
        self.assertEqual(len(aggregate_summary.splitlines()), 13)
        self.assertEqual(
            {
                (row["dispatch_policy"], row["wall_time_policy"])
                for row in summary
            },
            {
                (dispatch_policy, wall_time_policy)
                for dispatch_policy in prediction_study.DISPATCH_POLICIES
                for wall_time_policy in prediction_study.WALL_TIME_POLICIES
            },
        )

    def test_prediction_study_uses_named_relative_performance_tables(self):
        root = pathlib.Path("/source")
        cases = dict(prediction_study.prediction_cases(root, None))
        table_dir = root / "experimental/multi-cluster/relative_performance_tables"

        self.assertEqual(
            cases["app_avg"],
            table_dir / "application_average_relative_performance_borax.csv",
        )
        self.assertEqual(
            cases["rajaperf"],
            table_dir / "rajaperf_relative_performance_borax.csv",
        )

    def test_summary_plot_uses_only_adapted_limit_results(self):
        rows = [
            {"wall_time_policy": "adapted-limit", "case": "ideal"},
            {"wall_time_policy": "actual-duration", "case": "ideal"},
        ]

        self.assertEqual(prediction_study.rows_for_summary_plot(rows), rows[:1])

    def test_prediction_study_uses_analysis_policy_labels(self):
        records = []
        for dispatch_policy, case in (
            ("RelPerfOnly", "ideal"),
            ("WaitTimeOnly", "wait_time_only"),
            ("turnaround", "app_average_per_machine"),
            ("turnaround", "app_average_5_percent"),
        ):
            for run in range(1, 11):
                records.append(
                    {
                        "dispatch_policy": dispatch_policy,
                        "wall_time_policy": "adapted-limit",
                        "case": case,
                        "run": run,
                        "jobs": 100,
                        "dropped_jobs": 0,
                        **{metric: float(run) for metric in prediction_study.METRICS},
                    }
                )

        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory)
            prediction_study.write_results(output, records)
            summary = (output / "summary.md").read_text()

        self.assertIn("| RelPerfOnly | adapted-limit |", summary)
        self.assertIn("| WaitTimeOnly | adapted-limit | WaitTimeOnly |", summary)
        self.assertIn("| turnaround | adapted-limit | App average (100%) |", summary)
        self.assertIn("| turnaround | adapted-limit | App average (5% train) |", summary)
        self.assertNotIn("| wait |", summary)

    def test_prediction_study_command_uses_selected_systems(self):
        args = SimpleNamespace(
            launcher=["srun"],
            ranks=6,
            executable=pathlib.Path("/install/bin/mpi_performance_dispatch"),
            systems=pathlib.Path("/inputs/custom-machines.csv"),
            ground_truth=pathlib.Path("/inputs/custom-ground-truth.csv"),
            applications=pathlib.Path("/inputs/custom-applications.csv"),
            seed=7,
            max_time_limit=43200.0,
        )
        command = prediction_study.build_command(
            args,
            pathlib.Path("/source"),
            "turnaround",
            "adapted-limit",
            pathlib.Path("/inputs/prediction.csv"),
            pathlib.Path("/inputs/jobs.csv"),
            pathlib.Path("/outputs/dispatch.csv"),
        )

        systems_index = command.index("--systems")
        self.assertEqual(command[systems_index + 1], "/inputs/custom-machines.csv")
        ground_truth_index = command.index("--ground-truth")
        self.assertEqual(
            command[ground_truth_index + 1], "/inputs/custom-ground-truth.csv"
        )
        applications_index = command.index("--applications")
        self.assertEqual(
            command[applications_index + 1], "/inputs/custom-applications.csv"
        )

    def test_kt_study_blanks_nonpositive_predictions(self):
        truth = [{"App": "app", "Args": "-n 1", "Ranks": "2", "dane": "1.5"}]
        with tempfile.TemporaryDirectory() as directory:
            prediction = pathlib.Path(directory) / "prediction.csv"
            prediction.write_text(
                "source_machine,app,args,ranks,dane\n"
                "borax,APP,-n1,2,-0.25\n",
                encoding="utf-8",
            )
            rows, rejected = kt_study.load_prediction(
                prediction, truth, ["dane"]
            )

        self.assertEqual(
            rows, [{"App": "app", "Args": "-n 1", "Ranks": "2", "dane": ""}]
        )
        self.assertEqual(len(rejected), 1)
        self.assertEqual(rejected[0]["mode"], "dane")
        self.assertEqual(rejected[0]["value"], "-0.25")

    def test_kt_study_accepts_missing_prediction_workload(self):
        truth = [
            {"App": "app", "Args": "-n 1", "Ranks": "2", "dane": "1.5"},
            {"App": "app", "Args": "-n 2", "Ranks": "2", "dane": "2.0"},
        ]
        with tempfile.TemporaryDirectory() as directory:
            prediction = pathlib.Path(directory) / "prediction.csv"
            prediction.write_text(
                "app,args,ranks,dane\n"
                "app,-n1,2,1.25\n",
                encoding="utf-8",
            )
            rows, rejected = kt_study.load_prediction(
                prediction, truth, ["dane"]
            )

        self.assertEqual(rows[0]["dane"], "1.25")
        self.assertEqual(rows[1]["dane"], "")
        self.assertEqual(rejected, [])

    def test_kt_study_can_ignore_prediction_only_workloads(self):
        truth = [{"App": "app", "Args": "-n 1", "Ranks": "2", "dane": "1.5"}]
        with tempfile.TemporaryDirectory() as directory:
            prediction = pathlib.Path(directory) / "prediction.csv"
            prediction.write_text(
                "app,args,ranks,dane\n"
                "app,-n1,2,1.25\n"
                "extra,-n2,2,2.0\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "absent from ground truth"):
                kt_study.load_prediction(prediction, truth, ["dane"])
            rows, rejected = kt_study.load_prediction(
                prediction, truth, ["dane"], ignore_extra=True
            )

        self.assertEqual(rows[0]["dane"], "1.25")
        self.assertEqual(rejected, [])

    def test_kt_study_defaults_exclude_five_percent_app_average(self):
        truth = [
            {"App": "app", "Args": "-n 1", "Ranks": "2", "dane": "1.5"}
        ]
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            args = SimpleNamespace(
                case=None,
                systems=root / "systems.csv",
                ground_truth_source=root / "ground-truth.csv",
                output_dir=root / "output",
                fully_trained=root / "fully-trained.csv",
                knowledge_transfer_1=root / "kt-1.csv",
                knowledge_transfer_3=root / "kt-3.csv",
                knowledge_transfer_5=root / "kt-5.csv",
                app_average_5_percent=None,
                machine_rep=root / "machine-rep.csv",
                ignore_extra_predictions=False,
            )
            with (
                patch.object(kt_study, "configured_modes", return_value=["dane"]),
                patch.object(kt_study, "load_ground_truth", return_value=truth),
                patch.object(kt_study, "load_prediction", return_value=(truth, [])),
                patch.object(kt_study, "write_table"),
                patch.object(
                    kt_study.baselines,
                    "application_average_rows",
                    return_value=truth,
                ),
                patch.object(
                    kt_study.baselines, "rajaperf_speedups", return_value={}
                ),
                patch.object(kt_study.baselines, "rajaperf_rows", return_value=truth),
            ):
                _, cases, _ = kt_study.prepare_tables(args)

        self.assertEqual([case for case, _ in cases], list(kt_study.DEFAULT_CASES))
        self.assertNotIn("app_average_5_percent", {case for case, _ in cases})

    def test_wait_study_runs_one_case_per_trace(self):
        cases = [
            ("ideal", pathlib.Path("/inputs/ground-truth.csv")),
            ("fully_trained", pathlib.Path("/inputs/fully-trained.csv")),
        ]

        self.assertEqual(
            kt_study.cases_for_dispatch_policy("WaitTimeOnly", cases),
            [("wait_time_only", pathlib.Path("/inputs/ground-truth.csv"))],
        )
        self.assertIs(
            kt_study.cases_for_dispatch_policy("turnaround", cases), cases
        )

        self.assertEqual(
            prediction_study.cases_for_dispatch_policy("WaitTimeOnly", cases),
            [("wait_time_only", pathlib.Path("/inputs/ground-truth.csv"))],
        )
        self.assertIs(
            prediction_study.cases_for_dispatch_policy("turnaround", cases), cases
        )

    def test_kt_study_accepts_configured_jobs_per_trace(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            executable = root / "dispatcher"
            applications = root / "applications.csv"
            systems = root / "systems.csv"
            prediction = root / "prediction.csv"
            for path in (executable, applications, systems, prediction):
                path.write_text("fixture\n", encoding="utf-8")
            for index in range(10):
                (root / f"trace-{index:02d}.csv").write_text(
                    "submit_time,num_nodes,time_limit,duration\n"
                    "0,1,10,5\n"
                    "20,1,10,5\n",
                    encoding="utf-8",
                )
            args = SimpleNamespace(
                executable=executable,
                applications=applications,
                systems=systems,
                jobs_glob=str(root / "trace-*.csv"),
                jobs_per_trace=2,
            )

            traces = kt_study.validate_run_inputs(
                args, [("ideal", prediction)]
            )

        self.assertEqual(len(traces), 10)

    def test_prediction_study_accepts_configured_jobs_per_trace(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            experiment = root / "experimental/multi-cluster"
            experiment.mkdir(parents=True)
            executable = root / "dispatcher"
            systems = root / "systems.csv"
            applications = experiment / "apps.csv"
            prediction = root / "prediction.csv"
            for path in (executable, systems, applications, prediction):
                path.write_text("fixture\n", encoding="utf-8")
            for index in range(10):
                (root / f"trace-{index:02d}.csv").write_text(
                    "submit_time,num_nodes,time_limit,duration\n"
                    "0,1,10,5\n"
                    "20,1,10,5\n",
                    encoding="utf-8",
                )

            traces = prediction_study.validate_inputs(
                root,
                executable,
                systems,
                [("ideal", prediction)],
                str(root / "trace-*.csv"),
                2,
            )

        self.assertEqual(len(traces), 10)

if __name__ == "__main__":
    unittest.main()
