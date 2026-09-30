#!/usr/bin/env python3
"""Packs the sky and water images: the entries of the game's `skywaterimages`
table, which the sky draws its cloud, sea and water planes from.

The table sits in the global image table (pack_hud_images.py describes its
entries). The sky selects an entry with the game's texSelect(), which loads it
as one tile with a half-texel sample offset; each entry is written here as a
display list holding that request, like the gunfire and shadow images
(docs/formats/goldeneye-display-list-resource.md, "Effect images"). Paths:
docs/formats/goldeneye-sky-images.md.
"""

from __future__ import annotations

import struct
from typing import Callable, List

from ge_displaylist import G_ENDDL
from ge_dlemit import (
    DISPLAY_LIST_RESOURCE_TYPE,
    DISPLAY_LIST_RESOURCE_VERSION,
    G_SETTEX,
    OTR_G_SETTIMG_HASH,
    UCODE_INDY_GE,
    TextureBinding,
    _reference_command,
    _request_wrap_mode,
)
from o2r_writer import O2rWriter
from pack_hud_images import ENTRY_SIZE, TABLE_ROM_START

BASE_PATH = "sky/img"

# `s_skywaterimages`'s offset in the global image table, and its length: the
# clouds, the grayscale water and the blue water.
TABLE_OFFSET = 0xFB4
ENTRY_COUNT = 3

_REQUEST_TYPE_MIPMAP = 2
_REQUEST_TYPE_TILE_PRESWAPPED = 4
_REQUEST_SAMPLE_OFFSET = 2


class SkyImagesError(ValueError):
    """Raised when the ROM does not hold the sky image table."""


def image_path(index: int) -> str:
    return f"{BASE_PATH}/{index}"


def read_entries(rom: bytes) -> List[tuple]:
    """The table's entries as (index, texture id, width, height, level, s
    flags, t flags), from a US ROM."""
    end = TABLE_ROM_START + TABLE_OFFSET + ENTRY_COUNT * ENTRY_SIZE
    if len(rom) < end:
        raise SkyImagesError("ROM is too small to hold the global image table")
    entries = []
    for index in range(ENTRY_COUNT):
        texture_id, width, height, level, _fmt, _size, s_flags, t_flags = struct.unpack_from(
            ">IBBBBBBB", rom, TABLE_ROM_START + TABLE_OFFSET + index * ENTRY_SIZE
        )
        if width == 0 or height == 0:
            raise SkyImagesError(f"sky image {index} has no size (US ROM only)")
        entries.append((index, texture_id, width, height, level, s_flags, t_flags))
    return entries


def encode_image_list(texture_id: int, level: int, s_flags: int, t_flags: int, binding: TextureBinding) -> bytes:
    """The list texSelect's single-tile case builds: the request, the texture
    reference and the end of the list. The request has a half-texel sample
    offset, and is a mipmap request when the entry has levels."""
    kind = _REQUEST_TYPE_MIPMAP if level else _REQUEST_TYPE_TILE_PRESWAPPED
    request = (
        (G_SETTEX << 24)
        | (_request_wrap_mode(s_flags) << 22)
        | (_request_wrap_mode(t_flags) << 20)
        | (_REQUEST_SAMPLE_OFFSET << 18)
        | kind
    )
    out = bytearray(struct.pack(">B7x", UCODE_INDY_GE))
    out += struct.pack(">II", request, texture_id)
    out += _reference_command(
        OTR_G_SETTIMG_HASH, (binding.fmt << 21) | (binding.size << 19) | (binding.width - 1), binding.path
    )
    out += struct.pack(">II", G_ENDDL << 24, 0)
    return bytes(out)


def pack_sky_images(rom: bytes, textures: Callable[[int], TextureBinding], writer: O2rWriter) -> List[str]:
    """Writes one list per table entry into `writer`. Returns the resource
    paths."""
    paths = []
    for index, texture_id, _width, _height, level, s_flags, t_flags in read_entries(rom):
        path = image_path(index)
        payload = encode_image_list(texture_id, level, s_flags, t_flags, textures(texture_id))
        writer.add_resource(
            path,
            DISPLAY_LIST_RESOURCE_TYPE,
            version=DISPLAY_LIST_RESOURCE_VERSION,
            resource_id=0,
            payload=payload,
        )
        paths.append(path)
    return paths
