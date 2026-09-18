#!/usr/bin/env python3
"""Unit tests for pack_textures.py, against a synthetic images.def and
split texture files -- no GoldenEye ROM or real assets needed."""

import tempfile
import unittest
import zipfile
from pathlib import Path

from o2r_writer import O2rWriter, OTR_HEADER_SIZE, resource_type_code, unpack_blob_payload, unpack_resource_header
from pack_textures import ImageEntry, pack_textures, parse_images_def


SAMPLE_IMAGES_DEF = """\
// comment line, should be ignored
IMAGE(TEX_TITLE, 0x1000, some, other, fields)
IMAGE(TEX_HUD_AMMO, 0x200, 1, 2)
IMAGE(TEX_NOFILE, 0x40)
"""


class ParseImagesDefTest(unittest.TestCase):
    def test_parses_name_and_hex_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            def_path = Path(tmp) / "images.def"
            def_path.write_text(SAMPLE_IMAGES_DEF)

            entries = parse_images_def(def_path)

            self.assertEqual(
                entries,
                [
                    ImageEntry(name="TEX_TITLE", def_size=0x1000),
                    ImageEntry(name="TEX_HUD_AMMO", def_size=0x200),
                    ImageEntry(name="TEX_NOFILE", def_size=0x40),
                ],
            )

    def test_ignores_non_image_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            def_path = Path(tmp) / "images.def"
            def_path.write_text("// nothing here\n\n")
            self.assertEqual(parse_images_def(def_path), [])


class PackTexturesTest(unittest.TestCase):
    def _make_fixture(self, tmp: str):
        def_path = Path(tmp) / "images.def"
        def_path.write_text(SAMPLE_IMAGES_DEF)

        split_dir = Path(tmp) / "split"
        split_dir.mkdir()
        (split_dir / "TEX_TITLE.bin").write_bytes(b"\xAA" * 0x1000)
        (split_dir / "TEX_HUD_AMMO.bin").write_bytes(b"\xBB" * 0x200)
        # TEX_NOFILE.bin deliberately absent, to exercise the missing-file path.

        return def_path, split_dir

    def test_packs_present_images_and_reports_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            def_path, split_dir = self._make_fixture(tmp)
            archive_path = Path(tmp) / "textures.o2r"

            with O2rWriter(archive_path) as writer:
                missing = pack_textures(def_path, split_dir, writer)

            self.assertEqual(missing, ["TEX_NOFILE"])

            with zipfile.ZipFile(archive_path) as zf:
                self.assertEqual(
                    sorted(zf.namelist()), ["textures/TEX_HUD_AMMO", "textures/TEX_TITLE"]
                )

                title_raw = zf.read("textures/TEX_TITLE")
                header = unpack_resource_header(title_raw[:OTR_HEADER_SIZE])
                self.assertEqual(header.resource_type, resource_type_code("OBLB"))
                self.assertEqual(unpack_blob_payload(title_raw[OTR_HEADER_SIZE:]), b"\xAA" * 0x1000)

                ammo_raw = zf.read("textures/TEX_HUD_AMMO")
                self.assertEqual(unpack_blob_payload(ammo_raw[OTR_HEADER_SIZE:]), b"\xBB" * 0x200)

    def test_custom_virtual_prefix(self):
        with tempfile.TemporaryDirectory() as tmp:
            def_path, split_dir = self._make_fixture(tmp)
            archive_path = Path(tmp) / "textures.o2r"

            with O2rWriter(archive_path) as writer:
                pack_textures(def_path, split_dir, writer, virtual_prefix="assets/tex")

            with zipfile.ZipFile(archive_path) as zf:
                self.assertIn("assets/tex/TEX_TITLE", zf.namelist())


if __name__ == "__main__":
    unittest.main()
