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
    END_OFFSET,
    ENTRY_SIZE,
    FIRST_OFFSET,
    TABLE_PATH,
    TABLE_ROM_START,
    HudImagesError,
    image_path,
    pack_hud_images,
    read_entries,
)


def make_rom(level: int = 0) -> bytes:
    rom = bytearray(TABLE_ROM_START + END_OFFSET)
    for slot, offset in enumerate(range(FIRST_OFFSET, END_OFFSET, ENTRY_SIZE)):
        # texture id, 5x12, RGBA 32-bit, clamped in S, wrapping in T.
        struct.pack_into(">IBBBBBBB", rom, TABLE_ROM_START + offset, 100 + slot, 5, 12, level, 0, 3, 2, 0)
    return bytes(rom)


def lookup(texture_id: int) -> TextureBinding:
    return TextureBinding(f"textures/T{texture_id}", 0, 3, 5)


class ReadEntriesTest(unittest.TestCase):
    def test_reads_every_entry_in_the_range(self):
        entries = read_entries(make_rom())
        self.assertEqual(len(entries), (END_OFFSET - FIRST_OFFSET) // ENTRY_SIZE)
        self.assertEqual(entries[0], (FIRST_OFFSET, 100, 5, 12, 0, 2, 0))

    def test_rejects_a_table_that_is_not_there(self):
        with self.assertRaises(HudImagesError):
            read_entries(bytes(TABLE_ROM_START + END_OFFSET))

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
        self.assertEqual(written[1], image_path(FIRST_OFFSET))
        self.assertEqual(image_path(0xD2C), "hud/img/0d2c")
        self.assertEqual(sorted(files), sorted(written))

    def test_the_table_gives_each_entrys_size(self):
        _, files = self.pack()
        data = files[TABLE_PATH]
        self.assertIn(b"0xc84\t5\t12\n", data)
        self.assertEqual(data.count(b"\n"), (END_OFFSET - FIRST_OFFSET) // ENTRY_SIZE)

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


if __name__ == "__main__":
    unittest.main()
