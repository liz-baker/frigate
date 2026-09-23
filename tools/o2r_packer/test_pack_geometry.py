#!/usr/bin/env python3
"""Unit tests for pack_geometry.py's filelist.u.csv parsing and archive
packing, using a synthetic filelist fixture (no real ROM/decomp
checkout needed)."""

import tempfile
import unittest
import zipfile
from pathlib import Path

from o2r_writer import OTR_HEADER_SIZE, O2rWriter, unpack_blob_payload
from pack_geometry import GeometryEntry, pack_geometry, parse_filelist_geometry

SAMPLE_FILELIST = """\
4425312,69104,assets/obseg/bg/bg_sev_all_p.bin,0,0
7188032,0,assets/obseg/bg/bg_imp_all_p.bin,0,1
8538160,6448,assets/obseg/stan/Tbg_ame_all_p_stanZ.bin,1,0
9335824,16,assets/obseg/text/LameE,1,0
9340560,1424,assets/obseg/text/u/LarecJ.bin,1,0
3040240,676,assets/font/fontBankGothic_kerning.bin,0,0
3040916,8716,assets/font/fontBankGothic_fontchartable.bin,0,0
7261072,9344,assets/obseg/chr/CarmourguardZ.bin,1,1
"""


class ParseFilelistGeometryTest(unittest.TestCase):
    def test_parses_entries_regardless_of_extract_flag(self):
        with tempfile.TemporaryDirectory() as tmp:
            filelist = Path(tmp) / "filelist.u.csv"
            filelist.write_text(SAMPLE_FILELIST)

            entries = parse_filelist_geometry(filelist)

            self.assertIn(
                GeometryEntry("bg", "bg_sev_all_p", "assets/obseg/bg/bg_sev_all_p.bin", 4425312, 69104, False),
                entries,
            )
            # extract=1 rows are included too (bg_imp_all_p), unlike pack_objects.py.
            self.assertIn(
                GeometryEntry("bg", "bg_imp_all_p", "assets/obseg/bg/bg_imp_all_p.bin", 7188032, 0, False),
                entries,
            )

    def test_strips_bin_extension_but_keeps_bare_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            filelist = Path(tmp) / "filelist.u.csv"
            filelist.write_text(SAMPLE_FILELIST)

            entries = parse_filelist_geometry(filelist)
            names = {e.virtual_name for e in entries if e.category == "text"}

            self.assertEqual(names, {"LameE", "LarecJ"})

    def test_ignores_categories_outside_bg_stan_text_font(self):
        with tempfile.TemporaryDirectory() as tmp:
            filelist = Path(tmp) / "filelist.u.csv"
            filelist.write_text(SAMPLE_FILELIST)

            entries = parse_filelist_geometry(filelist)

            self.assertTrue(all(e.category != "chr" for e in entries))

    def test_respects_requested_category_subset(self):
        with tempfile.TemporaryDirectory() as tmp:
            filelist = Path(tmp) / "filelist.u.csv"
            filelist.write_text(SAMPLE_FILELIST)

            entries = parse_filelist_geometry(filelist, categories=("font",))

            self.assertEqual(
                [e.virtual_name for e in entries],
                ["fontBankGothic_kerning", "fontBankGothic_fontchartable"],
            )


class PackGeometryTest(unittest.TestCase):
    def test_packs_extracted_files_and_reports_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            filelist = Path(tmp) / "filelist.u.csv"
            filelist.write_text(SAMPLE_FILELIST)

            decomp_dir = Path(tmp) / "external" / "007"
            bg_dir = decomp_dir / "assets" / "obseg" / "bg"
            bg_dir.mkdir(parents=True)
            (bg_dir / "bg_sev_all_p.bin").write_bytes(b"BGSEV")
            (bg_dir / "bg_imp_all_p.bin").write_bytes(b"")

            # Tbg_ame_all_p_stanZ.bin deliberately not created -- missing.

            text_dir = decomp_dir / "assets" / "obseg" / "text"
            (text_dir / "u").mkdir(parents=True)
            (text_dir / "LameE").write_bytes(b"TEXTAMEE")
            (text_dir / "u" / "LarecJ.bin").write_bytes(b"TEXTARECJ")

            font_dir = decomp_dir / "assets" / "font"
            font_dir.mkdir(parents=True)
            (font_dir / "fontBankGothic_kerning.bin").write_bytes(b"KERNING")
            (font_dir / "fontBankGothic_fontchartable.bin").write_bytes(b"CHARTABLE+PIXELS")

            output_path = Path(tmp) / "geometry.o2r"
            with O2rWriter(output_path) as writer:
                missing = pack_geometry(filelist, decomp_dir, writer)

            self.assertEqual(missing, ["assets/obseg/stan/Tbg_ame_all_p_stanZ.bin"])

            with zipfile.ZipFile(output_path) as zf:
                names = set(zf.namelist())
                self.assertIn("obseg/bg/bg_sev_all_p", names)
                self.assertIn("obseg/bg/bg_imp_all_p", names)
                self.assertIn("obseg/text/LameE", names)
                self.assertIn("obseg/text/LarecJ", names)
                # Font segments live under "font/<bank>", one entry per bank
                # (kerning + fontchartable concatenated), not "obseg/font/".
                self.assertIn("font/fontBankGothic", names)
                self.assertNotIn("font/fontBankGothic_kerning", names)
                self.assertNotIn("font/fontBankGothic_fontchartable", names)

                # bg stays OBLB (Blob-framed, length-prefixed payload).
                self.assertEqual(unpack_blob_payload(zf.read("obseg/bg/bg_sev_all_p")[OTR_HEADER_SIZE:]), b"BGSEV")

                # text/font are GETX/GEFN -- raw payload straight after the
                # header, no Blob length prefix, matching what
                # TextFactory/FontFactory read.
                self.assertEqual(zf.read("obseg/text/LarecJ")[OTR_HEADER_SIZE:], b"TEXTARECJ")
                self.assertEqual(
                    zf.read("font/fontBankGothic")[OTR_HEADER_SIZE:], b"KERNINGCHARTABLE+PIXELS"
                )


if __name__ == "__main__":
    unittest.main()
