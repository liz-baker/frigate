#!/usr/bin/env python3
"""Unit tests for pack_ramrom.py against a synthetic decomp tree."""

import tempfile
import unittest
import zipfile
from pathlib import Path

from o2r_writer import OTR_HEADER_SIZE, O2rWriter
from pack_ramrom import TABLE_PATH, RamromError, pack_ramrom, parse_table

SOURCE = """
extern u32* ramrom_Dam_1;
struct ramrom_struct ramrom_table[] = {
    {&ramrom_Dam_1, 0},
    {&ramrom_Train, 3},
    {0,0}
};
"""


def write_decomp(root: Path, files=("Dam_1", "Train")) -> None:
    (root / "src" / "game").mkdir(parents=True)
    (root / "src" / "game" / "ramromreplay.c").write_text(SOURCE)
    (root / "assets" / "ramrom").mkdir(parents=True)
    for name in files:
        (root / "assets" / "ramrom" / f"ramrom_{name}.bin").write_bytes(name.encode())


class ParseTableTest(unittest.TestCase):
    def test_entries_in_table_order_without_the_terminator(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_decomp(Path(tmp))
            self.assertEqual(parse_table(tmp), [("Dam_1", 0), ("Train", 3)])

    def test_a_missing_table_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_decomp(Path(tmp))
            (Path(tmp) / "src" / "game" / "ramromreplay.c").write_text("int x;")
            with self.assertRaises(RamromError):
                parse_table(tmp)


class PackTest(unittest.TestCase):
    def test_writes_the_table_and_each_demo_unchanged(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_decomp(Path(tmp) / "decomp")
            output = Path(tmp) / "out.o2r"
            with O2rWriter(output) as writer:
                written = pack_ramrom(Path(tmp) / "decomp", writer)
            with zipfile.ZipFile(output) as archive:
                table = archive.read(TABLE_PATH)[OTR_HEADER_SIZE:]
                demo = archive.read("ramrom/Train")[OTR_HEADER_SIZE:]
        self.assertEqual(written, [TABLE_PATH, "ramrom/Dam_1", "ramrom/Train"])
        self.assertIn(b"Dam_1\t0\nTrain\t3\n", table)
        self.assertIn(b"Train", demo)

    def test_a_table_entry_without_a_file_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_decomp(Path(tmp) / "decomp", files=("Dam_1",))
            with O2rWriter(Path(tmp) / "out.o2r") as writer:
                with self.assertRaises(RamromError):
                    pack_ramrom(Path(tmp) / "decomp", writer)


if __name__ == "__main__":
    unittest.main()
