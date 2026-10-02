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
    AMMOTYPE_GLOBAL_MAX = AMMO_9MM,
    AMMO_DARTS,
    AMMOTYPE_MAX
} AMMOTYPE;
typedef enum TEXTBANK_LEVEL_INDEX
{
    LNULL,
    LTEST
} TEXTBANK_LEVEL_INDEX;
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
    { 0x32   , 0x02000C90,   1.0f, },
};
"""

FIELDS_BEFORE_AMMO = "1.0, 0.0, 0.0, 0.0, 3.0, 3.0, 8.5"
FIELDS_AFTER_MAG = (
    "0xFF, 0x00, 1, 0, 0, NULL, 1.0, 0.0, 0.0, SCALEVAL1, 0.9, 1.0, 0xFF, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 2.0, 4.0, 0.0,"
)


# MuzzleFlashExtension, PosX/Y/Z, PlayX/Y/Z, then the crosshair and aim-lock
# speeds (SCALEVAL1 is 0.8), sway and bolt recoil of the fields below.
PLACEMENT = "1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0.0"
# AutomaticFiringRate and SingleFiringRate, RecoilSpeed's four bytes,
# RecoilBack and RecoilUp, the cartridge, the noise fields, the zoom, inaccuracy, shoot-through limit and destruction amount.
FIRING = "-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\t0.0\t1.0\t1.0"


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
    text = root / "assets" / "obseg" / "text"
    text.mkdir(parents=True)
    (text / "LtestE.h").write_text("enum LTESTE_STRINGS\n{\n    TEST_STR_00_LF, // \"a\"\n    TEST_STR_01_LF, // \"b\"\n};\n")
    (gun / "gunWeaponStats.inc.c").write_text(
        "#ifdef BUGFIX_R0\n#define SCALEVAL1 0.8\n#endif\n"
        + stats("default_weaponstats", "0", 0, "WEAPONSTATBITFLAG_CLICKY")
        + "#include <assets/obseg/gun/pistol/gunWeaponStat.inc.c>\n"
        + stats("scope_stats", "0", 0, "WEAPONSTATBITFLAG_DISABLE_CROUCH")
    )
    (gun / "pistol" / "gunFileRecord.inc.c").write_text("GUNFILERECORD( pistol, 0, GUNSTATS(pistol), 1, getStringID(LTEST, TEST_STR_01_LF), 3.0, 4.0, 5.0, 6.0, 7.0, 8, 9, 10.0, 11.0, 12.0)\n")
    (gun / "gunModelFileRecord.inc.c").write_text(
        "GunModelFileRecord gitem_structs[] = {\n"
        "    { NULL, NULL, TRUE, NULL, 0x1, 0x2, 0.0, 0.0, 1000.0, 0.0, 0.0, 0x3, 0x4, 10.0, 10.0, 1000.0},\n"
        "    #include <assets/obseg/gun/pistol/gunFileRecord.inc.c>\n"
        f"    {{ NULL, NULL, 0, GUNSTATS({scope_stats}), 1, 2, 0.0, 0.0, 1000.0, 0.0, 0.0, 3, 4, 10.0, 10.0, 1000.0}}\n"
        "};\n"
    )


class BuildWeaponStatsTest(unittest.TestCase):
    def test_one_line_per_item_then_the_ammo_types(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_decomp(Path(tmp))
            lines = build_weapon_stats(tmp).decode().splitlines()
        self.assertEqual(
            lines,
            [
                f"item\t0\tITEM_UNARMED\t0\t0\t0\t0x10\t-\t-\t{PLACEMENT}\t{FIRING}",
                f"item\t1\tITEM_PISTOL\t1\t1\t7\t0x110\tmodels/gun/GpistolZ\t-\t{PLACEMENT}\t{FIRING}",
                f"item\t2\tITEM_SCOPE\t0\t0\t0\t0x8000\t-\t-\t{PLACEMENT}\t{FIRING}",
                "watch\t0\t0x1\t0x2\t0.0\t0.0\t1000.0\t0.0\t0.0\t0x3\t0x4\t10.0\t10.0\t1000.0",
                "watch\t1\t0x1\t0x401\t3.0\t4.0\t5.0\t6.0\t7.0\t0x8\t0x9\t10.0\t11.0\t12.0",
                "watch\t2\t0x1\t0x2\t0.0\t0.0\t1000.0\t0.0\t0.0\t0x3\t0x4\t10.0\t10.0\t1000.0",
                "ammo\t0\tAMMO_NONE\t0\t0x0\t0.0",
                "ammo\t1\tAMMO_9MM\t800\t0xc84\t0.0",
                "ammo\t2\tAMMO_DARTS\t50\t0xc90\t1.0",
            ],
        )

    def test_the_firing_columns_resolve_rates_recoil_bytes_and_the_cartridge(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_decomp(root)
            gun = root / "assets" / "obseg" / "gun"
            text = (gun / "gunWeaponStats.inc.c").read_text()
            text = "#ifdef BUGFIX_R0\n#define RATE_TEST 0x4\n#endif\n" + text
            (gun / "gunWeaponStats.inc.c").write_text(text)
            pistol = stats("pistol_stats", "AMMO_9MM", 7, "WEAPONSTATBITFLAG_CLICKY")
            pistol = pistol.replace("0xFF, 0x00, 1, 0, 0, NULL", "RATE_TEST, 0xFF, 1, 0, 0, &cartridge_header")
            pistol = pistol.replace("1.0, 0xFF, 0.0, 0.0", "1.0, 0x40C0006, 4.5, 2.0", 1)
            (gun / "pistol" / "gunWeaponStat.inc.c").write_text(pistol)
            lines = build_weapon_stats(tmp).decode().splitlines()
        self.assertTrue(lines[1].endswith("\t4\t-1\t4\t12\t0\t6\t4.5\t2.0\tmodels/gun/GcartridgeZ\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\t0.0\t1.0\t1.0"))

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
        self.assertIn(b"item\t1\tITEM_PISTOL\t1\t1\t7\t0x110\tmodels/gun/GpistolZ\t-\t", data[OTR_HEADER_SIZE:])


if __name__ == "__main__":
    unittest.main()
