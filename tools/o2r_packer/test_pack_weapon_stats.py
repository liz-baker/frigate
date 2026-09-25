#!/usr/bin/env python3
"""Unit tests for pack_weapon_stats.py against a synthetic decomp tree."""

import tempfile
import unittest
import zipfile
from pathlib import Path

from o2r_writer import OTR_HEADER_SIZE, O2rWriter
from pack_weapon_stats import RESOURCE_PATH, WeaponStatsError, build_weapon_stats, pack_weapon_stats

BONDCONSTANTS = """
#define WEAPONSTATBITFLAG_CLICKY 0x10
#define WEAPONSTATBITFLAG_ONLY_1_HANDED 0x100
#define WEAPONSTATBITFLAG_DISABLE_CROUCH 0x8000
typedef enum AMMOTYPE
{
    AMMO_NONE,
    AMMO_9MM,
    AMMOTYPE_MAX
} AMMOTYPE;
typedef enum ITEM_IDS
{
    ITEM_NOTHING = -1,

    ITEM_UNARMED,
    ITEM_PISTOL,
    ITEM_SCOPE,
    ITEM_IDS_MAX
} ITEM_IDS;
"""

GUN_C = """
AmmoStats ammo_related[AMMO_RELATED_MAX] = {
    { 0x0    , 0x00000000,   0.0f, },
    { 0x320  , 0x02000C84,   0.0f, },
};
"""

FIELDS_BEFORE_AMMO = "1.0, 0.0, 0.0, 0.0, 3.0, 3.0, 8.5"
FIELDS_AFTER_MAG = (
    "0xFF, 0x00, 1, 0, 0, NULL, 1.0, 0.0, 0.0, SCALEVAL1, 0.9, 1.0, 0xFF, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 2.0, 4.0, 0.0,"
)


def stats(name: str, ammo: str, magazine: int, flags: str) -> str:
    return f"WeaponStats {name} = {{ {FIELDS_BEFORE_AMMO}, {ammo}, {magazine}, {FIELDS_AFTER_MAG} {flags} }};\n"


def write_decomp(root: Path, scope_stats: str = "scope") -> None:
    (root / "src" / "game").mkdir(parents=True)
    (root / "src" / "bondconstants.h").write_text(BONDCONSTANTS)
    (root / "src" / "game" / "gun.c").write_text(GUN_C)
    gun = root / "assets" / "obseg" / "gun"
    (gun / "pistol").mkdir(parents=True)
    (gun / "pistol" / "gunWeaponStat.inc.c").write_text(
        stats("pistol_stats", "AMMO_9MM", 7, "WEAPONSTATBITFLAG_CLICKY | // comment\n WEAPONSTATBITFLAG_ONLY_1_HANDED")
    )
    (gun / "gunWeaponStats.inc.c").write_text(
        "#ifdef BUGFIX_R0\n#define SCALEVAL1 0.8\n#endif\n"
        + stats("default_weaponstats", "0", 0, "WEAPONSTATBITFLAG_CLICKY")
        + "#include <assets/obseg/gun/pistol/gunWeaponStat.inc.c>\n"
        + stats("scope_stats", "0", 0, "WEAPONSTATBITFLAG_DISABLE_CROUCH")
    )
    (gun / "pistol" / "gunFileRecord.inc.c").write_text("GUNFILERECORD( pistol, 0, GUNSTATS(pistol), 1, 2)\n")
    (gun / "gunModelFileRecord.inc.c").write_text(
        "GunModelFileRecord gitem_structs[] = {\n"
        "    { NULL, NULL, TRUE, NULL, 0x1, 0x2},\n"
        "    #include <assets/obseg/gun/pistol/gunFileRecord.inc.c>\n"
        f"    {{ NULL, NULL, 0, GUNSTATS({scope_stats}), 1, 2}}\n"
        "};\n"
    )


class BuildWeaponStatsTest(unittest.TestCase):
    def test_one_line_per_item_then_the_ammo_limits(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_decomp(Path(tmp))
            lines = build_weapon_stats(tmp).decode().splitlines()
        self.assertEqual(
            lines,
            [
                "item\t0\tITEM_UNARMED\t0\t0\t0\t0x10",
                "item\t1\tITEM_PISTOL\t1\t1\t7\t0x110",
                "item\t2\tITEM_SCOPE\t0\t0\t0\t0x8000",
                "ammo\t0\tAMMO_NONE\t0",
                "ammo\t1\tAMMO_9MM\t800",
            ],
        )

    def test_stats_that_are_not_defined_are_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_decomp(Path(tmp), scope_stats="missing")
            with self.assertRaises(WeaponStatsError):
                build_weapon_stats(tmp)


class PackTest(unittest.TestCase):
    def test_writes_the_table_as_a_blob(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_decomp(Path(tmp) / "decomp")
            output = Path(tmp) / "out.o2r"
            with O2rWriter(output) as writer:
                self.assertEqual(pack_weapon_stats(Path(tmp) / "decomp", writer), RESOURCE_PATH)
            with zipfile.ZipFile(output) as archive:
                data = archive.read(RESOURCE_PATH)
        self.assertIn(b"item\t1\tITEM_PISTOL\t1\t1\t7\t0x110\n", data[OTR_HEADER_SIZE:])


if __name__ == "__main__":
    unittest.main()
