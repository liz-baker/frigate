#!/usr/bin/env python3
"""Unit tests for build_textures_o2r.py's orchestration logic: given
no real ROM or decomp toolchain, these stub out the extraction
subprocess and check the driver wires arguments/paths correctly and
hands off to pack_textures with the right locations."""

import tempfile
import unittest
from pathlib import Path
from unittest import mock

import build_textures_o2r
import zipfile
from o2r_writer import OTR_HEADER_SIZE, unpack_blob_payload


class ExtractImagesTest(unittest.TestCase):
    """extract_images() is now a thin wrapper around
    decomp_extract.run_extraction_mode() -- see test_decomp_extract.py
    for coverage of the ROM-symlinking/argument-wiring behavior itself.
    This just checks the wrapper delegates with the right mode."""

    def test_delegates_to_run_extraction_mode_with_images_mode(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp_dir = Path(tmp) / "external" / "007"
            rom_path = Path(tmp) / "baserom.u.z64"

            with mock.patch("build_textures_o2r.run_extraction_mode") as mock_run:
                build_textures_o2r.extract_images(decomp_dir, rom_path)

            mock_run.assert_called_once_with(decomp_dir, rom_path, "images")


class BuildTexturesO2rTest(unittest.TestCase):
    def test_packs_extracted_output_into_archive(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp_dir = Path(tmp) / "external" / "007"
            (decomp_dir / "scripts").mkdir(parents=True)
            (decomp_dir / "scripts" / "extract_baserom.u.sh").write_text("#!/bin/bash\n")

            # Simulate what a real extraction run would have produced,
            # since we're stubbing subprocess.run instead of actually
            # invoking bash/the decomp toolchain.
            assets_dir = decomp_dir / "assets"
            assets_dir.mkdir()
            (assets_dir / "images.def").write_text("IMAGE(TEX_A, 0x4)\n")
            split_dir = assets_dir / "images" / "split"
            split_dir.mkdir(parents=True)
            (split_dir / "TEX_A.bin").write_bytes(b"DATA")

            rom_path = Path(tmp) / "baserom.u.z64"
            rom_path.write_bytes(b"fake rom")
            output_path = Path(tmp) / "out.o2r"

            with mock.patch("subprocess.run"):
                missing = build_textures_o2r.build_textures_o2r(decomp_dir, rom_path, output_path)

            self.assertEqual(missing, [])
            with zipfile.ZipFile(output_path) as zf:
                self.assertEqual(zf.namelist(), ["textures/TEX_A"])
                self.assertEqual(unpack_blob_payload(zf.read("textures/TEX_A")[OTR_HEADER_SIZE:]), b"DATA")


if __name__ == "__main__":
    unittest.main()
