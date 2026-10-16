#!/usr/bin/env python3
"""Unit tests for the table generators (tools/gen_tables.py, tools/tables/), on synthetic sources. The last test
regenerates the committed tables from external/007 and skips without that checkout."""

import re
import subprocess
import sys
import unittest
from pathlib import Path

from tables import combat, models, monitors, weapons
from tables.common import Decomp, REGION_DEFINES, TableError, enum_entries, preprocess

ROOT = Path(__file__).resolve().parent.parent


class PreprocessTest(unittest.TestCase):
    def test_picks_the_branch_of_the_defined_symbols(self):
        text = "a\n#ifdef X\nb\n#else\nc\n#endif\nd"
        self.assertEqual(preprocess(text, {"X"}).split(), ["a", "b", "d"])
        self.assertEqual(preprocess(text, set()).split(), ["a", "c", "d"])

    def test_elif_chain_takes_only_the_first_live_branch(self):
        text = "#ifdef A\n1\n#else\n#ifdef B\n2\n#else\n#ifdef C\n3\n#endif\n#endif\n#endif"
        self.assertEqual(preprocess(text, {"B", "C"}).split(), ["2"])
        text = "#if defined(A)\na\n#elif defined(B) && !defined(C)\nb\n#else\nc\n#endif"
        self.assertEqual(preprocess(text, {"B"}).split(), ["b"])
        self.assertEqual(preprocess(text, {"B", "C"}).split(), ["c"])

    def test_a_nested_conditional_in_a_dead_branch_stays_dead(self):
        text = "#ifdef A\n#ifndef B\nx\n#endif\n#else\ny\n#endif"
        self.assertEqual(preprocess(text, set()).split(), ["y"])

    def test_unterminated_conditional_is_an_error(self):
        with self.assertRaises(TableError):
            preprocess("#ifdef A\nx", set())

    def test_other_directives_are_kept(self):
        self.assertIn("#define X 1", preprocess("#define X 1\n#include <a.h>", set()))


class EnumTest(unittest.TestCase):
    def test_values_follow_explicit_numbers_and_earlier_names(self):
        text = "typedef enum E { A, B = 5, C, D = A, F } E;"
        self.assertEqual(enum_entries(text, "E"), [("A", 0), ("B", 5), ("C", 6), ("D", 0), ("F", 1)])


WEAPON_CONSTANTS = """
#ifdef BUGFIX_R0
#define SCALEVAL1 0.80000001
#define RATE_UZI 0x4
#else
#ifdef BUGFIX_R2
#define SCALEVAL1 0.881200015545f
#define RATE_UZI 0x3
#else
#ifdef BUGFIX_R1
#define SCALEVAL1 0.8812
#define RATE_UZI 0x4
#endif
#endif
#endif
"""


class WeaponRegionTest(unittest.TestCase):
    def constants(self, region):
        text = preprocess(WEAPON_CONSTANTS, REGION_DEFINES[region])
        return {name: value for name, value in weapons._SCALE_DEFINE_RE.findall(text)}

    def test_each_region_gets_its_own_scale_and_rate_constants(self):
        self.assertEqual(self.constants("us"), {"SCALEVAL1": "0.80000001", "RATE_UZI": "0x4"})
        self.assertEqual(self.constants("eur"), {"SCALEVAL1": "0.881200015545", "RATE_UZI": "0x3"})
        self.assertEqual(self.constants("jp"), {"SCALEVAL1": "0.8812", "RATE_UZI": "0x4"})


class CombatParseTest(unittest.TestCase):
    def test_animation_tables_mark_placeholders(self):
        source = """
        s32 animation_table_ptrs1[] = { PTR_ANIM_idle, PTR_ANIM_null50, PTR_ANIM_walking, 0 };
        struct ModelAnimation *animation_table_ptrs2[] = { PTR_ANIM_helicopter_cradle, 0 };
        """
        tables = combat.parse_animation_tables(source)
        self.assertEqual(tables[1], ["idle", None, "walking"])
        self.assertEqual(tables[2], ["helicopter_cradle"])
        with self.assertRaises(TableError):
            combat.parse_animation_tables("int x;")

    def test_struck_lists_stop_at_the_terminator_and_round_to_single_precision(self):
        source = """
        struct StruckAnim death[] = {
            {PTR_ANIM_a, 1, 0.1, 2.0, 3, 4.0, 5.0},
            {0, 0, 0, 0, 0, 0, 0},
            {PTR_ANIM_never, 0, 0, 0, 0, 0, 0},
        };
        """
        lists = combat.parse_struck(source)
        self.assertEqual(list(lists), ["death"])
        self.assertEqual(lists["death"], [("a", 1, 0.10000000149011612, 2.0, 3, 4.0, 5.0)])

    def test_a_firing_table_without_a_terminator_runs_into_the_next(self):
        row = "{PTR_ANIM_x, " + ", ".join(["1.0"] * 17) + "}"
        end = "{0, " + ", ".join(["0"] * 17) + "}"
        source = f"""
        struct weapon_firing_animation_table first[] = {{ {row} }};
        struct weapon_firing_animation_table second[] = {{ {row}, {end} }};
        """
        tables = combat.parse_firing(source)
        self.assertEqual([len(tables["first"]), len(tables["second"])], [2, 1])


class MonitorExpandTest(unittest.TestCase):
    HEADER = """
    #define MONHOLDTIME(t) 0x8, t
    #define MONZOOMWIDTH(x,t) 0x5, x, t
    #define MONZOOMSQUARE(w,t) MONZOOMWIDTH(w, t), MONHOLDTIME(t)
    #define COLOR_WHITE 0xFFFFFFFF
    """

    def test_macros_expand_recursively(self):
        macros = monitors.parse_macros(self.HEADER)
        self.assertEqual(
            [t.strip() for t in monitors._expand("MONZOOMSQUARE(0x200, 20)", macros).split(",")],
            ["0x5", "0x200", "20", "0x8", "20"],
        )

    def test_words_resolve_lists_images_and_numbers(self):
        lists, images = {"monA": 0}, {"IMGBOND": 3}
        self.assertEqual(monitors._word("monA", lists, images), ("monA", 0))
        self.assertEqual(monitors._word("IMGBOND", lists, images), (None, 3))
        self.assertEqual(monitors._word("-1", lists, images), (None, 0xFFFFFFFF))
        with self.assertRaises(TableError):
            monitors._word("nothing", lists, images)


class ChrSlotTest(unittest.TestCase):
    def test_heads_share_the_slot_space_and_all_bonds_bodies_are_dropped(self):
        text = preprocess(
            """
            typedef enum BODIES { BODY_A, BODY_B,
            #ifdef ALL_BONDS
            BODY_EXTRA,
            #endif
            BODIES_MAX } BODIES;
            typedef enum HEADS { HEAD_START = BODY_B, HEAD_X, HEAD_END } HEADS;
            """,
            set(),
        )
        slots = models.slot_names(text)
        self.assertEqual(slots[0], ["BODY_A"])
        self.assertEqual(slots[1], ["BODY_B"])
        self.assertEqual(slots[2], ["HEAD_X"])


class CommittedTablesTest(unittest.TestCase):
    @unittest.skipUnless((ROOT / "external" / "007" / "src" / "bondconstants.h").exists(), "no decomp checkout")
    def test_committed_tables_match_the_decomp(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools" / "gen_tables.py"), "--check"], capture_output=True, text=True
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
