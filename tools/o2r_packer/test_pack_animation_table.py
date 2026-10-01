#!/usr/bin/env python3
"""Unit tests for pack_animation_table.py against a synthetic initanitable.c."""

import tempfile
import unittest
import zipfile
from pathlib import Path

from o2r_writer import OTR_HEADER_SIZE, O2rWriter
import pack_animation_table as p

SOURCE = """
s32 animation_table_ptrs1[] = {
    PTR_ANIM_idle,
    PTR_ANIM_null50,
    PTR_ANIM_walking,
    0
};

struct ModelAnimation *animation_table_ptrs2[] = {
    PTR_ANIM_helicopter_cradle,
    0
};
"""


class PackAnimationTableTest(unittest.TestCase):
    def test_parses_both_tables_and_marks_placeholders(self):
        tables = p.parse_animation_tables(SOURCE)
        self.assertEqual(tables[1], ["idle", None, "walking"])
        self.assertEqual(tables[2], ["helicopter_cradle"])

    def test_text_has_a_line_per_id(self):
        text = p.build_animation_table(SOURCE).decode("ascii")
        self.assertEqual(text, "1\t0\tidle\n1\t1\t-\n1\t2\twalking\n2\t0\thelicopter_cradle\n")

    def test_rejects_a_name_without_a_clip(self):
        with self.assertRaises(p.AnimationTableError):
            p.build_animation_table(SOURCE, {"idle", "walking"})

    def test_rejects_a_source_without_the_tables(self):
        with self.assertRaises(p.AnimationTableError):
            p.parse_animation_tables("int x;")

    def test_writes_the_resource(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp = Path(tmp) / "decomp"
            (decomp / "src" / "game").mkdir(parents=True)
            (decomp / "src" / "game" / "initanitable.c").write_text(SOURCE)
            output = Path(tmp) / "out.o2r"
            with O2rWriter(output) as writer:
                self.assertEqual(p.pack_animation_table(decomp, writer), "animations/table")
            with zipfile.ZipFile(output) as archive:
                data = archive.read("animations/table")
            self.assertTrue(data[OTR_HEADER_SIZE:].endswith(p.build_animation_table(SOURCE)))


if __name__ == "__main__":
    unittest.main()
