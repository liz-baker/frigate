#!/usr/bin/env python3
"""Unit tests for verify_roundtrip.py, against synthetic fixtures."""

import tempfile
import unittest
from pathlib import Path

from verify_roundtrip import verify_textures_roundtrip

SAMPLE_IMAGES_DEF = """\
IMAGE(TEX_A, 0x4)
IMAGE(TEX_B, 0x4)
"""


class VerifyTexturesRoundtripTest(unittest.TestCase):
    def test_clean_roundtrip_reports_zero_mismatches(self):
        with tempfile.TemporaryDirectory() as tmp:
            def_path = Path(tmp) / "images.def"
            def_path.write_text(SAMPLE_IMAGES_DEF)

            split_dir = Path(tmp) / "split"
            split_dir.mkdir()
            (split_dir / "TEX_A.bin").write_bytes(b"AAAA")
            (split_dir / "TEX_B.bin").write_bytes(b"BBBB")

            mismatches = verify_textures_roundtrip(str(def_path), str(split_dir))
            self.assertEqual(mismatches, 0)

    def test_skips_missing_files_without_false_mismatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            def_path = Path(tmp) / "images.def"
            def_path.write_text(SAMPLE_IMAGES_DEF)

            split_dir = Path(tmp) / "split"
            split_dir.mkdir()
            (split_dir / "TEX_A.bin").write_bytes(b"AAAA")
            # TEX_B.bin deliberately absent.

            mismatches = verify_textures_roundtrip(str(def_path), str(split_dir))
            self.assertEqual(mismatches, 0)

    def test_detects_a_genuine_mismatch(self):
        # Force a mismatch by having pack_textures write different bytes
        # than what's on disk at comparison time: patch it to flip one
        # byte of the payload it packs, independent of the real source
        # file verify_textures_roundtrip reads back for comparison.
        import verify_roundtrip as verify_roundtrip_module

        real_pack_textures = verify_roundtrip_module.pack_textures

        def corrupting_pack_textures(images_def_path, split_dir, writer, **kwargs):
            entries = verify_roundtrip_module.parse_images_def(images_def_path)
            missing = []
            for entry in entries:
                bin_path = Path(split_dir) / f"{entry.name}.bin"
                if not bin_path.exists():
                    missing.append(entry.name)
                    continue
                payload = bytearray(bin_path.read_bytes())
                payload[0] ^= 0xFF  # corrupt the first byte
                writer.add_resource(f"textures/{entry.name}", "OBLB", 1, 0, bytes(payload))
            return missing

        verify_roundtrip_module.pack_textures = corrupting_pack_textures
        try:
            with tempfile.TemporaryDirectory() as tmp:
                def_path = Path(tmp) / "images.def"
                def_path.write_text("IMAGE(TEX_A, 0x4)\n")

                split_dir = Path(tmp) / "split"
                split_dir.mkdir()
                (split_dir / "TEX_A.bin").write_bytes(b"AAAA")

                mismatches = verify_textures_roundtrip(str(def_path), str(split_dir))
                self.assertEqual(mismatches, 1)
        finally:
            verify_roundtrip_module.pack_textures = real_pack_textures


if __name__ == "__main__":
    unittest.main()
