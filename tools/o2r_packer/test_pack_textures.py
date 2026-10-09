#!/usr/bin/env python3
"""Unit tests for pack_textures.py, against a synthetic images.def and
hand-built (but valid) GoldenEye texture bitstreams -- no GoldenEye ROM
or real assets needed. Bitstream construction reuses test_ge_texture.py's
`_BitWriter`/`_texture_header` helpers rather than duplicating them."""

import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

from ge_texture import TexFormat
from o2r_writer import O2rWriter, OTR_HEADER_SIZE, resource_type_code, unpack_resource_header
from pack_textures import ImageEntry, pack_textures, parse_images_def
from test_ge_texture import _BitWriter, _texture_header


def _uncompressed_rgba32(width: int, height: int, pixels) -> bytes:
    w = _texture_header(False)
    w.write(TexFormat.RGBA32.value, 4)
    w.write(width, 8)
    w.write(height, 8)
    w.write(0, 4)  # UNCOMPRESSED0
    for r, g, b, a in pixels:
        w.write(r, 8)
        w.write(g, 8)
        w.write(b, 8)
        w.write(a, 8)
    return w.bytes()


def _paletted_ci8(width: int, height: int, palette, indices: bytes, lod: int = 0) -> bytes:
    w = _texture_header(True, lod=lod)
    w.write(TexFormat.RGBA16_CI8.value, 8)
    w.write(len(palette) - 1, 8)
    for c in palette:
        w.write(c, 16)
    w.write(width, 8)
    w.write(height, 8)
    header = w.bytes()

    import zlib

    co = zlib.compressobj(9, zlib.DEFLATED, -15)
    deflated = b"\x11\x72" + co.compress(indices) + co.flush()
    return header + deflated


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

    def test_packs_the_detail_tile_fields(self):
        with tempfile.TemporaryDirectory() as tmp:
            def_path = Path(tmp) / "images.def"
            def_path.write_text("IMAGE(TEX_GRAVEL, 0x135, HIT_DEFAULT, HIT_DIRT, 0x3, 0x8, 0xD, 0x2)\n")

            self.assertEqual(
                parse_images_def(def_path), [ImageEntry(name="TEX_GRAVEL", def_size=0x135, detail=0x38D2)]
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
        (split_dir / "TEX_TITLE.bin").write_bytes(
            _uncompressed_rgba32(2, 1, [(0x11, 0x22, 0x33, 0xFF), (0x44, 0x55, 0x66, 0x00)])
        )
        (split_dir / "TEX_HUD_AMMO.bin").write_bytes(
            _paletted_ci8(2, 1, [0b1111100000000001, 0b0000000000011111], bytes([0, 1]))
        )
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
                    sorted(zf.namelist()),
                    ["textures/TEX_HUD_AMMO", "textures/TEX_HUD_AMMO.pal", "textures/TEX_TITLE"],
                )

                title_raw = zf.read("textures/TEX_TITLE")
                header = unpack_resource_header(title_raw[:OTR_HEADER_SIZE])
                self.assertEqual(header.resource_type, resource_type_code("OTEX"))

                body = title_raw[OTR_HEADER_SIZE:]
                texture_type, width, height, flags = struct.unpack(">IIII", body[:16])
                data_size = struct.unpack(">I", body[24:28])[0]
                pixel_data = body[28 : 28 + data_size]

                self.assertEqual(texture_type, 1)  # RGBA32bpp
                self.assertEqual((width, height), (2, 1))
                self.assertEqual(flags, 0)
                self.assertEqual(
                    pixel_data,
                    bytes([0x11, 0x22, 0x33, 0xFF, 0x44, 0x55, 0x66, 0x00]),
                )

                ammo_raw = zf.read("textures/TEX_HUD_AMMO")
                ammo_body = ammo_raw[OTR_HEADER_SIZE:]
                ammo_type, ammo_w, ammo_h = struct.unpack(">III", ammo_body[:12])
                self.assertEqual(ammo_type, 4)  # Palette8bpp
                self.assertEqual((ammo_w, ammo_h), (2, 1))

                pal_raw = zf.read("textures/TEX_HUD_AMMO.pal")
                pal_body = pal_raw[OTR_HEADER_SIZE:]
                pal_type, pal_w, pal_h = struct.unpack(">III", pal_body[:12])
                self.assertEqual(pal_type, 2)  # RGBA16bpp
                self.assertEqual(pal_body[28:36], bytes([0xF8, 0x01, 0x00, 0x1F, 0, 0, 0, 0]))
                self.assertEqual((pal_w, pal_h), (256, 1))  # CI8 palettes are padded to 256

    def test_mipmapped_image_gets_a_mip_chain_companion(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            (tmp_path / "images.def").write_text("IMAGE(TEX_MIP, 0x40)\nIMAGE(TEX_FLAT, 0x40)\n")
            split = tmp_path / "split"
            split.mkdir()
            palette = [0b1111100000000001, 0b0000000000011111]
            (split / "TEX_MIP.bin").write_bytes(_paletted_ci8(4, 4, palette, bytes(16), lod=3))
            (split / "TEX_FLAT.bin").write_bytes(_paletted_ci8(4, 4, palette, bytes(16)))
            archive_path = tmp_path / "out.o2r"
            with O2rWriter(archive_path) as writer:
                pack_textures(tmp_path / "images.def", split, writer)

            with zipfile.ZipFile(archive_path) as zf:
                names = zf.namelist()
                self.assertIn("textures/TEX_MIP.mip", names)
                self.assertNotIn("textures/TEX_FLAT.mip", names)
                body = zf.read("textures/TEX_MIP.mip")[OTR_HEADER_SIZE:]
                tex_type, width, height = struct.unpack(">III", body[:12])
                self.assertEqual((tex_type, width, height), (4, 4, 4))  # Palette8bpp, base size
                # Rows padded to 8 bytes: 4x4 (32), 2x2 (16), 1x1 (8).
                (data_size,) = struct.unpack(">I", body[24:28])
                self.assertEqual(data_size, 32 + 16 + 8)

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
