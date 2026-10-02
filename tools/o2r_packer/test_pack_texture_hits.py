#!/usr/bin/env python3
"""Unit tests for pack_texture_hits.py against a synthetic images.def."""

import tempfile
import unittest
import zipfile
from pathlib import Path

from o2r_writer import OTR_HEADER_SIZE, O2rWriter
from pack_texture_hits import (
    RESOURCE_PATH,
    ImageHit,
    TextureHitsError,
    build_texture_hits,
    pack_texture_hits,
    parse_hit_types,
    parse_image_hits,
)

CONSTANTS = """
typedef enum HIT_TYPE
{
    HIT_DEFAULT,
    HIT_STONE,
    HIT_GLASS,
    HIT_TYPE_COUNT
}HIT_TYPE;
"""

IMAGES = """IMAGE(COPYICON, 0x754, HIT_DEFAULT, HIT_DEFAULT, 0, 0, 0, 0)
IMAGE(WALL, 0x10, HIT_STONE, HIT_GLASS, 0, 0, 0, 0)
"""


class ParseTest(unittest.TestCase):
    def test_hit_types_take_their_enum_order(self):
        self.assertEqual(parse_hit_types(CONSTANTS), {"HIT_DEFAULT": 0, "HIT_STONE": 1, "HIT_GLASS": 2, "HIT_TYPE_COUNT": 3})

    def test_a_missing_enum_is_an_error(self):
        with self.assertRaises(TextureHitsError):
            parse_hit_types("")

    def test_images_keep_file_order_and_both_types(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "images.def"
            path.write_text(IMAGES)
            hits = parse_image_hits(path, parse_hit_types(CONSTANTS))
        self.assertEqual(hits, [ImageHit("COPYICON", 0, 0), ImageHit("WALL", 1, 2)])

    def test_an_unknown_type_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "images.def"
            path.write_text("IMAGE(X, 1, HIT_NOPE, HIT_STONE, 0, 0, 0, 0)\n")
            with self.assertRaises(TextureHitsError):
                parse_image_hits(path, parse_hit_types(CONSTANTS))


class PackTest(unittest.TestCase):
    def test_the_table_has_one_line_per_image(self):
        self.assertEqual(build_texture_hits([ImageHit("A", 1, 2), ImageHit("B", 0, 3)]), b"A 1 2\nB 0 3\n")

    def test_pack_writes_the_resource(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp = Path(tmp) / "decomp"
            (decomp / "src").mkdir(parents=True)
            (decomp / "assets").mkdir()
            (decomp / "src" / "bondconstants.h").write_text(CONSTANTS)
            (decomp / "assets" / "images.def").write_text(IMAGES)
            out = Path(tmp) / "out.o2r"
            with O2rWriter(out) as writer:
                self.assertEqual(pack_texture_hits(decomp, writer), RESOURCE_PATH)
            with zipfile.ZipFile(out) as z:
                self.assertEqual(z.namelist(), [RESOURCE_PATH])
                self.assertTrue(z.read(RESOURCE_PATH)[OTR_HEADER_SIZE:].endswith(b"WALL 1 2\n"))


if __name__ == "__main__":
    unittest.main()
