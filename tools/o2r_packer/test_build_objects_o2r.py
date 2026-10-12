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


class ExtractAnimationTableTest(unittest.TestCase):
    """extract_animation_table() reads its bytes directly out of the ROM
    rather than routing through tools/extractor -- see its own docstring
    for why."""

    def test_slices_the_rom_at_the_filelist_offset_and_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp_dir = Path(tmp) / "external" / "007"
            (decomp_dir / "scripts").mkdir(parents=True)
            (decomp_dir / "scripts" / "filelist.u.csv").write_text(
                "7261072,9344,assets/obseg/chr/CarmourguardZ.bin,1,1\n"
                "5,7,assets/animationtable_entries.bin,0,0\n"
            )
            rom_path = Path(tmp) / "baserom.u.z64"
            rom_path.write_bytes(b"0123456789ABCDEFGHIJ")  # bytes[5:12] == b"56789AB"

            build_objects_o2r.extract_animation_table(decomp_dir, rom_path)

            out_path = decomp_dir / "assets" / "animationtable_entries.bin"
            self.assertEqual(out_path.read_bytes(), b"56789AB")

    def test_raises_when_row_missing_from_filelist(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp_dir = Path(tmp) / "external" / "007"
            (decomp_dir / "scripts").mkdir(parents=True)
            (decomp_dir / "scripts" / "filelist.u.csv").write_text(
                "7261072,9344,assets/obseg/chr/CarmourguardZ.bin,1,1\n"
            )
            rom_path = Path(tmp) / "baserom.u.z64"
            rom_path.write_bytes(b"")

            with self.assertRaises(ValueError):
                build_objects_o2r.extract_animation_table(decomp_dir, rom_path)

    def test_raises_if_the_row_is_ever_marked_compressed(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp_dir = Path(tmp) / "external" / "007"
            (decomp_dir / "scripts").mkdir(parents=True)
            (decomp_dir / "scripts" / "filelist.u.csv").write_text(
                "5,7,assets/animationtable_entries.bin,1,0\n"
            )
            rom_path = Path(tmp) / "baserom.u.z64"
            rom_path.write_bytes(b"0123456789ABCDEFGHIJ")

            with self.assertRaises(NotImplementedError):
                build_objects_o2r.extract_animation_table(decomp_dir, rom_path)


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

            with mock.patch("build_objects_o2r.run_extraction_mode") as mock_run, mock.patch(
                "build_objects_o2r.parse_images_def"
            ), mock.patch("build_objects_o2r.pack_model_resources", return_value=[]) as mock_models, mock.patch(
                "build_objects_o2r.pack_animation_resources"
            ) as mock_animations, mock.patch("build_objects_o2r.pack_animation_table") as mock_table, mock.patch(
                "build_objects_o2r.parse_animations", return_value={}
            ), mock.patch("build_objects_o2r.pack_chr_records") as mock_records, mock.patch(
                "build_objects_o2r.pack_weapon_stats"
            ) as mock_weapons, mock.patch("build_objects_o2r.pack_prop_models") as mock_props, mock.patch(
                "build_objects_o2r.pack_ramrom"
            ) as mock_ramrom, mock.patch("build_objects_o2r.pack_ai_lists") as mock_ai, mock.patch(
                "build_objects_o2r.pack_chr_combat"
            ) as mock_combat, mock.patch("build_objects_o2r.pack_monitor_lists") as mock_monitors, mock.patch(
                "build_objects_o2r.extract_animation_table"
            ) as mock_extract_anim:
                missing = build_objects_o2r.build_objects_o2r(decomp_dir, rom_path, output_path)

            self.assertEqual(missing, [])
            self.assertEqual([c.args[2] for c in mock_run.call_args_list], ["files", "images"])
            mock_extract_anim.assert_called_once_with(decomp_dir, rom_path)
            mock_models.assert_called_once()
            mock_animations.assert_called_once()
            mock_table.assert_called_once()
            mock_records.assert_called_once()
            mock_weapons.assert_called_once()
            mock_props.assert_called_once()
            mock_ramrom.assert_called_once()
            mock_ai.assert_called_once()
            mock_combat.assert_called_once()
            mock_monitors.assert_called_once()
            with zipfile.ZipFile(output_path) as zf:
                self.assertEqual(zf.namelist(), [])


if __name__ == "__main__":
    unittest.main()
