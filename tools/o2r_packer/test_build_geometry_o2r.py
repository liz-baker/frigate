#!/usr/bin/env python3
"""Unit tests for build_geometry_o2r.py's orchestration logic: given no
real ROM or decomp toolchain, these stub out the extractor build/run
subprocess calls and check the driver wires the filtered CSV and
locations correctly."""

import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

import build_geometry_o2r
from o2r_writer import OTR_HEADER_SIZE, unpack_blob_payload

SAMPLE_FILELIST = "4425312,69104,assets/obseg/bg/bg_sev_all_p.bin,0,0\n"


class EnsureExtractorBuiltTest(unittest.TestCase):
    def test_builds_extractor_when_binary_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp_dir = Path(tmp) / "external" / "007"
            (decomp_dir / "tools" / "extractor").mkdir(parents=True)

            with mock.patch("subprocess.run") as mock_run:
                binary = build_geometry_o2r.ensure_extractor_built(decomp_dir)

            mock_run.assert_called_once()
            args, kwargs = mock_run.call_args
            self.assertEqual(args[0], ["make"])
            self.assertEqual(kwargs["cwd"], decomp_dir / "tools" / "extractor")
            self.assertEqual(binary, decomp_dir / "tools" / "extractor" / "extractor")

    def test_skips_build_when_binary_already_present(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp_dir = Path(tmp) / "external" / "007"
            extractor_dir = decomp_dir / "tools" / "extractor"
            extractor_dir.mkdir(parents=True)
            (extractor_dir / "extractor").write_bytes(b"")

            with mock.patch("subprocess.run") as mock_run:
                build_geometry_o2r.ensure_extractor_built(decomp_dir)

            mock_run.assert_not_called()

    def test_raises_clear_error_when_decomp_not_checked_out(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp_dir = Path(tmp) / "external" / "007"  # deliberately not created

            with self.assertRaises(FileNotFoundError):
                build_geometry_o2r.ensure_extractor_built(decomp_dir)


class ExtractGeometryTest(unittest.TestCase):
    def test_invokes_extractor_with_filtered_csv_forcing_extract(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp_dir = Path(tmp) / "external" / "007"
            extractor_dir = decomp_dir / "tools" / "extractor"
            extractor_dir.mkdir(parents=True)
            (extractor_dir / "extractor").write_bytes(b"")

            filelist = Path(tmp) / "filelist.u.csv"
            filelist.write_text(SAMPLE_FILELIST)

            rom_path = Path(tmp) / "baserom.u.z64"
            rom_path.write_bytes(b"fake rom")

            written_csvs = []
            real_run = build_geometry_o2r.subprocess.run

            def capture_run(command, **kwargs):
                if command[0] != "make":
                    csv_path = Path(command[2])
                    written_csvs.append(csv_path.read_text())
                return mock.DEFAULT

            with mock.patch("subprocess.run", side_effect=capture_run) as mock_run:
                build_geometry_o2r.extract_geometry(decomp_dir, rom_path, filelist)

            mock_run.assert_called_once()
            args, kwargs = mock_run.call_args
            command = args[0]
            self.assertEqual(command[0], str(extractor_dir / "extractor"))
            self.assertEqual(command[1], str(rom_path.resolve()))
            self.assertEqual(kwargs["cwd"], decomp_dir)

            # The row's extract column must be forced to 1 even though
            # SAMPLE_FILELIST itself says 0.
            self.assertEqual(written_csvs, ["4425312,69104,assets/obseg/bg/bg_sev_all_p.bin,0,1\n"])
            # The temp CSV is cleaned up afterwards.
            self.assertEqual(list(decomp_dir.glob("*.csv")), [])


class BuildGeometryO2rTest(unittest.TestCase):
    def test_packs_extracted_output_into_archive(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp_dir = Path(tmp) / "external" / "007"
            (decomp_dir / "scripts").mkdir(parents=True)
            (decomp_dir / "scripts" / "filelist.u.csv").write_text(SAMPLE_FILELIST)
            extractor_dir = decomp_dir / "tools" / "extractor"
            extractor_dir.mkdir(parents=True)
            (extractor_dir / "extractor").write_bytes(b"")

            # Simulate what a real extractor invocation would have
            # produced, since we're stubbing subprocess.run.
            bg_dir = decomp_dir / "assets" / "obseg" / "bg"
            bg_dir.mkdir(parents=True)
            (bg_dir / "bg_sev_all_p.bin").write_bytes(b"BGSEV")

            rom_path = Path(tmp) / "baserom.u.z64"
            rom_path.write_bytes(b"fake rom")
            output_path = Path(tmp) / "out.o2r"

            with mock.patch("subprocess.run"), mock.patch("build_geometry_o2r.run_extraction_mode") as mock_run, mock.patch(
                "build_geometry_o2r.parse_images_def"
            ), mock.patch("build_geometry_o2r.pack_bg_resources", return_value=[]) as mock_bg, mock.patch(
                "build_geometry_o2r.read_segment", return_value=b"SEGMENT"
            ) as mock_segment, mock.patch("build_geometry_o2r.pack_rarewarelogo") as mock_rare, mock.patch(
                "build_geometry_o2r.read_sight", return_value=b"SIGHT"
            ) as mock_sight, mock.patch(
                "build_geometry_o2r.read_blood_stream", return_value=b"BLOOD"
            ) as mock_blood, mock.patch("build_geometry_o2r.pack_eyeintro") as mock_eye, mock.patch(
                "build_geometry_o2r.pack_hud_images"
            ) as mock_hud, mock.patch("build_geometry_o2r.pack_sky_images") as mock_sky, mock.patch(
                "build_geometry_o2r.pack_texture_hits"
            ) as mock_hits:
                missing = build_geometry_o2r.build_geometry_o2r(decomp_dir, rom_path, output_path)

            self.assertEqual(missing, [])
            mock_run.assert_called_once_with(decomp_dir, rom_path, "images")
            self.assertEqual(mock_bg.call_args.args[0], ["assets/obseg/bg/bg_sev_all_p.bin"])
            mock_segment.assert_called_once_with(b"fake rom")
            self.assertEqual(mock_rare.call_args.args[0], b"SEGMENT")
            mock_sight.assert_called_once_with(b"fake rom")
            mock_blood.assert_called_once_with(b"fake rom")
            self.assertEqual(mock_eye.call_args.args[:2], (b"SIGHT", b"BLOOD"))
            self.assertEqual(mock_hud.call_args.args[0], b"fake rom")
            self.assertEqual(mock_hits.call_args.args[0], decomp_dir)
            self.assertEqual(mock_sky.call_args.args[0], b"fake rom")
            with zipfile.ZipFile(output_path) as zf:
                self.assertEqual(zf.namelist(), ["obseg/bg/bg_sev_all_p"])
                self.assertEqual(unpack_blob_payload(zf.read("obseg/bg/bg_sev_all_p")[OTR_HEADER_SIZE:]), b"BGSEV")


if __name__ == "__main__":
    unittest.main()
