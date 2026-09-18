#!/usr/bin/env python3
"""Unit tests for decomp_extract.py's ROM-symlinking workaround, shared
by build_textures_o2r.py and build_objects_o2r.py."""

import tempfile
import unittest
from pathlib import Path
from unittest import mock

import decomp_extract


class RunExtractionModeTest(unittest.TestCase):
    def test_invokes_script_with_single_mode_argument(self):
        # Deliberately a single mode argument, not "mode <rom>" -- see
        # decomp_extract.py's module docstring for why the two-argument
        # form is unsafe (a bug in the vendored script's own argument
        # parsing).
        with tempfile.TemporaryDirectory() as tmp:
            decomp_dir = Path(tmp) / "external" / "007"
            (decomp_dir / "scripts").mkdir(parents=True)
            script = decomp_dir / "scripts" / "extract_baserom.u.sh"
            script.write_text("#!/bin/bash\n")

            rom_dir = Path(tmp) / "roms"
            rom_dir.mkdir()
            rom_path = rom_dir / "GoldenEye 007 (USA).z64"
            rom_path.write_bytes(b"fake rom")

            with mock.patch("subprocess.run") as mock_run:
                decomp_extract.run_extraction_mode(decomp_dir, rom_path, "files")

            mock_run.assert_called_once()
            args, kwargs = mock_run.call_args
            command = args[0]
            self.assertEqual(command, ["bash", str(script), "files"])
            self.assertEqual(kwargs["cwd"], decomp_dir)
            self.assertTrue(kwargs["check"])

            baserom_link = decomp_dir / "baserom.u.z64"
            self.assertTrue(baserom_link.is_symlink())
            self.assertEqual(baserom_link.resolve(), rom_path.resolve())

    def test_replaces_a_stale_baserom_symlink(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp_dir = Path(tmp) / "external" / "007"
            (decomp_dir / "scripts").mkdir(parents=True)
            (decomp_dir / "scripts" / "extract_baserom.u.sh").write_text("#!/bin/bash\n")

            stale_target = Path(tmp) / "old.z64"
            stale_target.write_bytes(b"stale")
            (decomp_dir / "baserom.u.z64").symlink_to(stale_target)

            rom_path = Path(tmp) / "baserom.u.z64.real"
            rom_path.write_bytes(b"fake rom")

            with mock.patch("subprocess.run"):
                decomp_extract.run_extraction_mode(decomp_dir, rom_path, "images")

            baserom_link = decomp_dir / "baserom.u.z64"
            self.assertEqual(baserom_link.resolve(), rom_path.resolve())

    def test_raises_clear_error_when_decomp_not_checked_out(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp_dir = Path(tmp) / "external" / "007"  # deliberately not created
            rom_path = Path(tmp) / "baserom.u.z64"
            rom_path.write_bytes(b"fake rom")

            with self.assertRaises(FileNotFoundError):
                decomp_extract.run_extraction_mode(decomp_dir, rom_path, "files")


if __name__ == "__main__":
    unittest.main()
