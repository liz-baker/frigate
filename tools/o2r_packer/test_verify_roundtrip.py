#!/usr/bin/env python3
"""Unit tests for verify_roundtrip.py, against synthetic fixtures. See
verify_roundtrip.py's module docstring for why texture verification
checks declared image size against width/height/texture_type rather
than a byte-for-byte round trip (pack_textures.py decodes images now,
it doesn't copy them opaquely)."""

import tempfile
import unittest
from pathlib import Path

from ge_texture import TexFormat
from test_ge_texture import _texture_header
from verify_roundtrip import _expected_image_data_size, verify_textures_roundtrip

SAMPLE_IMAGES_DEF = """\
IMAGE(TEX_A, 0x4)
IMAGE(TEX_B, 0x4)
"""


def _uncompressed_i8(width: int, height: int, values) -> bytes:
    w = _texture_header(False)
    w.write(TexFormat.I8.value, 4)
    w.write(width, 8)
    w.write(height, 8)
    w.write(0, 4)  # UNCOMPRESSED0
    for v in values:
        w.write(v, 8)
    return w.bytes()


class ExpectedImageDataSizeTest(unittest.TestCase):
    def test_full_byte_formats(self):
        self.assertEqual(_expected_image_data_size(1, 4, 3), 4 * 3 * 4)  # RGBA32bpp
        self.assertEqual(_expected_image_data_size(6, 5, 2), 5 * 2)  # Grayscale8bpp

    def test_sub_byte_formats_pack_per_row(self):
        # Palette4bpp, odd width: each row rounds up to a whole byte.
        self.assertEqual(_expected_image_data_size(3, 3, 2), 2 * 2)


class VerifyTexturesRoundtripTest(unittest.TestCase):
    def test_clean_decode_reports_zero_failures(self):
        with tempfile.TemporaryDirectory() as tmp:
            def_path = Path(tmp) / "images.def"
            def_path.write_text(SAMPLE_IMAGES_DEF)

            split_dir = Path(tmp) / "split"
            split_dir.mkdir()
            (split_dir / "TEX_A.bin").write_bytes(_uncompressed_i8(2, 1, [1, 2]))
            (split_dir / "TEX_B.bin").write_bytes(_uncompressed_i8(1, 2, [3, 4]))

            failures = verify_textures_roundtrip(str(def_path), str(split_dir))
            self.assertEqual(failures, 0)

    def test_skips_missing_files_without_false_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            def_path = Path(tmp) / "images.def"
            def_path.write_text(SAMPLE_IMAGES_DEF)

            split_dir = Path(tmp) / "split"
            split_dir.mkdir()
            (split_dir / "TEX_A.bin").write_bytes(_uncompressed_i8(2, 1, [1, 2]))
            # TEX_B.bin deliberately absent.

            failures = verify_textures_roundtrip(str(def_path), str(split_dir))
            self.assertEqual(failures, 0)

    def test_detects_a_declared_size_mismatch(self):
        # Force a mismatch independent of a real decode bug: patch
        # pack_textures to under-report ImageDataSize for one resource.
        import struct

        import verify_roundtrip as verify_roundtrip_module

        real_pack_textures = verify_roundtrip_module.pack_textures

        # Write a
        # texture resource by hand whose declared size doesn't match
        # its width/height, bypassing decode entirely.
        def fake_pack_textures(images_def_path, split_dir, writer, **kwargs):
            entries = verify_roundtrip_module.parse_images_def(images_def_path)
            payload = struct.pack(">IIIIffI", 1, 4, 4, 0, 1.0, 1.0, 4) + b"\x00" * 4  # claims 4 bytes, needs 64
            for entry in entries:
                writer.add_resource(f"textures/{entry.name}", "OTEX", version=1, resource_id=0, payload=payload)
            return []

        verify_roundtrip_module.pack_textures = fake_pack_textures
        try:
            with tempfile.TemporaryDirectory() as tmp:
                def_path = Path(tmp) / "images.def"
                def_path.write_text("IMAGE(TEX_A, 0x4)\n")

                split_dir = Path(tmp) / "split"
                split_dir.mkdir()
                (split_dir / "TEX_A.bin").write_bytes(_uncompressed_i8(1, 1, [1]))

                failures = verify_textures_roundtrip(str(def_path), str(split_dir))
                self.assertEqual(failures, 1)
        finally:
            verify_roundtrip_module.pack_textures = real_pack_textures


if __name__ == "__main__":
    unittest.main()
