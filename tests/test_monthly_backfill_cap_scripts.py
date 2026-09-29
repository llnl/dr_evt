#!/usr/bin/env python3
"""Regression tests for monthly power-cap experiment scripts."""

import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SUITE = (ROOT / "experimental/fugaku-power/"
         "individual-monthly-warm-start-backfill-cap-jobs")
FCFS_SUITE = (ROOT / "experimental/fugaku-power/"
              "individual-monthly-warm-start-fcfs-cap-jobs")
BOTH_SUITE = (ROOT / "experimental/fugaku-power/"
              "individual-monthly-warm-start-both-power-caps-jobs")
BASE_RUNNER = (ROOT / "experimental/fugaku-power/"
               "individual-monthly-warm-start-jobs/run_one.sh")


class MonthlyBackfillCapScriptsTest(unittest.TestCase):
    def test_generator_creates_isolated_capped_job_set(self):
        spec = importlib.util.spec_from_file_location(
            "generate_capped_monthly_jobs", SUITE / "generate_job_set.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            trace_dir = temporary / "traces"
            output_dir = temporary / "jobs"
            trace_dir.mkdir()
            (trace_dir / "23_07_scheduling_trace.csv").touch()

            months, jobs = module.generate(trace_dir, output_dir)
            self.assertEqual((months, jobs), (1, 15))
            manifest = (output_dir / "manifest.tsv").read_text()
            self.assertEqual(manifest.count("\neasypower-cap\t"), 15)
            self.assertNotIn("\ncapacity\t", manifest)
            self.assertNotIn("\nbaseline\t", manifest)
            self.assertFalse((output_dir / "jobs/capacity").exists())
            self.assertFalse((output_dir / "jobs/baseline").exists())
            generated = (output_dir / "jobs/easypower/"
                         "easypower-cap-2023-07-n64-t1h.slurm")
            self.assertTrue(generated.stat().st_mode & 0o100)
            self.assertIn("'easypower' '2023-07' '64' '3600' '3600s'",
                          generated.read_text())

    def test_additional_generators_create_correct_variants(self):
        variants = (
            (FCFS_SUITE, "easypower-fcfs-cap", "easypower-fcfs-cap"),
            (BOTH_SUITE, "easypower-both-caps", "easypower-both-caps"),
        )
        for index, (suite, manifest_kind, filename_prefix) in enumerate(variants):
            with self.subTest(manifest_kind=manifest_kind), \
                    tempfile.TemporaryDirectory() as temporary:
                temporary = Path(temporary)
                trace_dir = temporary / "traces"
                output_dir = temporary / "jobs"
                trace_dir.mkdir()
                (trace_dir / "23_07_scheduling_trace.csv").touch()
                spec = importlib.util.spec_from_file_location(
                    f"generate_monthly_jobs_{index}", suite / "generate_job_set.py")
                module = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)

                self.assertEqual(module.generate(trace_dir, output_dir), (1, 15))
                manifest = (output_dir / "manifest.tsv").read_text()
                self.assertEqual(manifest.count(f"\n{manifest_kind}\t"), 15)
                self.assertNotIn("\ncapacity\t", manifest)
                self.assertNotIn("\nbaseline\t", manifest)
                generated = (output_dir / "jobs/easypower" /
                             f"{filename_prefix}-2023-07-n64-t1h.slurm")
                self.assertTrue(generated.stat().st_mode & 0o100)
                self.assertIn(str(suite / "run_one.sh"), generated.read_text())
                self.assertFalse(
                    (output_dir / "historical-infile-lists").exists())

    def test_wrappers_force_exact_cap_combinations(self):
        variants = ((SUITE, "1", "0"), (FCFS_SUITE, "0", "1"),
                    (BOTH_SUITE, "1", "1"))
        for suite, expected_backfill, expected_fcfs in variants:
            with self.subTest(suite=suite.name), \
                    tempfile.TemporaryDirectory() as temporary:
                temporary = Path(temporary)
                capture = temporary / "capture.txt"
                runner = temporary / "runner.sh"
                runner.write_text(
                    "#!/usr/bin/env bash\n"
                    "printf '%s\\n' \"$CAP_BACKFILL_POWER\" "
                    "\"$CAP_FCFS_POWER\" \"$OUTPUT_ROOT\" "
                    "\"$SHARED_OUTPUT_ROOT\" \"$*\" "
                    ">\"$CAPTURE\"\n")
                runner.chmod(0o700)
                environment = os.environ.copy()
                environment.update({
                    "MONTHLY_WARM_START_RUNNER": str(runner),
                    "CAP_BACKFILL_POWER": "9",
                    "CAP_FCFS_POWER": "9",
                    "OUTPUT_ROOT": str(temporary / "custom-results"),
                    "CAPTURE": str(capture),
                })
                subprocess.run(
                    [str(suite / "run_one.sh"), "easypower", "2023-07",
                     "64", "3600", "3600s"], check=True, env=environment)
                self.assertEqual(
                    capture.read_text().splitlines(),
                    [expected_backfill, expected_fcfs,
                     str(temporary / "custom-results"),
                     str(ROOT / "experimental/fugaku-power/results/"
                         "individual-monthly-warm-start-sweep"),
                     "easypower 2023-07 64 3600 3600s"])

                rejected = subprocess.run(
                    [str(suite / "run_one.sh"), "baseline", "2023-07"],
                    check=False, env=environment, capture_output=True,
                    text=True)
                self.assertEqual(rejected.returncode, 2)
                self.assertIn("reuses capacity and baseline", rejected.stderr)

    def test_submitters_refuse_missing_shared_results(self):
        for suite in (SUITE, FCFS_SUITE, BOTH_SUITE):
            with self.subTest(suite=suite.name), \
                    tempfile.TemporaryDirectory() as temporary:
                temporary = Path(temporary)
                environment = os.environ.copy()
                environment.update({
                    "OUTPUT_ROOT": str(temporary / "variant-results"),
                    "SHARED_OUTPUT_ROOT": str(temporary / "missing-shared"),
                })
                result = subprocess.run(
                    [str(suite / "run_n_left.sh"), "1"], check=False,
                    env=environment, capture_output=True, text=True)
                self.assertEqual(result.returncode, 2)
                self.assertIn("shared input, capacity, or baseline is incomplete",
                              result.stderr)

    def test_fcfs_year_submitters_select_requested_years(self):
        cases = (
            ("run_2022.sh", ("2022-",)),
            ("run_2023_2024.sh", ("2023-", "2024-")),
        )
        for script_name, expected_prefixes in cases:
            with self.subTest(script=script_name), \
                    tempfile.TemporaryDirectory() as temporary:
                temporary = Path(temporary)
                shared = temporary / "shared"
                first_month = "2022-01" if script_name == "run_2022.sh" \
                    else "2023-01"
                month_root = shared / first_month
                for directory in (month_root / "capacity-detection",
                                  month_root / "easy-baseline",
                                  month_root / "input"):
                    directory.mkdir(parents=True)
                (month_root / "capacity-detection/.complete").touch()
                (month_root / "capacity-detection/"
                 "resource_capacity_without_reduced_capacity.csv").write_text(
                     "capacity\n")
                (month_root / "easy-baseline/.complete").touch()
                (month_root / "easy-baseline/jobs.csv").write_text("jobs\n")
                (month_root / "easy-baseline/resources.csv").write_text(
                    "resources\n")
                (month_root / "input/warm-start-scheduling_trace.csv").write_text(
                    "trace\n")
                (month_root / "input/infile.txt").write_text("input\n")

                bin_dir = temporary / "bin"
                bin_dir.mkdir()
                capture = temporary / "submitted.txt"
                sbatch = bin_dir / "sbatch"
                sbatch.write_text(
                    "#!/usr/bin/env bash\n"
                    "printf '%s\\n' \"$1\" >>\"$CAPTURE\"\n")
                sbatch.chmod(0o700)
                environment = os.environ.copy()
                environment.update({
                    "PATH": f"{bin_dir}:{environment['PATH']}",
                    "CAPTURE": str(capture),
                    "OUTPUT_ROOT": str(temporary / "results"),
                    "SHARED_OUTPUT_ROOT": str(shared),
                })

                result = subprocess.run(
                    [str(FCFS_SUITE / script_name), "2"], check=True,
                    env=environment, capture_output=True, text=True)
                submitted = capture.read_text().splitlines()
                self.assertEqual(len(submitted), 2)
                self.assertIn("Submitted 2 jobs", result.stdout)
                self.assertTrue(all(
                    any(prefix in path for prefix in expected_prefixes)
                    for path in submitted))

    def test_fcfs_submitter_rejects_more_than_60_jobs(self):
        result = subprocess.run(
            [str(FCFS_SUITE / "run_2022.sh"), "61"], check=False,
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("COUNT_FROM_1_TO_60", result.stderr)

    def run_shared_runner(self, cap_backfill_power, cap_fcfs_power):
        temporary_context = tempfile.TemporaryDirectory()
        self.addCleanup(temporary_context.cleanup)
        temporary = Path(temporary_context.name)
        output_root = temporary / "variant-results"
        shared_output_root = temporary / "shared-results"
        month_root = shared_output_root / "2023-07"
        capacity_dir = month_root / "capacity-detection"
        input_dir = month_root / "input"
        baseline_dir = month_root / "easy-baseline"
        historical_dir = temporary / "historical"
        workload_dir = temporary / "workload"
        bin_dir = temporary / "bin"
        for directory in (capacity_dir, input_dir, baseline_dir, historical_dir,
                          workload_dir, bin_dir):
            directory.mkdir(parents=True)
        (capacity_dir / ".complete").touch()
        (capacity_dir / "resource_capacity_without_reduced_capacity.csv").write_text(
            "time,total_nodes\n0,1\n")
        (baseline_dir / ".complete").touch()
        (baseline_dir / "jobs.csv").write_text("baseline jobs\n")
        (baseline_dir / "resources.csv").write_text("baseline resources\n")
        (input_dir / "warm-start-scheduling_trace.csv").write_text("input\n")
        (input_dir / "infile.txt").write_text("input\n")
        (input_dir / "23_07_scheduling_trace.csv").write_text("history\n")

        capture = temporary / "driver-arguments.txt"
        driver = bin_dir / "driver"
        driver.write_text(
            "#!/usr/bin/env bash\n"
            "printf '%s\\n' \"$@\" >\"$CAPTURE\"\n"
            "printf 'jobs\\n' >\"$3/jobs.csv\"\n"
            "printf 'resources\\n' >\"$3/resources.csv\"\n"
            "printf 'telemetry\\n' >\"$3/target_horizon.csv\"\n")
        driver.chmod(0o700)
        module = bin_dir / "module"
        module.write_text("#!/usr/bin/env bash\nexit 0\n")
        module.chmod(0o700)

        environment = os.environ.copy()
        environment.update({
            "PATH": f"{bin_dir}:{environment['PATH']}",
            "CAPTURE": str(capture),
            "CAP_BACKFILL_POWER": str(cap_backfill_power),
            "CAP_FCFS_POWER": str(cap_fcfs_power),
            "OUTPUT_ROOT": str(output_root),
            "SHARED_OUTPUT_ROOT": str(shared_output_root),
            "HISTORICAL_TRACE_DIR": str(historical_dir),
            "WORKLOAD_TRACE_DIR": str(workload_dir),
            "EASYPOWER_DRIVER": str(driver),
            "TOTAL_NODES": "1",
        })
        subprocess.run(
            [str(BASE_RUNNER), "easypower", "2023-07", "64", "3600",
             "3600s"], check=True, env=environment)
        return capture.read_text().splitlines()

    def test_shared_runner_passes_all_cap_combinations(self):
        combinations = (
            (0, 0, []),
            (1, 0, ["--cap_backfill_power"]),
            (0, 1, ["--cap_fcfs_power"]),
            (1, 1, ["--cap_backfill_power", "--cap_fcfs_power"]),
        )
        for backfill, fcfs, expected in combinations:
            with self.subTest(backfill=backfill, fcfs=fcfs):
                arguments = self.run_shared_runner(backfill, fcfs)
                self.assertEqual(arguments[0], "easypower-progressive")
                self.assertIn("/shared-results/2023-07/input/infile.txt",
                              arguments[1])
                self.assertIn("/variant-results/2023-07/job-window-64/",
                              arguments[2])
                actual = [argument for argument in arguments
                          if argument.startswith("--cap_")]
                self.assertEqual(actual, expected)


if __name__ == "__main__":
    unittest.main()
