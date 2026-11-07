#!/usr/bin/env python3
"""Check build-header discovery and fail-closed reservation mode detection."""

from pathlib import Path
import contextlib
import importlib.util
import io
import sys
import tempfile
import unittest
from unittest.mock import patch

from read_build_config import reservation_barrier, require_reservation_barrier


class BuildConfigTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve()
        self.binary = self.root / "build" / "simulator"
        self.binary.parent.mkdir()
        self.binary.touch()
        self.header = self.binary.parent / "dr_evt_config.hpp"

    def test_enabled_and_wrong_expectation(self):
        self.header.write_text("#define DR_EVT_LEGACY_RESERVATION_BARRIER 1\n")
        self.assertEqual(reservation_barrier(self.binary), (True, self.header))
        with self.assertRaisesRegex(ValueError, "expected OFF"):
            require_reservation_barrier(self.binary, False)

    def test_disabled_is_explicit_undef(self):
        self.header.write_text("/* #undef DR_EVT_LEGACY_RESERVATION_BARRIER */\n")
        self.assertEqual(reservation_barrier(self.binary), (False, self.header))
        require_reservation_barrier(self.binary, False)

    def test_old_header_is_unknown_not_disabled(self):
        self.header.write_text("#define DR_EVT_LEGACY_QUEUE_INPUT 0\n")
        with self.assertRaisesRegex(ValueError, "does not record"):
            reservation_barrier(self.binary)

    def test_missing_header(self):
        with self.assertRaisesRegex(ValueError, "Cannot find"):
            reservation_barrier(self.binary)

    def test_install_header_and_symlinked_binary(self):
        binary = self.root / "install" / "bin" / "simulator"
        binary.parent.mkdir(parents=True)
        binary.touch()
        header = self.root / "install" / "include" / "dr_evt_config.hpp"
        header.parent.mkdir()
        header.write_text("#define DR_EVT_LEGACY_RESERVATION_BARRIER 1\n")
        link = self.root / "simulator"
        link.symlink_to(binary)
        self.assertEqual(reservation_barrier(link), (True, header))

    def test_explicit_header(self):
        header = self.root / "custom-include" / "dr_evt_config.hpp"
        header.parent.mkdir()
        header.write_text("/* #undef DR_EVT_LEGACY_RESERVATION_BARRIER */\n")
        self.assertEqual(reservation_barrier(self.binary, header), (False, header))


class RapsBuildConfigTests(unittest.TestCase):
    setUp = BuildConfigTests.setUp

    @classmethod
    def setUpClass(cls):
        script = Path(__file__).resolve().parents[1] / "scripts" / "compare_with_raps.py"
        spec = importlib.util.spec_from_file_location("raps_comparison", script)
        cls.comparison = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.comparison)

    def invoke(self, *extra):
        args = ["compare_with_raps.py", "--input", "unused.csv", "--total_nodes", "4",
                "--simulator", str(self.binary), "--workdir", str(self.root / "output")]
        with patch.object(sys, "argv", args + list(extra)):
            return self.comparison.main()

    def test_raps_rejects_disabled_before_running(self):
        self.header.write_text("/* #undef DR_EVT_LEGACY_RESERVATION_BARRIER */\n")
        errors = io.StringIO()
        with patch.object(self.comparison, "run_dr_evt") as native, \
                patch.object(self.comparison, "run_raps") as reference, \
                contextlib.redirect_stderr(errors), self.assertRaises(SystemExit):
            self.invoke("--dr-evt-output", "unused-output.csv")
        native.assert_not_called()
        reference.assert_not_called()
        self.assertIn("expected ON", errors.getvalue())
        self.assertFalse((self.root / "output").exists())

    def test_raps_accepts_enabled_build(self):
        self.header.write_text("#define DR_EVT_LEGACY_RESERVATION_BARRIER 1\n")
        with patch.object(self.comparison, "clamp_actual", return_value=1), \
                patch.object(self.comparison, "run_dr_evt", return_value=[(0, 0, 1, 5)]) as native, \
                patch.object(self.comparison, "run_raps", return_value={0: (0, 0)}) as reference, \
                contextlib.redirect_stdout(io.StringIO()):
            self.invoke()
        native.assert_called_once()
        reference.assert_called_once()

    def test_preparation_requires_no_simulator_build(self):
        with patch.object(self.comparison, "clamp_actual", return_value=1), \
                patch.object(self.comparison, "run_dr_evt") as native, \
                patch.object(self.comparison, "run_raps") as reference, \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(self.invoke("--prepare-only"), 0)
        native.assert_not_called()
        reference.assert_not_called()


if __name__ == "__main__":
    unittest.main()
