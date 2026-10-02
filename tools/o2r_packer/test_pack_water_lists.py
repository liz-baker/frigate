#!/usr/bin/env python3
"""Unit tests for pack_water_lists.py."""

import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

from ge_dlemit import UCODE_INDY_GE, WATER_TEXTURES, water_list_path
from o2r_writer import OTR_HEADER_SIZE, O2rWriter
from pack_water_lists import WaterListsError, encode_water_list, pack_water_lists, water_commands


class WaterListsTest(unittest.TestCase):
    def test_each_list_ends_with_the_end_of_the_list(self):
        for texture_id in WATER_TEXTURES:
            commands = water_commands(texture_id)
            self.assertEqual(commands[-1], (0xB8000000, 0))
            self.assertEqual(len(commands), 13)

    def test_the_words_the_game_rewrites_are_where_it_expects_them(self):
        for texture_id in WATER_TEXTURES:
            commands = water_commands(texture_id)
            self.assertEqual(commands[0][0] >> 24, 0xF5)
            self.assertEqual(commands[1][0] >> 24, 0xF5)
            self.assertEqual(commands[2][0] >> 24, 0xF2)
            self.assertEqual(commands[3][0] >> 24, 0xF2)
            self.assertEqual(commands[4][0] >> 24, 0xFA)

    def test_the_tiles_differ_by_image(self):
        self.assertNotEqual(water_commands(WATER_TEXTURES[0])[0], water_commands(WATER_TEXTURES[1])[0])

    def test_payload_starts_with_the_microcode(self):
        payload = encode_water_list(WATER_TEXTURES[1])
        self.assertEqual(payload[:8], bytes([UCODE_INDY_GE]) + bytes(7))
        self.assertEqual(struct.unpack_from(">II", payload, 8), water_commands(WATER_TEXTURES[1])[0])

    def test_unknown_image_is_an_error(self):
        with self.assertRaises(WaterListsError):
            water_commands(1)

    def test_packs_one_resource_per_image(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "out.o2r"
            with O2rWriter(output) as writer:
                paths = pack_water_lists(writer)
            self.assertEqual(paths, [water_list_path(t) for t in WATER_TEXTURES])
            with zipfile.ZipFile(output) as archive:
                data = archive.read(paths[0])
        self.assertEqual(data[OTR_HEADER_SIZE:], encode_water_list(WATER_TEXTURES[0]))


if __name__ == "__main__":
    unittest.main()
