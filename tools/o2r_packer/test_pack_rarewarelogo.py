#!/usr/bin/env python3
"""Unit tests for pack_rarewarelogo.py against a synthetic segment (no ROM
needed)."""

import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

from ge_dlemit import GeDisplayListEmitError, iter_commands, path_hash
from o2r_writer import OTR_HEADER_SIZE, O2rWriter
from pack_rarewarelogo import (
    _SIGNATURE,
    SEGMENT_ROM_END,
    SEGMENT_ROM_START,
    RarewareLogoError,
    pack_rarewarelogo,
    read_segment,
)

SEGMENT_SIZE = SEGMENT_ROM_END - SEGMENT_ROM_START

G_VTX = 0x04
G_ENDDL = 0xB8
G_SETTIMG = 0xFD
G_LOADBLOCK = 0xF3
# G_SETTIMG's format and size fields for RGBA16.
RGBA16_SETTIMG = 0xFD100000


def command(w0: int, w1: int = 0) -> bytes:
    return struct.pack(">II", w0, w1)


def make_segment() -> bytearray:
    segment = bytearray(SEGMENT_SIZE)
    segment[: len(_SIGNATURE)] = _SIGNATURE
    # List 0: loads 16 vertices at 0x2b18.
    segment[0x43E8 : 0x43F8] = command(G_VTX << 24 | 0x100, 0x02002B18) + command(G_ENDDL << 24)
    # List 1: loads a 1372-texel image at 0x20, then ends.
    segment[0x44B0:0x44C8] = (
        command(RGBA16_SETTIMG, 0x02000020) + command(G_LOADBLOCK << 24, 1371 << 12) + command(G_ENDDL << 24)
    )
    # List 2: empty.
    segment[0x4758:0x4760] = command(G_ENDDL << 24)
    segment[0x2B18:0x2C18] = bytes(range(256))
    segment[0x4FE8:0x57E8] = bytes([0xAA]) * 0x800
    segment[0x5FF0:0x67F0] = bytes([0xBB]) * 0x800
    return segment


def pack(segment: bytes):
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "out.o2r"
        with O2rWriter(path) as writer:
            written = pack_rarewarelogo(segment, writer)
        with zipfile.ZipFile(path) as z:
            return written, {name: z.read(name) for name in z.namelist()}


class ReadSegmentTest(unittest.TestCase):
    def test_returns_the_segment_bytes(self):
        rom = bytearray(SEGMENT_ROM_END + 16)
        rom[SEGMENT_ROM_START : SEGMENT_ROM_START + len(_SIGNATURE)] = _SIGNATURE
        segment = read_segment(bytes(rom))
        self.assertEqual(len(segment), SEGMENT_SIZE)

    def test_rejects_a_rom_without_the_segment(self):
        with self.assertRaises(RarewareLogoError):
            read_segment(bytes(SEGMENT_ROM_END + 16))

    def test_rejects_a_truncated_rom(self):
        with self.assertRaises(RarewareLogoError):
            read_segment(bytes(SEGMENT_ROM_START))


class PackRarewareLogoTest(unittest.TestCase):
    def test_writes_lists_vertices_and_images_at_the_documented_paths(self):
        written, _ = pack(make_segment())
        self.assertEqual(
            sorted(written),
            [
                "rarewarelogo/dl/0043e8",
                "rarewarelogo/dl/0044b0",
                "rarewarelogo/dl/004758",
                "rarewarelogo/img/000020",
                "rarewarelogo/img/004fe8",
                "rarewarelogo/img/005ff0",
                "rarewarelogo/vtx/002b18",
            ],
        )

    def test_list_references_its_vertex_block_by_path(self):
        _, files = pack(make_segment())
        payload = files["rarewarelogo/dl/0043e8"][OTR_HEADER_SIZE:]
        digests = [d for _, _, _, d in iter_commands(payload) if d is not None]
        self.assertEqual(digests, [path_hash("rarewarelogo/vtx/002b18")])

    def test_code_loaded_images_carry_the_segment_bytes(self):
        _, files = pack(make_segment())
        self.assertIn(bytes([0xAA]) * 0x800, files["rarewarelogo/img/004fe8"])
        self.assertIn(bytes([0xBB]) * 0x800, files["rarewarelogo/img/005ff0"])

    def test_unresolvable_vertex_address_is_an_error(self):
        segment = make_segment()
        segment[0x43E8:0x43F0] = command(G_VTX << 24 | 0x100, 0x06002B18)
        with self.assertRaises(GeDisplayListEmitError):
            pack(segment)


if __name__ == "__main__":
    unittest.main()
