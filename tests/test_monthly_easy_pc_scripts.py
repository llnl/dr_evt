#!/usr/bin/env python3
"""Regression tests for monthly EASY+PC experiment scripts."""

import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SUITE = ROOT / "experimental/fugaku-power/individual-monthly-warm-start-easy-pc-jobs"


def load_generator():
    specification = importlib.util.spec_from_file_location(
        "generate_easy_pc_jobs", SUITE / "generate_job_set.py")
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


class MonthlyEASYPCScriptsTest(unittest.TestCase):
    def test_generator_creates_mean_and_max_jobs(self):
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            traces = temporary / "traces"
            output = temporary / "suite"
            traces.mkdir()
            for name in ("22_01_scheduling_trace.csv",
                         "23_06_scheduling_trace.csv"):
                (traces / name).write_text("header\n")

            months, jobs = load_generator().generate(traces, output)
            self.assertEqual((months, jobs), (2, 4))
            manifest = (output / "manifest.tsv").read_text()
            self.assertIn("easy-pc\t2022-01\tmean", manifest)
            self.assertIn("easy-pc\t2022-01\tmax", manifest)
            self.assertIn("easy-pc\t2023-06\tmean", manifest)
            self.assertIn("easy-pc\t2023-06\tmax", manifest)
            generated = sorted((output / "jobs").glob("*.slurm"))
            self.assertEqual(len(generated), 4)
            self.assertTrue(all(path.stat().st_mode & 0o100
                                for path in generated))

    def test_submitter_checks_shared_artifacts_and_skips_complete_runs(self):
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            shared = temporary / "shared"
            output = temporary / "output"
            month = "2023-06"
            shared_month = shared / month
            for directory in (shared_month / "capacity-detection",
                              shared_month / "easy-baseline",
                              shared_month / "input"):
                directory.mkdir(parents=True)
            (shared_month / "capacity-detection/.complete").touch()
            (shared_month / "capacity-detection/"
             "resource_capacity_without_reduced_capacity.csv").write_text(
                 "capacity\n")
            (shared_month / "easy-baseline/.complete").touch()
            (shared_month / "easy-baseline/jobs.csv").write_text("jobs\n")
            (shared_month / "easy-baseline/resources.csv").write_text(
                "resources\n")
            (shared_month / "input/warm-start-scheduling_trace.csv").write_text(
                "trace\n")
            (shared_month / "input/infile.txt").write_text("input\n")

            complete = output / month / "easy-pc-mean"
            complete.mkdir(parents=True)
            (complete / ".complete").touch()
            (complete / "jobs.csv").write_text("jobs\n")
            (complete / "resources.csv").write_text("resources\n")

            manifest = temporary / "manifest.tsv"
            manifest.write_text(
                "kind\tmonth\tpower_mode\tscript\tprerequisite\n"
                "easy-pc\t2023-06\tmean\tjobs/mean.slurm\tshared\n"
                "easy-pc\t2023-06\tmax\tjobs/max.slurm\tshared\n")
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
                "OUTPUT_ROOT": str(output),
                "SHARED_OUTPUT_ROOT": str(shared),
                "MANIFEST": str(manifest),
            })
            result = subprocess.run(
                [str(SUITE / "run_n_left.sh"), "2"], check=True,
                env=environment, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, universal_newlines=True)
            self.assertEqual(capture.read_text().splitlines(),
                             [str(SUITE / "jobs/max.slurm")])
            self.assertIn("Submitted 1 jobs", result.stdout)

    def test_runner_rejects_invalid_mode(self):
        result = subprocess.run(
            [str(SUITE / "run_one.sh"), "median", "2023-06"],
            check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            universal_newlines=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("Usage:", result.stderr)


if __name__ == "__main__":
    unittest.main()
