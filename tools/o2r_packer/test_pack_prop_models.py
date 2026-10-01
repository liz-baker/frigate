#!/usr/bin/env python3
"""Unit tests for pack_prop_models.py against a synthetic decomp tree."""

import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

from o2r_writer import OTR_HEADER_SIZE, O2rWriter
from pack_prop_models import RESOURCE_PATH, PropModelsError, build_prop_models, pack_prop_models

SEG5 = 0x05000000
BOUNDS = (-1.0, 2.0, -3.0, 4.0, -5.0, 6.0)

BONDCONSTANTS = """
typedef enum PROP
{
    PROP_SIBLING,  /* a comment */
    PROP_NESTED,
    PROP_NOBOX,
    PROP_MAX
} PROP;

typedef enum E_EXPLOSIONTYPE
{
    EXPLOSION_BREAK_OBJECT,
    EXPLOSION_BREAK_OBJECT2,
    EXPLOSION_MEDIUM,
    E_EXPLOSIONTYPE_COUNT
} E_EXPLOSIONTYPE;
"""

EXPLOSIONS = ["EXPLOSION_MEDIUM", "EXPLOSION_BREAK_OBJECT2", "EXPLOSION_BREAK_OBJECT"]


def addr(offset):
    return SEG5 + offset


def node(node_type, data=0, parent=0, next_=0, prev=0, child=0):
    return struct.pack(">HHIIIII", node_type, 0, data, parent, next_, prev, child)


def bbox_record():
    return struct.pack(">I6f", 0, *BOUNDS)


# The box is a sibling of the root's first child.
SIBLING = (
    node(0x01, child=addr(0x18))
    + node(0x02, parent=addr(0), next_=addr(0x30))
    + node(0x0A, data=addr(0x48), parent=addr(0), prev=addr(0x18))
    + bbox_record()
)
# The box is a child of the root's first child.
NESTED = (
    node(0x01, child=addr(0x18))
    + node(0x02, parent=addr(0), child=addr(0x30))
    + node(0x0A, data=addr(0x48), parent=addr(0x18))
    + bbox_record()
)
NOBOX = node(0x01, child=addr(0x18)) + node(0x02, parent=addr(0))

MODELS = [("sibling", "0.1", SIBLING), ("nested", "1.0", NESTED), ("nobox", "", NOBOX)]


def write_decomp(root: Path, models=MODELS) -> None:
    (root / "src").mkdir(parents=True)
    (root / "src" / "bondconstants.h").write_text(BONDCONSTANTS)
    prop_dir = root / "assets" / "obseg" / "prop"
    includes, headers = [], []
    for name, scale, data in models:
        (prop_dir / name).mkdir(parents=True, exist_ok=True)
        (prop_dir / name / "propFileRecord.inc.c").write_text(
            f"//NAME, SCALE\nPROPFILERECORD({name}, {scale})\n"
        )
        (prop_dir / f"P{name}Z.bin").write_bytes(data)
        includes.append(f"    #include <assets/obseg/prop/{name}/propFileRecord.inc.c>")
        headers.append(f"MODELFILEHEADER( {name}, 0, &SKELETON(standard_object), 0, 0, 1, 1.0, 0, 0)")
    (prop_dir / "propItemModelFileRecord.inc.c").write_text(
        "ItemModelFileRecord PitemZ_entries[] = {\n" + "\n".join(includes) + '\n    {0, "", 1.0}\n};\n'
    )
    (prop_dir / "propModelFileHeaders.inc.c").write_text("\n".join(headers) + "\n")
    records = "".join(
        f"    // [{i}] PROP\n    {{\n\t\t{kind}    ,\n\t\t{{0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000}}\n\t}},\n"
        for i, kind in enumerate(EXPLOSIONS[: len(models)])
    )
    terminator = "    {\n\t\tEXPLOSION_BREAK_OBJECT    ,\n\t\t{0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff}\n    }\n"
    (prop_dir / "propExplosionDetailsRecords.inc.c").write_text(
        "ExplosionDetailsRecord object_explosion_details[] = {\n" + records + terminator + "};\n"
    )


def lines(decomp: Path):
    return [line.split("\t") for line in build_prop_models(decomp).decode().splitlines()]


class BuildTest(unittest.TestCase):
    def test_one_line_per_prop_with_scale_and_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_decomp(Path(tmp))
            rows = lines(Path(tmp))
        self.assertEqual(len(rows), 3)
        self.assertEqual(rows[0][:5], ["model", "0", "sibling", "0.1", "-1.0"])
        self.assertEqual([float(v) for v in rows[0][4:10]], list(BOUNDS))
        self.assertEqual(rows[1][2:4], ["nested", "1.0"])
        self.assertEqual([float(v) for v in rows[1][4:10]], list(BOUNDS))
        self.assertEqual(rows[0][10], "standard_object")
        self.assertEqual([row[11] for row in rows], ["2", "1", "0"])

    def test_a_model_with_no_box_has_dashes_and_an_empty_scale_is_the_default(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_decomp(Path(tmp))
            row = lines(Path(tmp))[2]
        self.assertEqual(row, ["model", "2", "nobox", "0.1", "-", "-", "-", "-", "-", "-", "standard_object", "0"])

    def test_a_record_count_that_disagrees_with_the_enum_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_decomp(Path(tmp), MODELS[:-1])
            with self.assertRaises(PropModelsError):
                build_prop_models(tmp)

    def test_a_model_that_was_not_extracted_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_decomp(Path(tmp))
            (Path(tmp) / "assets" / "obseg" / "prop" / "PnestedZ.bin").unlink()
            with self.assertRaises(PropModelsError):
                build_prop_models(tmp)


class PackTest(unittest.TestCase):
    def test_writes_the_table_as_a_blob(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_decomp(Path(tmp) / "decomp")
            output = Path(tmp) / "out.o2r"
            with O2rWriter(output) as writer:
                self.assertEqual(pack_prop_models(Path(tmp) / "decomp", writer), RESOURCE_PATH)
            with zipfile.ZipFile(output) as archive:
                data = archive.read(RESOURCE_PATH)
        self.assertIn(b"model\t2\tnobox\t0.1\t-", data[OTR_HEADER_SIZE:])


if __name__ == "__main__":
    unittest.main()
