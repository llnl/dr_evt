#!/usr/bin/env python3
"""Check build-header discovery and fail-closed reservation mode detection."""

from pathlib import Path
import tempfile
import unittest

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


if __name__ == "__main__":
    unittest.main()
