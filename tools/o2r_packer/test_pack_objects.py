#!/usr/bin/env python3
"""Unit tests for pack_objects.py's filelist.u.csv parsing and archive
packing, using a synthetic filelist fixture (no real ROM/decomp
checkout needed)."""

import tempfile
import unittest
import zipfile
from pathlib import Path

from o2r_writer import OTR_HEADER_SIZE, O2rWriter, unpack_blob_payload
from pack_objects import ObjectEntry, pack_objects, parse_filelist_obseg

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


class PackObjectsTest(unittest.TestCase):
    def test_packs_extracted_files_and_reports_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            filelist = Path(tmp) / "filelist.u.csv"
            filelist.write_text(SAMPLE_FILELIST)

            decomp_dir = Path(tmp) / "external" / "007"
            chr_dir = decomp_dir / "assets" / "obseg" / "chr"
            chr_dir.mkdir(parents=True)
            (chr_dir / "CarmourguardZ.bin").write_bytes(b"ARMOURGUARD")
            # CbaronsamediZ.bin deliberately not created -- should be reported missing.

            gun_dir = decomp_dir / "assets" / "obseg" / "gun"
            gun_dir.mkdir(parents=True)
            (gun_dir / "Gak47Z.bin").write_bytes(b"AK47")

            prop_dir = decomp_dir / "assets" / "obseg" / "prop"
            prop_dir.mkdir(parents=True)
            (prop_dir / "PICBMZ.bin").write_bytes(b"ICBM")

            output_path = Path(tmp) / "objects.o2r"
            with O2rWriter(output_path) as writer:
                missing = pack_objects(filelist, decomp_dir, writer)

            self.assertEqual(missing, ["assets/obseg/chr/CbaronsamediZ.bin"])

            with zipfile.ZipFile(output_path) as zf:
                self.assertEqual(
                    sorted(zf.namelist()),
                    sorted(["obseg/chr/CarmourguardZ", "obseg/gun/Gak47Z", "obseg/prop/PICBMZ"]),
                )
                self.assertEqual(unpack_blob_payload(zf.read("obseg/chr/CarmourguardZ")[OTR_HEADER_SIZE:]), b"ARMOURGUARD")
                self.assertEqual(unpack_blob_payload(zf.read("obseg/gun/Gak47Z")[OTR_HEADER_SIZE:]), b"AK47")
                self.assertEqual(unpack_blob_payload(zf.read("obseg/prop/PICBMZ")[OTR_HEADER_SIZE:]), b"ICBM")


if __name__ == "__main__":
    unittest.main()
