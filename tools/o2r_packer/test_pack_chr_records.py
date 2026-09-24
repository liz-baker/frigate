#!/usr/bin/env python3
"""Unit tests for pack_chr_records.py against a synthetic decomp tree."""

import tempfile
import unittest
import zipfile
from pathlib import Path

from o2r_writer import OTR_HEADER_SIZE, O2rWriter
from pack_chr_records import RESOURCE_PATH, ChrRecordsError, build_records, pack_chr_records

BONDCONSTANTS = """
typedef enum BODIES
{
    BODY_First,
    BODY_Second, /* comment */
#ifdef ALL_BONDS
    BODY_Skipped,
#endif
    BODY_Last,
    HEAD_START,
    BODY_Head_A = HEAD_START,
    BODY_Head_B,
    BODIES_MAX
} BODIES;

typedef enum HEADS
{
    HEAD_Head_A = HEAD_START,
    HEAD_Head_B,
    HEAD_END,
    HEAD_COUNT = HEAD_END - HEAD_START,
    HEAD_FIXED = 0xFFFFFFFF
} HEADS;
"""

RECORDS = [
    ("first", "CfirstZ", "{&first_header, \"CfirstZ\", 1.0, 1.0, 0x1, 0x00, 0x00, 0x00},"),
    ("second", "CsecondZ", "{&second_header, \"CsecondZ\", 1.0, 0.96609998, 0x0, 0x1, 0x00, 0x00},"),
    ("last", "ClastZ", "{&last_header, \"ClastZ\", 1.0, 1.0, 1, 0, 0, 0},"),
    ("head_a", "CheadaZ", "{&heada_header, \"CheadaZ\", 1.0, 1.0, 0x1, 0x1, 0x00, 0x00},"),
    ("head_b", "CheadbZ", "{&headb_header, \"CheadbZ\", 1.0, 1.0, 0x0, 0x1, 0x00, 0x00},"),
]


def write_decomp(root: Path, records=RECORDS) -> None:
    (root / "src").mkdir(parents=True)
    (root / "src" / "bondconstants.h").write_text(BONDCONSTANTS)
    chr_dir = root / "assets" / "obseg" / "chr"
    includes = []
    for directory, _, line in records:
        (chr_dir / directory).mkdir(parents=True)
        (chr_dir / directory / "chrModelFileRecord.inc.c").write_text(line + "\n")
        includes.append(f"    #include <assets/obseg/chr/{directory}/chrModelFileRecord.inc.c>")
    (chr_dir / "chrModelFileRecords.inc.c").write_text(
        "struct ChrModelFileRecord c_item_entries[] = {\n" + "\n".join(includes) + "\n};\n"
    )


class BuildRecordsTest(unittest.TestCase):
    def test_one_line_per_slot_with_every_identifier_naming_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_decomp(Path(tmp))
            lines = build_records(tmp).decode().splitlines()
        self.assertEqual(
            lines,
            [
                "0\tBODY_First\tCfirstZ\t1\t0",
                "1\tBODY_Second\tCsecondZ\t0\t1",
                "2\tBODY_Last\tClastZ\t1\t0",
                "3\tBODY_Head_A,HEAD_Head_A\tCheadaZ\t1\t1",
                "4\tBODY_Head_B,HEAD_Head_B\tCheadbZ\t0\t1",
            ],
        )

    def test_a_record_count_that_disagrees_with_the_enums_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_decomp(Path(tmp), RECORDS[:-1])
            with self.assertRaises(ChrRecordsError):
                build_records(tmp)


class PackTest(unittest.TestCase):
    def test_writes_the_records_as_a_blob(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_decomp(Path(tmp) / "decomp")
            output = Path(tmp) / "out.o2r"
            with O2rWriter(output) as writer:
                self.assertEqual(pack_chr_records(Path(tmp) / "decomp", writer), RESOURCE_PATH)
            with zipfile.ZipFile(output) as archive:
                data = archive.read(RESOURCE_PATH)
        self.assertIn(b"0\tBODY_First\tCfirstZ\t1\t0\n", data[OTR_HEADER_SIZE:])


if __name__ == "__main__":
    unittest.main()
