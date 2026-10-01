#!/usr/bin/env python3
"""Unit tests for pack_sky_images.py against a synthetic global image table
(no ROM needed)."""

import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

from ge_dlemit import G_SETTEX, OTR_G_SETTIMG_HASH, TextureBinding, path_hash
from o2r_writer import OTR_HEADER_SIZE, O2rWriter
from pack_hud_images import ENTRY_SIZE, TABLE_ROM_START
from pack_sky_images import (
    ENTRY_COUNT,
    TABLE_OFFSET,
    SkyImagesError,
    image_path,
    pack_sky_images,
    read_entries,
)


def make_rom(levels=(0, 0, 6)) -> bytes:
    rom = bytearray(TABLE_ROM_START + TABLE_OFFSET + ENTRY_COUNT * ENTRY_SIZE)
    for index in range(ENTRY_COUNT):
        # texture id, 64x64, IA 8-bit, wrapping in both axes.
        struct.pack_into(
            ">IBBBBBBB", rom, TABLE_ROM_START + TABLE_OFFSET + index * ENTRY_SIZE, 200 + index, 64, 64, levels[index], 3, 1, 0, 0
        )
    return bytes(rom)


def lookup(texture_id: int) -> TextureBinding:
    return TextureBinding(f"textures/T{texture_id}", 3, 1, 64)


class ReadEntriesTest(unittest.TestCase):
    def test_reads_the_three_entries(self):
        entries = read_entries(make_rom())
        self.assertEqual(len(entries), ENTRY_COUNT)
        self.assertEqual(entries[0], (0, 200, 64, 64, 0, 0, 0))
        self.assertEqual(entries[2][4], 6)

    def test_rejects_a_table_that_is_not_there(self):
        with self.assertRaises(SkyImagesError):
            read_entries(bytes(TABLE_ROM_START + TABLE_OFFSET + ENTRY_COUNT * ENTRY_SIZE))


class PackTest(unittest.TestCase):
    def pack(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "out.o2r"
            with O2rWriter(path) as writer:
                written = pack_sky_images(make_rom(), lookup, writer)
            with zipfile.ZipFile(path) as z:
                return written, {name: z.read(name) for name in z.namelist()}

    def test_one_list_per_entry(self):
        written, files = self.pack()
        self.assertEqual(written, [image_path(0), image_path(1), image_path(2)])
        self.assertEqual(image_path(1), "sky/img/1")
        self.assertEqual(sorted(files), sorted(written))

    def test_a_plain_image_is_a_single_tile_with_a_sample_offset(self):
        _, files = self.pack()
        payload = files[image_path(0)][OTR_HEADER_SIZE:]
        request, texture_id = struct.unpack_from(">II", payload, 8)
        self.assertEqual(request, (G_SETTEX << 24) | (2 << 18) | 4)
        self.assertEqual(texture_id, 200)
        w0, _, digest = struct.unpack_from(">IIQ", payload, 16)
        self.assertEqual(w0, (OTR_G_SETTIMG_HASH << 24) | (3 << 21) | (1 << 19) | 63)
        self.assertEqual(digest, path_hash("textures/T200"))

    def test_a_mipmapped_image_is_a_mipmap_request(self):
        _, files = self.pack()
        payload = files[image_path(2)][OTR_HEADER_SIZE:]
        request, _ = struct.unpack_from(">II", payload, 8)
        self.assertEqual(request & 7, 2)


if __name__ == "__main__":
    unittest.main()
