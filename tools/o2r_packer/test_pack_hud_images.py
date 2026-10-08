#!/usr/bin/env python3
"""Unit tests for pack_hud_images.py against a synthetic global image table
(no ROM needed)."""

import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

from ge_dlemit import G_SETTEX, OTR_G_SETTIMG_HASH, TextureBinding, path_hash
from o2r_writer import OTR_HEADER_SIZE, O2rWriter
from pack_hud_images import (
    EFFECT_RANGES,
    END_OFFSET,
    ENTRY_SIZE,
    FIRST_OFFSET,
    FLARE_LISTS,
    FOLDER_COUNT,
    FOLDER_FIRST,
    GLOBAL_LISTS,
    RADAR_FIRST,
    SINGLE_IMAGE_LISTS,
    TABLE_END,
    TABLE_PATH,
    TABLE_ROM_START,
    HudImagesError,
    global_list_path,
    image_path,
    pack_hud_images,
    read_entries,
)

# A display list that loads one image the way the table's own do: the
# texture, the tile that loads it, the load and the tile that draws it.
GLOBAL_LIST = (
    (0xE7000000, 0),
    (0xBA001402, 0),
    (0xFD700000, 0xABCD0000 | 0x83A),
    (0xF5700000, 0x07000000),
    (0xE6000000, 0),
    (0xF3000000, 0x0761F125),
    (0xF5680E00, 0x00018060),
    (0xF2000000, 0x000DC0DC),
    (0xBA000E02, 0),
    (0xB8000000, 0),
)


# A flare's list: a first image for tile 0, a second for tile 1 at TMEM 0x188.
FLARE_LIST = (
    (0xE7000000, 0),
    (0xFD700000, 0xABCD0000 | 0x824),
    (0xF5700000, 0x07000000),
    (0xE6000000, 0),
    (0xF3000000, 0x0761F125),
    (0xF5680E00, 0x00018060),
    (0xF2000000, 0x000DC0DC),
    (0xFD100000, 0xABCD0000 | 0x825),
    (0xF5100188, 0x07000000),
    (0xE6000000, 0),
    (0xF3000000, 0x070DF200),
    (0xF5100988, 0x01090A42),
    (0xF2000000, 0x010DC0DC),
    (0xBA000E02, 0),
    (0xB8000000, 0),
)


def make_rom(level: int = 0, effect_level: int = 0) -> bytes:
    rom = bytearray(TABLE_ROM_START + TABLE_END)
    for slot, offset in enumerate(range(FIRST_OFFSET, END_OFFSET, ENTRY_SIZE)):
        # texture id, 5x12, RGBA 32-bit, clamped in S, wrapping in T.
        struct.pack_into(">IBBBBBBB", rom, TABLE_ROM_START + offset, 100 + slot, 5, 12, level, 4, 3, 2, 0)
    for slot in range(FOLDER_COUNT):
        struct.pack_into(">IBBBBBBB", rom, TABLE_ROM_START + FOLDER_FIRST + slot * ENTRY_SIZE, 300 + slot, 32, 28, 0, 0, 3, 0, 2)
    for first, last in EFFECT_RANGES:
        for offset in range(first, last, ENTRY_SIZE):
            struct.pack_into(">IBBBBBBB", rom, TABLE_ROM_START + offset, 500 + offset, 8, 8, effect_level, 0, 2, 2, 0)
    for offset in SINGLE_IMAGE_LISTS:
        for index, (w0, w1) in enumerate(GLOBAL_LIST):
            struct.pack_into(">II", rom, TABLE_ROM_START + offset + index * 8, w0, w1)
    for offset in FLARE_LISTS:
        for index, (w0, w1) in enumerate(FLARE_LIST):
            struct.pack_into(">II", rom, TABLE_ROM_START + offset + index * 8, w0, w1)
    return bytes(rom)


def entry_count() -> int:
    return (END_OFFSET - FIRST_OFFSET) // ENTRY_SIZE + FOLDER_COUNT + sum((last - first) // ENTRY_SIZE for first, last in EFFECT_RANGES)


def path_count() -> int:
    # The table, the entries' lists and the effects' global lists.
    return 1 + entry_count() + len(GLOBAL_LISTS)


def lookup(texture_id: int) -> TextureBinding:
    return TextureBinding(f"textures/T{texture_id}", 0, 3, 5)


class ReadEntriesTest(unittest.TestCase):
    def test_reads_every_entry_in_the_range(self):
        entries = read_entries(make_rom())
        self.assertEqual(len(entries), entry_count())
        self.assertEqual(entries[0][0], EFFECT_RANGES[0][0])
        self.assertIn((FIRST_OFFSET, 100, 5, 12, 0, 2, 0, 4), entries)

    def test_rejects_a_table_that_is_not_there(self):
        with self.assertRaises(HudImagesError):
            read_entries(bytes(TABLE_ROM_START + END_OFFSET))

    def test_accepts_mipmapped_effect_entries(self):
        self.assertEqual(len(read_entries(make_rom(effect_level=6))), entry_count())

    def test_rejects_a_mipmapped_entry(self):
        with self.assertRaises(HudImagesError):
            read_entries(make_rom(level=3))


