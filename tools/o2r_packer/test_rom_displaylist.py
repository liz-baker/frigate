#!/usr/bin/env python3
"""Opt-in check of the display-list walker against your own US ROM.
Skipped unless `FRIGATE_ROMS` points at it (see rom_regions.py).

    FRIGATE_ROMS=/path/to/roms python3 -m unittest test_rom_displaylist -v

The font display list is the only display list stored uncompressed at a
fixed ROM offset (bg and model lists sit inside compressed containers),
so it is the one real list this can check without a container unwrap.
"""

import unittest
from pathlib import Path

from ge_displaylist import walk_display_list
from rom_regions import ROMS_ENV_VAR, discover_roms, locations_from_env

DECOMP_DIR = Path(__file__).resolve().parents[2] / "external" / "007"

# Opcodes the font list is expected to use, all F3D-numbered: geometry
# mode clear/set, texture, other-mode high/low, combine, pipe sync, end.
EXPECTED_OPCODES = {0xB6, 0xB7, 0xBB, 0xBA, 0xB9, 0xFC, 0xE7, 0xB8}


def _font_dl_range(filelist: Path):
    for line in filelist.read_text().splitlines():
        parts = line.strip().split(",")
        if len(parts) == 5 and parts[2].endswith("jfont_dl.bin"):
            return int(parts[0]), int(parts[1])
    raise LookupError("no jfont_dl entry in the decomp's filelist")


class RomDisplayListTest(unittest.TestCase):
    def test_font_display_list_walks_cleanly(self):
        roms = discover_roms(locations_from_env(), DECOMP_DIR)
        if "u" not in roms:
            self.skipTest(f"set {ROMS_ENV_VAR} to your own US ROM file or directory to run this")

        offset, size = _font_dl_range(DECOMP_DIR / "scripts" / "filelist.u.csv")
        with open(roms["u"], "rb") as f:
            f.seek(offset)
            data = f.read(size)

        # The blob holds three back-to-back lists, each ended by G_ENDDL.
        opcodes = set()
        for start in (0x00, 0x20, 0x40):
            refs = walk_display_list(data, start, lambda addr: None)
            self.assertEqual(refs.unresolved, [], f"list at {start:#x}")
            self.assertEqual(refs.opcode_counts[0xB8], 1, f"list at {start:#x}")
            opcodes |= set(refs.opcode_counts)

        self.assertLessEqual(opcodes, EXPECTED_OPCODES)

if __name__ == "__main__":
    unittest.main()
