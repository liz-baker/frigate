#!/usr/bin/env python3
"""Packs the two pieces of data the front-end's gun barrel screen loads from
outside any model or list: the sniper-sight backdrop image and the blood
animation stream.

Neither is a file-list entry. The backdrop is a fixed ROM region, RLE
compressed; the blood stream is initialised data in the game's own code
segment, which the ROM stores compressed. Both ranges are fixed here for the
US ROM and checked against the bytes found there. Resource paths and
layouts: docs/formats/goldeneye-eye-intro-resources.md.
"""

from __future__ import annotations

import struct
import zlib
from typing import List

from ge_dlemit import EmbeddedImage, encode_embedded_image_payload
from o2r_writer import O2rWriter

BASE_PATH = "eyeintro"
SIGHT_PATH = f"{BASE_PATH}/sight"
BLOOD_PATH = f"{BASE_PATH}/blood"

# US ROM range of the RLE-compressed backdrop (the file the decomp's
# scripts/filelist.u.csv names ge007.u.2A4D50.usedby7F008DE4.bin).
SIGHT_ROM_START = 0x2A4D50
SIGHT_ROM_SIZE = 107904
SIGHT_WIDTH = 440
SIGHT_HEIGHT = 299
_RLE_HEADER_SIZE = 10

# The game code segment's compressed blob in the US ROM (two-byte 0x1172
# marker, then a raw DEFLATE stream; docs/formats/goldeneye-containers.md),
# and the blood animation stream's place and length in its inflated bytes.
GAME_SEGMENT_ROM_START = 0x21990
BLOOD_OFFSET = 0xADA0
BLOOD_SIZE = 2524
_BLOOD_SIGNATURE = bytes.fromhex("002003050402602122 01e0e0".replace(" ", ""))

_N64_FORMAT_I = 4
_N64_SIZE_8B = 1


class EyeIntroError(ValueError):
    """Raised when the ROM does not hold the gun barrel screen's data."""


def rle_expand_8bit(data: bytes) -> bytes:
    """Expands the backdrop's run-length stream: a big-endian u16 width and
    height, six more header bytes, then (count, value) byte pairs until
    width * height pixels are out."""
    if len(data) < _RLE_HEADER_SIZE:
        raise EyeIntroError("backdrop is too short to hold its header")
    width, height = struct.unpack(">HH", data[:4])
    remaining = width * height
    out = bytearray()
    position = _RLE_HEADER_SIZE
    while remaining > 0:
        if position + 2 > len(data):
            raise EyeIntroError("backdrop's run-length stream ends early")
        count, value = data[position], data[position + 1]
        position += 2
        remaining -= count
        out += bytes([value]) * count
    return bytes(out[: width * height])


def read_sight(rom: bytes) -> bytes:
    """The backdrop's 440x299 8-bit intensity pixels from a US ROM."""
    end = SIGHT_ROM_START + SIGHT_ROM_SIZE
    if len(rom) < end:
        raise EyeIntroError("ROM is too small to hold the gun barrel backdrop")
    data = rom[SIGHT_ROM_START:end]
    if struct.unpack(">HH", data[:4]) != (SIGHT_WIDTH, SIGHT_HEIGHT):
        raise EyeIntroError(f"no gun barrel backdrop at ROM offset {SIGHT_ROM_START:#x} (US ROM only)")
    return rle_expand_8bit(data)


def read_blood_stream(rom: bytes) -> bytes:
    """The blood animation stream from a US ROM's game code segment."""
    marker = rom[GAME_SEGMENT_ROM_START : GAME_SEGMENT_ROM_START + 2]
    if marker != b"\x11\x72":
        raise EyeIntroError(f"no game code segment at ROM offset {GAME_SEGMENT_ROM_START:#x} (US ROM only)")
    try:
        segment = zlib.decompressobj(-15).decompress(rom[GAME_SEGMENT_ROM_START + 2 :])
    except zlib.error as error:
        raise EyeIntroError(f"the game code segment does not inflate: {error}") from error
    stream = segment[BLOOD_OFFSET : BLOOD_OFFSET + BLOOD_SIZE]
    if len(stream) != BLOOD_SIZE or not stream.startswith(_BLOOD_SIGNATURE):
        raise EyeIntroError("no blood animation stream in the game code segment (US ROM only)")
    return stream


def pack_eyeintro(sight: bytes, blood: bytes, writer: O2rWriter) -> List[str]:
    """Writes the backdrop and the blood stream into `writer`. Returns the
    resource paths written."""
    if len(sight) != SIGHT_WIDTH * SIGHT_HEIGHT:
        raise EyeIntroError(f"the backdrop must be {SIGHT_WIDTH}x{SIGHT_HEIGHT} pixels")
    image = EmbeddedImage(SIGHT_PATH, 0, _N64_FORMAT_I, _N64_SIZE_8B, SIGHT_WIDTH, len(sight), sight)
    writer.add_resource(SIGHT_PATH, "OTEX", version=1, resource_id=0, payload=encode_embedded_image_payload(image))
    writer.add_resource(BLOOD_PATH, "OBLB", version=0, resource_id=0, payload=blood)
    return [SIGHT_PATH, BLOOD_PATH]
