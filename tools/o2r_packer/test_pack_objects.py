#!/usr/bin/env python3
"""Unit tests for pack_objects.py's filelist.u.csv parsing, using a synthetic filelist fixture (no real ROM/decomp
checkout needed)."""

import tempfile
import unittest
from pathlib import Path

from pack_objects import ObjectEntry, parse_filelist_obseg

SAMPLE_FILELIST = """\
7261072,9344,assets/obseg/chr/CarmourguardZ.bin,1,1
7270416,14064,assets/obseg/chr/CbaronsamediZ.bin,1,1
7782256,2576,assets/obseg/gun/Gak47Z.bin,1,1
8039632,9600,assets/obseg/prop/PICBMZ.bin,1,1
4425312,69104,assets/obseg/bg/bg_sev_all_p.bin,0,0
1198784,1482432,assets/animationtable_entries.bin,0,0
"""


class ParseFilelistObsegTest(unittest.TestCase):
    def test_parses_extractable_entries_for_requested_categories(self):
        with tempfile.TemporaryDirectory() as tmp:
            filelist = Path(tmp) / "filelist.u.csv"
            filelist.write_text(SAMPLE_FILELIST)

            entries = parse_filelist_obseg(filelist)

            self.assertEqual(
                entries,
                [
                    ObjectEntry("chr", "CarmourguardZ", "assets/obseg/chr/CarmourguardZ.bin"),
                    ObjectEntry("chr", "CbaronsamediZ", "assets/obseg/chr/CbaronsamediZ.bin"),
                    ObjectEntry("gun", "Gak47Z", "assets/obseg/gun/Gak47Z.bin"),
                    ObjectEntry("prop", "PICBMZ", "assets/obseg/prop/PICBMZ.bin"),
                ],
            )

    def test_skips_entries_with_extract_flag_unset(self):
        with tempfile.TemporaryDirectory() as tmp:
            filelist = Path(tmp) / "filelist.u.csv"
            filelist.write_text(SAMPLE_FILELIST)

            entries = parse_filelist_obseg(filelist)

            self.assertTrue(all(e.category != "bg" for e in entries))

    def test_respects_requested_category_subset(self):
        with tempfile.TemporaryDirectory() as tmp:
            filelist = Path(tmp) / "filelist.u.csv"
            filelist.write_text(SAMPLE_FILELIST)

            entries = parse_filelist_obseg(filelist, categories=("gun",))

            self.assertEqual([e.name for e in entries], ["Gak47Z"])


if __name__ == "__main__":
    unittest.main()
