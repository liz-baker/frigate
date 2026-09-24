#!/usr/bin/env python3
"""Unit tests for pack_eyeintro.py against synthetic ROM data (no ROM
needed)."""

import struct
import tempfile
import unittest
import zlib
import zipfile
from pathlib import Path

from o2r_writer import OTR_HEADER_SIZE, O2rWriter
from pack_eyeintro import (
    BLOOD_OFFSET,
    BLOOD_PATH,
    BLOOD_SIZE,
    GAME_SEGMENT_ROM_START,
    SIGHT_HEIGHT,
    SIGHT_PATH,
    SIGHT_ROM_SIZE,
    SIGHT_ROM_START,
    SIGHT_WIDTH,
    EyeIntroError,
    _BLOOD_SIGNATURE,
    pack_eyeintro,
    read_blood_stream,
    read_sight,
    rle_expand_8bit,
)


def rle(width: int, height: int, runs) -> bytes:
    return struct.pack(">HH6x", width, height) + b"".join(bytes([count, value]) for count, value in runs)


def make_rom() -> bytes:
    rom = bytearray(SIGHT_ROM_START + SIGHT_ROM_SIZE)
    pixels = SIGHT_WIDTH * SIGHT_HEIGHT
    runs = [(255, 0x10)] * (pixels // 255) + [(pixels % 255, 0x20)]
    data = rle(SIGHT_WIDTH, SIGHT_HEIGHT, runs)
    rom[SIGHT_ROM_START : SIGHT_ROM_START + len(data)] = data

    segment = bytearray(BLOOD_OFFSET + BLOOD_SIZE + 8)
    segment[BLOOD_OFFSET : BLOOD_OFFSET + len(_BLOOD_SIGNATURE)] = _BLOOD_SIGNATURE
    compressor = zlib.compressobj(wbits=-15)
    blob = b"\x11\x72" + compressor.compress(bytes(segment)) + compressor.flush()
    rom[GAME_SEGMENT_ROM_START : GAME_SEGMENT_ROM_START + len(blob)] = blob
    return bytes(rom)


class RleExpandTest(unittest.TestCase):
    def test_expands_runs(self):
        self.assertEqual(rle_expand_8bit(rle(4, 2, [(3, 7), (5, 9)])), bytes([7, 7, 7, 9, 9, 9, 9, 9]))

    def test_rejects_a_stream_that_ends_early(self):
        with self.assertRaises(EyeIntroError):
            rle_expand_8bit(rle(4, 2, [(3, 7)]))


class ReadRomTest(unittest.TestCase):
    def test_reads_the_backdrop(self):
        sight = read_sight(make_rom())
        self.assertEqual(len(sight), SIGHT_WIDTH * SIGHT_HEIGHT)
        self.assertEqual((sight[0], sight[-1]), (0x10, 0x20))

    def test_rejects_a_rom_without_the_backdrop(self):
        with self.assertRaises(EyeIntroError):
            read_sight(bytes(SIGHT_ROM_START + SIGHT_ROM_SIZE))

    def test_reads_the_blood_stream(self):
        stream = read_blood_stream(make_rom())
        self.assertEqual(len(stream), BLOOD_SIZE)
        self.assertTrue(stream.startswith(_BLOOD_SIGNATURE))

    def test_rejects_a_rom_without_the_blood_stream(self):
        rom = bytearray(make_rom())
        rom[GAME_SEGMENT_ROM_START] = 0
        with self.assertRaises(EyeIntroError):
            read_blood_stream(bytes(rom))


class PackEyeIntroTest(unittest.TestCase):
    def pack(self, sight: bytes, blood: bytes):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "out.o2r"
            with O2rWriter(path) as writer:
                written = pack_eyeintro(sight, blood, writer)
            with zipfile.ZipFile(path) as z:
                return written, {name: z.read(name) for name in z.namelist()}

    def test_writes_the_backdrop_and_the_blood_stream(self):
        sight = bytes(range(256)) * (SIGHT_WIDTH * SIGHT_HEIGHT // 256) + bytes(SIGHT_WIDTH * SIGHT_HEIGHT % 256)
        written, files = self.pack(sight, b"\x01\x02\x03")
        self.assertEqual(written, [SIGHT_PATH, BLOOD_PATH])
        self.assertIn(sight, files[SIGHT_PATH][OTR_HEADER_SIZE:])
        self.assertTrue(files[BLOOD_PATH].endswith(b"\x01\x02\x03"))

    def test_rejects_a_backdrop_of_the_wrong_size(self):
        with self.assertRaises(EyeIntroError):
            self.pack(b"\x00", b"")


if __name__ == "__main__":
    unittest.main()
