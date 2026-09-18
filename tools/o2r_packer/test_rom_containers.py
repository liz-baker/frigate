#!/usr/bin/env python3
"""Opt-in check of the container unwrap plus display-list walker against
your own US ROM. Skipped unless `FRIGATE_ROMS` points at it (see
rom_regions.py).

    FRIGATE_ROMS=/path/to/roms python3 -m unittest test_rom_containers -v
"""

import unittest
from pathlib import Path

from ge_container import unwrap_bg_file, unwrap_model_file
from ge_displaylist import merge_vertex_ranges, walk_display_list
from ge_model import parse_model_tree, read_model_header_counts
from rom_regions import ROMS_ENV_VAR, discover_roms, locations_from_env

DECOMP_DIR = Path(__file__).resolve().parents[2] / "external" / "007"
FILELIST = DECOMP_DIR / "scripts" / "filelist.u.csv"

# One file per model directory; the gun uses plain display-list nodes and
# the character uses collision nodes.
MODEL_FILES = (
    "assets/obseg/chr/CarmourguardZ.bin",
    "assets/obseg/gun/Gak47Z.bin",
    "assets/obseg/prop/ParticZ.bin",
)
BG_FILE = "assets/obseg/bg/bg_sev_all_p.bin"

def _file_bytes(rom_path: Path, name: str) -> bytes:
    for line in FILELIST.read_text().splitlines():
        parts = line.strip().split(",")
        if len(parts) == 5 and parts[2] == name:
            with open(rom_path, "rb") as f:
                f.seek(int(parts[0]))
                return f.read(int(parts[1]))
    raise LookupError(f"{name} not in the decomp's filelist")


class RomContainerTest(unittest.TestCase):
    def setUp(self):
        roms = discover_roms(locations_from_env(), DECOMP_DIR)
        if "u" not in roms:
            self.skipTest(f"set {ROMS_ENV_VAR} to your own US ROM file or directory to run this")
        self.rom = roms["u"]

    def _check_model(self, name, counts):
        image = unwrap_model_file(_file_bytes(self.rom, name))
        tree = parse_model_tree(image, counts[name.rsplit("/", 1)[1][1:-len("Z.bin")]])

        self.assertIsNone(tree.nodes[tree.root].parent, name)
        walked = 0
        zero_count_loads = []
        for node in tree.nodes.values():
            if node.next is not None:
                self.assertEqual(tree.nodes[node.next].prev, node.offset, name)
                self.assertEqual(tree.nodes[node.next].parent, node.parent, name)
            if node.child is not None:
                self.assertEqual(tree.nodes[node.child].parent, node.offset, name)

            record = node.display_list
            if record is None:
                continue
            resolve = image.display_list_resolver(record.vertices_address)
            for start in (record.primary, record.secondary):
                if start is None:
                    continue
                refs = walk_display_list(image.data, start, resolve)
                walked += 1
                self.assertEqual(refs.unresolved, [], f"{name} list at {start:#x}")
                for offset, length in merge_vertex_ranges(refs):
                    self.assertGreaterEqual(offset, record.vertices)
                    if record.num_vertices == 0:
                        zero_count_loads.append(name)
                    else:
                        self.assertLessEqual(offset + length, record.vertices + record.vertex_bytes, name)
        self.assertGreater(walked, 0, name)
        return zero_count_loads

    def test_model_trees_are_well_formed(self):
        counts = read_model_header_counts(DECOMP_DIR)
        for name in MODEL_FILES:
            with self.subTest(name):
                self.assertEqual(self._check_model(name, counts), [])

    def test_every_model_file_parses(self):
        counts = read_model_header_counts(DECOMP_DIR)
        names = [
            line.split(",")[2]
            for line in FILELIST.read_text().splitlines()
            if any(f"/obseg/{kind}/" in line for kind in ("chr", "gun", "prop"))
        ]
        self.assertEqual(len(names), 512)
        zero_count_loads = set()
        for name in names:
            with self.subTest(name):
                zero_count_loads.update(self._check_model(name, counts))
        # A node whose count field is 0 yet whose lists load vertices; the
        # count is not a usable bound for it, so only this file may do it.
        self.assertEqual(zero_count_loads, {"assets/obseg/prop/PexplosionbitZ.bin"})

    def test_bg_room_lists_walk_cleanly(self):
        rooms = unwrap_bg_file(_file_bytes(self.rom, BG_FILE))
        self.assertGreater(len(rooms), 1)

        for room in rooms:
            for name, blob in (("primary", room.primary), ("secondary", room.secondary)):
                if blob is None:
                    continue
                refs = walk_display_list(blob, 0, room.resolve)
                self.assertEqual(refs.unresolved, [], f"room {room.index} {name}")
                for offset, length in merge_vertex_ranges(refs):
                    self.assertLessEqual(offset + length, len(room.vertices), f"room {room.index} {name}")


if __name__ == "__main__":
    unittest.main()
