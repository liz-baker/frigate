#!/usr/bin/env python3
"""Packs the images the in-level HUD draws from the game's global image
table: the ammo icons and the crosshair.

The table is initialised data in the ROM: 12-byte entries naming a texture
(by its index in `images.def`), its size and its wrap flags. The HUD selects
an entry with the game's texSelect(), which loads it as one tile; each
entry is written here as a display list holding that request, like the
gunfire and shadow images (docs/formats/goldeneye-display-list-resource.md,
"Effect images"). Paths: docs/formats/goldeneye-hud-images.md.
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

BASE_PATH = "hud/img"
TABLE_PATH = "hud/images"

# The global image table's place in the US ROM.
TABLE_ROM_START = 0x29D160
ENTRY_SIZE = 12

# The entries the HUD selects, by byte offset in the table: the ammo icons
# (`ammo_related`'s IconImage, 0xC84 to 0xD20) and the two crosshairs.
FIRST_OFFSET = 0xC84
END_OFFSET = 0xD44

_REQUEST_TYPE_TILE_PRESWAPPED = 4


class HudImagesError(ValueError):
    """Raised when the ROM does not hold the global image table."""


def image_path(offset: int) -> str:
    return f"{BASE_PATH}/{offset:04x}"


def read_entries(rom: bytes) -> List[tuple]:
    """The HUD's table entries as (offset, texture id, width, height, level,
    s flags, t flags), from a US ROM."""
    end = TABLE_ROM_START + END_OFFSET
    if len(rom) < end:
        raise HudImagesError("ROM is too small to hold the global image table")
    entries = []
    for offset in range(FIRST_OFFSET, END_OFFSET, ENTRY_SIZE):
        texture_id, width, height, level, _fmt, _size, s_flags, t_flags = struct.unpack_from(
            ">IBBBBBBB", rom, TABLE_ROM_START + offset
        )
        if level != 0 or width == 0 or height == 0:
            raise HudImagesError(f"global image table entry {offset:#x} is not a plain image (US ROM only)")
        entries.append((offset, texture_id, width, height, level, s_flags, t_flags))
    return entries


def encode_image_list(texture_id: int, s_flags: int, t_flags: int, binding: TextureBinding) -> bytes:
    """The list texSelect's single-tile case builds: the request, the texture
    reference and the end of the list. The request has no sample offset."""
    request = (
        (G_SETTEX << 24)
        | (_request_wrap_mode(s_flags) << 22)
        | (_request_wrap_mode(t_flags) << 20)
        | _REQUEST_TYPE_TILE_PRESWAPPED
    )
    out = bytearray(struct.pack(">B7x", UCODE_INDY_GE))
    out += struct.pack(">II", request, texture_id)
    out += _reference_command(
        OTR_G_SETTIMG_HASH, (binding.fmt << 21) | (binding.size << 19) | (binding.width - 1), binding.path
    )
    out += struct.pack(">II", G_ENDDL << 24, 0)
    return bytes(out)


def encode_table(entries: List[tuple]) -> bytes:
    """The sizes the HUD lays its images out with, one line per entry:
    `<offset in hex>\t<width>\t<height>`."""
    lines = [f"{offset:#x}\t{width}\t{height}" for offset, _id, width, height, _level, _s, _t in entries]
    return ("\n".join(lines) + "\n").encode("ascii")


def pack_hud_images(rom: bytes, textures: Callable[[int], TextureBinding], writer: O2rWriter) -> List[str]:
    """Writes one list per entry, and the table of their sizes, into `writer`.
    Returns the resource paths."""
    paths = []
    entries = read_entries(rom)
    writer.add_resource(TABLE_PATH, "OBLB", version=0, resource_id=0, payload=encode_table(entries))
    paths.append(TABLE_PATH)
    for offset, texture_id, _width, _height, _level, s_flags, t_flags in entries:
        path = image_path(offset)
        payload = encode_image_list(texture_id, s_flags, t_flags, textures(texture_id))
        writer.add_resource(
            path,
            DISPLAY_LIST_RESOURCE_TYPE,
            version=DISPLAY_LIST_RESOURCE_VERSION,
            resource_id=0,
            payload=payload,
        )
        paths.append(path)
    return paths
