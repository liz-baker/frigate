#!/usr/bin/env python3
"""Opt-in check of the container unwrap plus display-list walker against
your own US ROM. Skipped unless `FRIGATE_ROMS` points at it (see
rom_regions.py).

    FRIGATE_ROMS=/path/to/roms python3 -m unittest test_rom_containers -v
"""

import struct
import unittest
from pathlib import Path

from ge_container import unwrap_bg_file, unwrap_model_file
from ge_displaylist import VTX_SIZE, merge_vertex_ranges, walk_display_list
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

# Display-list node opcodes -> (vertex pointer offset, vertex count offset)
# within the node's data record. Both keep the primary and secondary lists
# at offsets 0 and 4: the plain node (4) and the collision node (0x18).
DL_RECORD_LAYOUTS = {4: (0x0C, 0x10), 0x18: (0x08, 0x0C)}


def _file_bytes(rom_path: Path, name: str) -> bytes:
    for line in FILELIST.read_text().splitlines():
        parts = line.strip().split(",")
        if len(parts) == 5 and parts[2] == name:
            with open(rom_path, "rb") as f:
                f.seek(int(parts[0]))
                return f.read(int(parts[1]))
    raise LookupError(f"{name} not in the decomp's filelist")


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def _display_list_nodes(image):
    """Every display-list node's data record reachable from the first node the
    file's switch table points at. A stand-in for the real node-tree
    parse, which is a later step: it climbs to the root and walks
    child/next links."""
    data = image.data
    node = image.resolve(_u32(data, 0))
    while (parent := image.resolve(_u32(data, node + 8))) is not None:
        node = parent

    seen, stack = set(), [node]
    while stack:
        node = stack.pop()
        if node is None or node in seen:
            continue
        seen.add(node)
        opcode = struct.unpack_from(">H", data, node)[0] & 0xFF
        if opcode in DL_RECORD_LAYOUTS:
            yield opcode, image.resolve(_u32(data, node + 4))
        stack.append(image.resolve(_u32(data, node + 0x14)))
        stack.append(image.resolve(_u32(data, node + 0x0C)))


class RomContainerTest(unittest.TestCase):
    def setUp(self):
        roms = discover_roms(locations_from_env(), DECOMP_DIR)
        if "u" not in roms:
            self.skipTest(f"set {ROMS_ENV_VAR} to your own US ROM file or directory to run this")
        self.rom = roms["u"]

    def _check_model(self, name):
        image = unwrap_model_file(_file_bytes(self.rom, name))

        walked = 0
        for opcode, record in _display_list_nodes(image):
            vertices_field, count_field = DL_RECORD_LAYOUTS[opcode]
            vertices_address = _u32(image.data, record + vertices_field)
            vertices_at = image.resolve(vertices_address)
            vertex_bytes = struct.unpack_from(">H", image.data, record + count_field)[0] * VTX_SIZE
            resolve = image.display_list_resolver(vertices_address)

            for list_field in (0, 4):
                start = image.resolve(_u32(image.data, record + list_field))
                if start is None:
                    continue
                refs = walk_display_list(image.data, start, resolve)
                walked += 1
                self.assertEqual(refs.unresolved, [], f"{name} list at {start:#x}")
                for offset, length in merge_vertex_ranges(refs):
                    self.assertGreaterEqual(offset, vertices_at)
                    self.assertLessEqual(offset + length, vertices_at + vertex_bytes)
        self.assertGreater(walked, 0, name)

    def test_model_display_lists_walk_cleanly(self):
        for name in MODEL_FILES:
            with self.subTest(name):
                self._check_model(name)

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
