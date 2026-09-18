#!/usr/bin/env python3
"""Unit tests for build_objects_o2r.py's orchestration logic: given no
real ROM or decomp toolchain, these stub out the extraction subprocess
and check the driver wires paths correctly and hands off to
pack_objects with the right locations."""

import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

import build_objects_o2r
from o2r_writer import OTR_HEADER_SIZE, unpack_blob_payload


class ExtractFilesTest(unittest.TestCase):
    """extract_files() is a thin wrapper around
    decomp_extract.run_extraction_mode() -- see test_decomp_extract.py
    for coverage of the ROM-symlinking/argument-wiring behavior itself."""

    def test_delegates_to_run_extraction_mode_with_files_mode(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp_dir = Path(tmp) / "external" / "007"
            rom_path = Path(tmp) / "baserom.u.z64"

            with mock.patch("build_objects_o2r.run_extraction_mode") as mock_run:
                build_objects_o2r.extract_files(decomp_dir, rom_path)

            mock_run.assert_called_once_with(decomp_dir, rom_path, "files")


class BuildObjectsO2rTest(unittest.TestCase):
    def test_packs_extracted_output_into_archive(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp_dir = Path(tmp) / "external" / "007"
            (decomp_dir / "scripts").mkdir(parents=True)

            # Simulate what a real extraction run would have produced,
            # since we're stubbing subprocess.run instead of actually
            # invoking bash/the decomp toolchain.
            (decomp_dir / "scripts" / "filelist.u.csv").write_text(
                "7261072,9344,assets/obseg/chr/CarmourguardZ.bin,1,1\n"
                "7782256,2576,assets/obseg/gun/Gak47Z.bin,1,1\n"
            )
            chr_dir = decomp_dir / "assets" / "obseg" / "chr"
            chr_dir.mkdir(parents=True)
            (chr_dir / "CarmourguardZ.bin").write_bytes(b"ARMOURGUARD")
            gun_dir = decomp_dir / "assets" / "obseg" / "gun"
            gun_dir.mkdir(parents=True)
            (gun_dir / "Gak47Z.bin").write_bytes(b"AK47")

            rom_path = Path(tmp) / "baserom.u.z64"
            rom_path.write_bytes(b"fake rom")
            output_path = Path(tmp) / "out.o2r"

            with mock.patch("build_objects_o2r.run_extraction_mode"):
                missing = build_objects_o2r.build_objects_o2r(decomp_dir, rom_path, output_path)

            self.assertEqual(missing, [])
            with zipfile.ZipFile(output_path) as zf:
                self.assertEqual(sorted(zf.namelist()), ["obseg/chr/CarmourguardZ", "obseg/gun/Gak47Z"])
                self.assertEqual(unpack_blob_payload(zf.read("obseg/chr/CarmourguardZ")[OTR_HEADER_SIZE:]), b"ARMOURGUARD")


if __name__ == "__main__":
    unittest.main()
