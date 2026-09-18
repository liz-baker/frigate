#!/usr/bin/env python3
"""Unit tests for pack_model_resources.py against synthetic files (no ROM
or decomp checkout needed); test_rom_model_archives.py covers real data."""

import tempfile
import unittest
import zipfile
from pathlib import Path

from o2r_writer import O2rWriter
from pack_model_resources import pack_bg_resources, pack_model_resources

FILELIST = "0,0,assets/obseg/chr/CmissingZ.bin,1,1\n0,0,assets/obseg/gun/GnoneZ.bin,1,0\n"


class MissingFilesTest(unittest.TestCase):
    def test_missing_model_and_bg_files_are_reported_not_written(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            filelist = tmp / "filelist.u.csv"
            filelist.write_text(FILELIST)
            archive = tmp / "out.o2r"
            with O2rWriter(archive) as writer:
                missing = pack_model_resources(filelist, tmp, writer, lambda _: None)
                missing += pack_bg_resources(["assets/obseg/bg/bg_x_all_p.bin"], tmp, writer, lambda _: None)

            self.assertEqual(missing, ["assets/obseg/chr/CmissingZ.bin", "assets/obseg/bg/bg_x_all_p.bin"])
            self.assertEqual(zipfile.ZipFile(archive).namelist(), [])


if __name__ == "__main__":
    unittest.main()
