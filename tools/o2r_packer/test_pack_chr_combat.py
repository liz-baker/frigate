#!/usr/bin/env python3
"""Unit tests for pack_chr_combat.py against a synthetic chr.c."""

import tempfile
import unittest
import zipfile
from pathlib import Path

from o2r_writer import OTR_HEADER_SIZE, O2rWriter
import pack_chr_combat as p


def _groups(prefix, table):
    entries = ",\n".join(f"    &ptr_{prefix}_group1" for _ in range(32))
    return (
        f"struct anim_group_info ptr_{prefix}_group1 = {{ &{table}, -1 }};\n"
        f"struct anim_group_info *ptr_{prefix}_groups[] = {{\n{entries}\n}};\n"
    )


SOURCE = (
    """
struct ChrHitReaction g_HitReactionTable[] = {
/* NULL */ {0, 0, 0, 0, 0, 0, 0, NULL, 0, NULL, 0},
/* FOOT */ {1, 1, 0, 17.0, 3, 0, 34.0, death_foot, 0, flinch_foot, 0},
/* end  */ {0xFFFFFFFF, 0, 0, 0.0, 0, 0, 0.0, NULL, 0, NULL, 0},
};
struct StruckAnim death_foot[] = {
    { PTR_ANIM_death_spin, 1, -1.0, 0.5, 1, 27.0, -1.0 },
    {0, 0, 0.0, 0.5, 0, -1.0, -1.0}
};
struct StruckAnim flinch_foot[] = {
    { PTR_ANIM_hit_leg, 0, 20.0, 0.5, 0, -1.0, -1.0 },
    {0, 0, 0.0, 0.5, 0, -1.0, -1.0}
};
struct explosion_death_animation D_8002E648[] = {
    { PTR_ANIM_death_explosion_forward, 0, 0.5, 9.0, 18.0, 29.0, -1.0 },
    {0, 0, 0.5, 0.0, 0.0, 0.0, -1.0},
};
s8 expl_forward[] = {0x0, 0x01, 0x00, 0x00};
struct explosion_anim_group_info explosion_animation_table[EXPLOSION_ANIMATION_TABLE_LEN] = {
    {expl_forward, 1}
};
/* the run-on: group1 has no terminator, so it continues into groupA */
struct weapon_firing_animation_table kneel_group1[] = {
    { PTR_ANIM_fire_kneel_a, 27.0, 0, 0, 0, -1.0, 35.0, 75.0, -1.0, -1.0, 31.0, 75.0, 0.87266463, -0.69813174, 0.9, -0.69813174, 1.5, 1.5 },
};
struct weapon_firing_animation_table kneel_groupA[] = {
    { PTR_ANIM_fire_kneel_b, 24.0, 0, 0, 0, -1.0, 46.0, 98.0, -1.0, -1.0, 41.0, 98.0, 0.87266463, -0.52359879, 1.1, -0.69813174, 1.6, 1.6 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};
"""
    + _groups("rifle_firing_animation", "kneel_group1")
)


class PackChrCombatTest(unittest.TestCase):
    def test_a_table_without_a_terminator_runs_into_the_next(self):
        tables = p.parse_firing(p._strip_comments(SOURCE))
        self.assertEqual([entry[0] for entry in tables["kneel_group1"]], ["fire_kneel_a", "fire_kneel_b"])
        self.assertEqual([entry[0] for entry in tables["kneel_groupA"]], ["fire_kneel_b"])

    def test_floats_are_single_precision(self):
        tables = p.parse_firing(p._strip_comments(SOURCE))
        # 0.87266463 as a float, written back as a double.
        self.assertEqual(float(tables["kneel_group1"][0][12]), 0.8726646304130554)

    def test_reactions_name_their_lists(self):
        source = p._strip_comments(SOURCE)
        reactions = p.parse_reactions(source, p.parse_struck(source))
        self.assertEqual(reactions[0], ("0", "0", "0.0", "0", "0.0", "-", "-"))
        self.assertEqual(reactions[1], ("1", "1", "17.0", "3", "34.0", "death_foot", "flinch_foot"))
        self.assertEqual(len(reactions), 2)

    def test_struck_lists_stop_at_the_terminator(self):
        struck = p.parse_struck(p._strip_comments(SOURCE))
        self.assertEqual(struck["death_foot"], [("death_spin", "1", "-1.0", "0.5", "1", "27.0", "-1.0")])

    def test_explosion_groups_keep_count_entries(self):
        self.assertEqual(p.parse_explosion_groups(p._strip_comments(SOURCE)), [[0]])

    def test_a_set_must_have_thirty_two_groups(self):
        broken = SOURCE.replace("    &ptr_rifle_firing_animation_group1,\n", "", 1)
        with self.assertRaises(p.ChrCombatError):
            p.parse_groups(p._strip_comments(broken))

    def test_rejects_an_animation_without_a_clip(self):
        with self.assertRaises(p.ChrCombatError):
            p.build_chr_combat(SOURCE, {"death_spin"})

    def test_text_lines(self):
        text = p.build_chr_combat(SOURCE).decode("ascii")
        self.assertIn("struck\tdeath_foot\t0\tdeath_spin\t1\t-1.0\t0.5\t1\t27.0\t-1.0\n", text)
        self.assertIn("group\trifle\t31\tkneel_group1\n", text)
        self.assertIn("explosiongroup\t0\t0\n", text)

    def test_writes_the_resource(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp = Path(tmp) / "decomp"
            (decomp / "src" / "game").mkdir(parents=True)
            (decomp / "src" / "game" / "chr.c").write_text(SOURCE)
            output = Path(tmp) / "out.o2r"
            with O2rWriter(output) as writer:
                self.assertEqual(p.pack_chr_combat(decomp, writer), "chr/combat")
            with zipfile.ZipFile(output) as archive:
                data = archive.read("chr/combat")
            self.assertTrue(data[OTR_HEADER_SIZE:].endswith(p.build_chr_combat(SOURCE)))


if __name__ == "__main__":
    unittest.main()
