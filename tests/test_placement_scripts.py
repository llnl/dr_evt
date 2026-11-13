#!/usr/bin/env python3
"""Regression checks for portable placement validation and timing scripts."""

import argparse
import csv
import importlib.util
import json
import os
import shlex
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "validation/flux"))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


benchmark = load("placement_benchmark", REPO / "validation/performance/simulation/placement-model/benchmark_placement.py")
paired = load("placement_paired", REPO / "validation/flux/test_paired.py")
comparison = load("placement_comparison", REPO / "validation/flux/compare_placement_model.py")


class PlacementScriptTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="dr-evt-placement-scripts-")
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def write_csv(self, name, header, rows):
        path = self.root / name
        with path.open("w", newline="") as stream:
            writer = csv.writer(stream, lineterminator="\n")
            writer.writerow(header)
            writer.writerows(rows)
        return path

    def example(self, bad_duration=False, bad_history=False, overlap=False):
        trace = self.write_csv("input.csv", ["job_submit_time", "num_nodes", "time_limit"],
                               [(0, 2, 2), (1, 2, 1)])
        expected = benchmark.read_trace(trace, 2)
        schedule = self.write_csv("schedule.csv", ["job_submit_time", "begin_time", "end_time", "num_nodes", "time_limit"],
                                  [(0, 0, 3 if bad_duration else 2, 2, 2),
                                   (1, 1 if overlap else 2, 2 if overlap else 3, 2, 1)])
        history = self.write_csv("resources.csv", ["time", "free_nodes", "allocated_nodes"],
                                 [(0, 2, 0), (0, 0, 2), (2, 2, 0), (2, 0, 2),
                                  (3, 0 if bad_history else 2, 2 if bad_history else 0)])
        return schedule, history, expected

    def test_seeded_input_matches_existing_profile(self):
        trace = self.root / "jobs.csv"
        benchmark.generate_trace(trace, 20000, 2718)
        self.assertEqual(benchmark.digest(trace),
                         "235c1fa1e259081dc207dea32f9cbecf6c723ac2c914a25b92220f91e4e17508")
        self.assertEqual(sum(benchmark.read_trace(trace, 500).values()), 20000)

    def test_audit_groups_equal_time_release_and_start(self):
        schedule, history, expected = self.example()
        self.assertEqual(benchmark.audit(schedule, history, 2, expected)["peak_allocated_nodes"], 2)

    def test_audit_rejects_bad_durations_overlap_and_history(self):
        for options in ({"bad_duration": True}, {"overlap": True}, {"bad_history": True}):
            with self.subTest(options=options):
                schedule, history, expected = self.example(**options)
                with self.assertRaises(ValueError):
                    benchmark.audit(schedule, history, 2, expected)

    def test_audit_detects_omitted_job(self):
        schedule, history, expected = self.example()
        with schedule.open("a") as stream:
            stream.write("1,3,4,2,1\n")
        with self.assertRaisesRegex(ValueError, "omitted, duplicated"):
            benchmark.audit(schedule, history, 2, expected)

    def test_input_rejects_empty_oversized_and_fractional_requests(self):
        for row in [(0, 0, 1), (0, 3, 1), (0.5, 1, 1), (0, 1, 0), (0, 1, "nan")]:
            trace = self.write_csv("bad.csv", ["job_submit_time", "num_nodes", "time_limit"], [row])
            with self.subTest(row=row), self.assertRaises(ValueError):
                benchmark.read_trace(trace, 2)

    def test_failure_reports_are_saved(self):
        output = self.root / "failed.json"
        result = subprocess.run([sys.executable, str(Path(benchmark.__file__)), "--binary",
                                 str(self.root / "missing"), "--generate-trace", "--output", str(output)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(json.loads(output.read_text())["status"], "failed")
        # Rerunning must preserve the original failure evidence.
        second = subprocess.run([sys.executable, str(Path(benchmark.__file__)), "--binary",
                                 str(self.root / "missing"), "--generate-trace", "--output", str(output)],
                                capture_output=True, text=True)
        self.assertEqual(second.returncode, 2)

    def test_command_keeps_timing_uninstrumented_and_legacy_binary_compatible(self):
        args = argparse.Namespace(nodes=500, reservation_depth=32, queue_depth=32)
        base = benchmark.command_for(Path("baseline"), Path("input.csv"), Path("jobs.csv"),
                                     Path("resources.csv"), args, "hybrid", None, None, False)
        self.assertNotIn("--resource_model", base)
        self.assertNotIn("--resource_model_diagnostics", base)
        timed = benchmark.command_for(Path("candidate"), Path("input.csv"), Path("jobs.csv"),
                                      Path("resources.csv"), args, "hybrid", "placement", "all", True)
        self.assertIn("--placement_coarse_filter", timed)
        self.assertNotIn("--resource_model_diagnostics", timed)

    def test_matrix_controls_all_features_and_both_filters(self):
        fixture = REPO / "validation/flux/evidence/placement-oracle.json"
        cases = json.loads(fixture.read_text())["cases"]
        calls = []

        def driver(command, **kwargs):
            calls.append((kwargs["env"]["DR_EVT_PLACEMENT_OPTIMIZATIONS"], command[1:]))
            case = cases[(len(calls) - 1) // 12]
            records = [{"schedule": entry} for entry in case["schedule"]]
            records += [{"snapshot": entry} for entry in case["snapshots"]]
            return subprocess.CompletedProcess(command, 0, "\n".join(json.dumps(r) for r in records), "")

        with patch.object(comparison.subprocess, "run", side_effect=driver):
            results = comparison.compare(Path("driver"), fixture, list(benchmark.FEATURES))
        self.assertEqual(len(results), len(cases) * 12)
        self.assertEqual(set(features for features, _ in calls), set(benchmark.FEATURES))
        self.assertEqual(set(tuple(args) for _, args in calls), {(), ("--coarse-filter",)})

    def test_existing_native_adapter_selects_placement_without_changing_workload(self):
        calls = []

        def simulator(command, **kwargs):
            calls.append((command, kwargs))
            output = Path(command[command.index('--outfile') + 1])
            resources = Path(command[command.index('--resource_trace') + 1])
            output.write_text('job_submit_time,begin_time,end_time,num_nodes,time_limit\n0,0,2,1,5\n')
            resources.write_text('time,free_nodes,allocated_nodes\n0,2,0\n0,1,1\n2,2,0\n')
            return subprocess.CompletedProcess(command, 0)

        with patch.object(paired.subprocess, 'run', side_effect=simulator):
            for model in ('capacity', 'placement'):
                directory = self.root / model
                directory.mkdir()
                paired.simulator_case(directory, [(0, 1, 5, 2)], Path('simulator'), 'hybrid',
                                      64, 32, 2, resource_model=model, timeout=900)
        self.assertIn('--per_pass_accounting', calls[0][0])
        self.assertNotIn('--resource_model', calls[0][0])
        self.assertNotIn('--per_pass_accounting', calls[1][0])
        self.assertIn('--resource_model', calls[1][0])
        self.assertEqual(calls[1][1]['timeout'], 900)
        self.assertEqual((self.root / 'capacity/dr_evt-input.csv').read_bytes(),
                         (self.root / 'placement/dr_evt-input.csv').read_bytes())

    def test_slurm_virtual_node_liveness_overlay_is_scoped_and_restored(self):
        script = (REPO / 'validation/slurm/licenses/build_event.sh').read_text()
        self.assertIn("name = 'src/slurmctld/ping_nodes.c'", script)
        self.assertIn('dlsym(RTLD_DEFAULT, "validation_clock_active")', script)
        self.assertIn('show HEAD:src/slurmctld/ping_nodes.c', script)
        self.assertIn("name = 'src/slurmctld/agent.c'", script)
        self.assertIn('REQUEST_BATCH_JOB_LAUNCH', script)
        self.assertIn('REQUEST_TERMINATE_JOB', script)
        self.assertIn('show HEAD:src/slurmctld/agent.c', script)
        clock = (REPO / 'validation/slurm/licenses/clock.c').read_text()
        self.assertIn('int validation_clock_active(void)', clock)

    def test_slurm_adapter_uses_compute_side_completion_rpc(self):
        client = (REPO / 'validation/slurm/licenses/client.c').read_text()
        self.assertIn('REQUEST_COMPLETE_BATCH_SCRIPT', client)
        self.assertIn('MESSAGE_EPILOG_COMPLETE', client)
        self.assertNotIn('slurm_complete_job(', client)

    def test_server_images_always_install_and_verify_matplotlib(self):
        for relative in ('containers/podman/server/Containerfile',
                         'containers/docker/server/Dockerfile'):
            image = (REPO / relative).read_text()
            common, optional = image.split('&& { { [ "$WITH_SLURM"', 1)
            self.assertIn('python3-matplotlib', common)
            self.assertNotIn('python3-matplotlib', optional.split(
                '&& rm -rf /var/lib/apt/lists/*', 1)[0])
            self.assertIn("RUN python3 -c 'import matplotlib'", image)

    def test_existing_benchmark_keeps_its_default_input(self):
        stub = self.root / "capture.py"
        stub.write_text("import json, sys\nif sys.argv[1] == '-c':\n    exec(sys.argv[2])\nelse:\n    print(json.dumps(sys.argv[1:]))\n")
        interpreter = self.root / "capture-python"
        interpreter.write_text("#!/bin/sh\nexec " + shlex.quote(sys.executable) + " " +
                               shlex.quote(str(stub)) + ' "$@"\n')
        interpreter.chmod(0o755)
        environment = dict(os.environ, SIMULATOR=sys.executable, PYTHON_EXECUTABLE=str(interpreter))
        result = subprocess.run(["bash", "tests/benchmark_wait_queues.sh", "--resource-model", "placement"],
                                cwd=REPO, env=environment, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = next(json.loads(line) for line in result.stdout.splitlines() if line.startswith("["))
        self.assertEqual(arguments[arguments.index("--trace") + 1],
                         "tests/test_traces/scale/huge_10000jobs.csv")
        self.assertNotIn("--generate-trace", arguments)

    def test_timeout_retains_failure_output(self):
        error = subprocess.TimeoutExpired(["simulator"], 1, output=b"partial", stderr=b"failure")
        log = self.root / "timeout.log"
        with patch.object(benchmark.subprocess, "run", side_effect=error):
            with self.assertRaises(RuntimeError):
                benchmark.execute(["simulator"], 1, log)
        self.assertIn("partial", log.read_text())


if __name__ == "__main__":
    unittest.main()