class PackTest(unittest.TestCase):
    def pack(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "out.o2r"
            with O2rWriter(path) as writer:
                written = pack_hud_images(make_rom(), lookup, writer)
            with zipfile.ZipFile(path) as z:
                return written, {name: z.read(name) for name in z.namelist()}

    def test_one_list_per_entry_at_its_offset(self):
        written, files = self.pack()
        self.assertEqual(written[0], TABLE_PATH)
        self.assertIn(image_path(FIRST_OFFSET), written)
        self.assertEqual(image_path(0xD2C), "hud/img/0d2c")
        self.assertEqual(image_path(0xAC8), "hud/fx/0ac8")
        self.assertEqual(image_path(FOLDER_FIRST), "hud/img/0fd8")
        self.assertEqual(len(written), path_count())
        self.assertEqual(sorted(files), sorted(written))

    def test_the_table_gives_each_entrys_size(self):
        _, files = self.pack()
        data = files[TABLE_PATH]
        self.assertIn(b"0xc84\t5\t12\t4\n", data)
        self.assertEqual(data.count(b"\n"), entry_count())

    def test_a_list_holds_the_request_and_the_texture_reference(self):
        _, files = self.pack()
        payload = files[image_path(FIRST_OFFSET)][OTR_HEADER_SIZE:]
        request, texture_id = struct.unpack_from(">II", payload, 8)
        # A clamped S, a wrapping T, no sample offset, the single-tile type.
        self.assertEqual(request, (G_SETTEX << 24) | (1 << 22) | (0 << 20) | 4)
        self.assertEqual(texture_id, 100)
        w0, _, digest = struct.unpack_from(">IIQ", payload, 16)
        self.assertEqual(w0, (OTR_G_SETTIMG_HASH << 24) | (0 << 21) | (3 << 19) | 4)
        self.assertEqual(digest, path_hash("textures/T100"))

    def test_an_effects_request_has_the_half_texel_sample_offset(self):
        _, files = self.pack()
        payload = files[image_path(EFFECT_RANGES[0][0])][OTR_HEADER_SIZE:]
        request, _ = struct.unpack_from(">II", payload, 8)
        self.assertEqual(request, (G_SETTEX << 24) | (1 << 22) | (0 << 20) | (2 << 18) | 4)

    def test_the_radars_circle_is_packed_as_an_effect_image(self):
        written, files = self.pack()
        self.assertEqual(image_path(RADAR_FIRST), "hud/fx/1020")
        self.assertIn("hud/fx/1020", written)
        self.assertIn(b"0x1020\t8\t8\t0\n", files[TABLE_PATH])

    def test_a_global_list_keeps_its_state_and_replaces_the_image_load(self):
        _, files = self.pack()
        payload = files[global_list_path(SINGLE_IMAGE_LISTS[0])][OTR_HEADER_SIZE:]
        words = [struct.unpack_from(">II", payload, 8 + i * 8) for i in range(2)]
        self.assertEqual(words, [GLOBAL_LIST[0], GLOBAL_LIST[1]])
        request, texture_id = struct.unpack_from(">II", payload, 24)
        self.assertEqual(request, (G_SETTEX << 24) | 4)
        self.assertEqual(texture_id, 0x83A)
        w0, _, digest = struct.unpack_from(">IIQ", payload, 32)
        self.assertEqual(w0 >> 24, OTR_G_SETTIMG_HASH)
        self.assertEqual(digest, path_hash("textures/T2106"))
        # What follows the render tile's size: the rest of the list.
        self.assertEqual(struct.unpack_from(">II", payload, 48), GLOBAL_LIST[8])
        self.assertEqual(struct.unpack_from(">II", payload, 56), GLOBAL_LIST[9])

    def test_a_flare_list_loads_its_second_image_into_tile_one(self):
        _, files = self.pack()
        payload = files[global_list_path(FLARE_LISTS[0])][OTR_HEADER_SIZE:]
        self.assertEqual(FLARE_LISTS[1] - FLARE_LISTS[0], 0xA8)
        self.assertEqual(FLARE_LISTS[-1], 0x9A8)
        # The first image is requested like a single image's.
        request, texture_id = struct.unpack_from(">II", payload, 16)
        self.assertEqual(request, (G_SETTEX << 24) | 4)
        self.assertEqual(texture_id, 0x824)
        # The second names the TMEM address and wraps of its render tile.
        request, texture_id = struct.unpack_from(">II", payload, 40)
        self.assertEqual(request, (G_SETTEX << 24) | (1 << 22) | (1 << 20) | (0x188 << 3) | 5)
        self.assertEqual(texture_id, 0x825)
        _, _, digest = struct.unpack_from(">IIQ", payload, 48)
        self.assertEqual(digest, path_hash("textures/T2085"))
        # What follows the render tile's size: the rest of the list.
        self.assertEqual(struct.unpack_from(">II", payload, 64), FLARE_LIST[13])
        self.assertEqual(struct.unpack_from(">II", payload, 72), FLARE_LIST[14])


if __name__ == "__main__":
    unittest.main()
