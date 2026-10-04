#!/usr/bin/env python3
"""Packs the images the in-level HUD and the file select screen draw from the
game's global image table: the ammo icons, the crosshair and the folder
screen's icons.

The table is initialised data in the ROM: 12-byte entries naming a texture
(by its index in `images.def`), its size and its wrap flags. The HUD selects
an entry with the game's texSelect(), which loads it as one tile; each
entry is written here as a display list holding that request, like the
gunfire and shadow images (docs/formats/goldeneye-display-list-resource.md,
"Effect images"). The effects' display lists that select a single image (the
smoke and the debris) are written too. Paths: docs/formats/goldeneye-hud-images.md.
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
    _REQUEST_SAMPLE_OFFSET,
    _reference_command,
    _request_wrap_mode,
)
from o2r_writer import O2rWriter

BASE_PATH = "hud/img"
EFFECT_PATH = "hud/fx"
GLOBAL_LIST_PATH = "hud/gdl"
TABLE_PATH = "hud/images"

# The global image table's place in the US ROM.
TABLE_ROM_START = 0x29D160
ENTRY_SIZE = 12

# The entries the HUD selects, by byte offset in the table: the ammo icons
# (`ammo_related`'s IconImage, 0xC84 to 0xD20) and the two crosshairs.
FIRST_OFFSET = 0xC84
END_OFFSET = 0xD44

# The file select screen's images (`s_mainfolderimages`, 0xFD8 to 0x1020): the
# copy and erase icons, the "select file" title, the cross, the tick and the
# dot. Plain images, selected like the HUD's.
FOLDER_FIRST = 0xFD8
FOLDER_COUNT = 6
FOLDER_END = FOLDER_FIRST + FOLDER_COUNT * ENTRY_SIZE

# The entries the in-level effects select: the flare image, the bullet impact
# images, the smoke and debris frames and the flares (0xAC8 to 0xC84, from
# `s_genericimage`), the glass overlay (0xD44 to 0xD5C) and the monitors' screens
# (`s_monitorimages`, 0xD5C to 0xFC0). Unlike the HUD's they may be mipmapped;
# only the base level is packed, so they draw as one tile.
MONITOR_FIRST = 0xD5C
MONITOR_COUNT = 51
EFFECT_RANGES = ((0xAC8, 0xC84), (0xD44, 0xD5C), (MONITOR_FIRST, MONITOR_FIRST + MONITOR_COUNT * 12))
TABLE_END = max(MONITOR_FIRST + MONITOR_COUNT * 12, FOLDER_END)

_REQUEST_TYPE_TILE_PRESWAPPED = 4
# The image is loaded at the TMEM word address in bits 3 to 11 and sets tile 1,
# whose s and t coordinate shifts are the 3-bit fields at bits 12 and 15
# (src/texture/TexExpand.cpp, docs/formats/goldeneye-display-list-resource.md).
_REQUEST_TYPE_SECOND_TILE = 5
_REQUEST_TMEM_SHIFT = 3
_REQUEST_SHIFT_S = 12
_REQUEST_SHIFT_T = 15

# The display lists at the start of the table that select one image: the smoke
# an explosion leaves (0x000) and the debris's static noise (0xA50).
SINGLE_IMAGE_LISTS = (0x000, 0xA50)
# The 15 lists (0x078 to 0x9A8, 0xA8 apart) that blend a smoke and a fire image
# for the frames of an explosion's flare.
FLARE_LISTS = tuple(0x078 + (0xA8 * frame) for frame in range(15))
GLOBAL_LISTS = SINGLE_IMAGE_LISTS + FLARE_LISTS

_G_ENDDL = 0xB8
_G_SETTIMG = 0xFD
_G_SETTILESIZE = 0xF2
_G_SETTILE = 0xF5
_IMAGE_MARKER = 0xABCD


class HudImagesError(ValueError):
    """Raised when the ROM does not hold the global image table."""


def is_effect(offset: int) -> bool:
    """Whether the entry is one of the effects' (selected with a half-texel
    sample offset) and not the HUD's."""
    return not (FIRST_OFFSET <= offset < END_OFFSET or FOLDER_FIRST <= offset < FOLDER_END)


def image_path(offset: int) -> str:
    return f"{EFFECT_PATH if is_effect(offset) else BASE_PATH}/{offset:04x}"


def global_list_path(offset: int) -> str:
    return f"{GLOBAL_LIST_PATH}/{offset:04x}"


def read_entries(rom: bytes) -> List[tuple]:
    """The table entries the HUD and the effects select, as (offset, texture
    id, width, height, level, s flags, t flags, format), from a US ROM."""
    end = TABLE_ROM_START + TABLE_END
    if len(rom) < end:
        raise HudImagesError("ROM is too small to hold the global image table")
    entries = []
    ranges = [(FIRST_OFFSET, END_OFFSET, False), (FOLDER_FIRST, FOLDER_END, False)] + [(first, last, True) for first, last in EFFECT_RANGES]
    for first, last, mipmapped_ok in sorted(ranges):
        for offset in range(first, last, ENTRY_SIZE):
            texture_id, width, height, level, fmt, _size, s_flags, t_flags = struct.unpack_from(
                ">IBBBBBBB", rom, TABLE_ROM_START + offset
            )
            if (level != 0 and not mipmapped_ok) or width == 0 or height == 0:
                raise HudImagesError(f"global image table entry {offset:#x} is not a plain image (US ROM only)")
            entries.append((offset, texture_id, width, height, level, s_flags, t_flags, fmt))
    return entries


def encode_image_list(
    texture_id: int, s_flags: int, t_flags: int, binding: TextureBinding, sample_offset: bool = False
) -> bytes:
    """The list texSelect's single-tile case builds: the request, the texture
    reference and the end of the list. The HUD's requests have no sample
    offset; the effects' have the half texel."""
    request = (
        (G_SETTEX << 24)
        | (_request_wrap_mode(s_flags) << 22)
        | (_request_wrap_mode(t_flags) << 20)
        | ((_REQUEST_SAMPLE_OFFSET << 18) if sample_offset else 0)
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
    `<offset in hex>\t<width>\t<height>\t<format>`."""
    lines = [f"{offset:#x}\t{width}\t{height}\t{fmt}" for offset, _id, width, height, _level, _s, _t, fmt in entries]
    return ("\n".join(lines) + "\n").encode("ascii")


def _second_image_request(rom: bytes, at: int) -> int:
    """The request for the second image of a global list, from the commands that
    follow its load: the render tile (the one that is not tile 7) gives the
    TMEM address, the wrap modes and the coordinate shifts."""
    while True:
        w0, w1 = struct.unpack_from(">II", rom, at)
        at += 8
        opcode = w0 >> 24
        if opcode == _G_SETTILE and (w1 >> 24) & 7 != 7:
            tmem = w0 & 0x1FF
            cmt, cms = (w1 >> 18) & 3, (w1 >> 8) & 3
            shift_t, shift_s = (w1 >> 10) & 0xF, w1 & 0xF
            if shift_t > 7 or shift_s > 7:
                raise HudImagesError("a global display list shifts a second image's coordinates by more than 7")
            return (
                (G_SETTEX << 24)
                | (_request_wrap_mode(cms) << 22)
                | (_request_wrap_mode(cmt) << 20)
                | (shift_t << _REQUEST_SHIFT_T)
                | (shift_s << _REQUEST_SHIFT_S)
                | (tmem << _REQUEST_TMEM_SHIFT)
                | _REQUEST_TYPE_SECOND_TILE
            )
        if opcode in (_G_SETTILESIZE, _G_ENDDL):
            raise HudImagesError("a global display list loads a second image into no render tile")


def encode_global_list(rom: bytes, offset: int, textures: Callable[[int], TextureBinding]) -> bytes:
    """A display list of the table that selects one image, or a smoke and a fire
    image: its commands as the ROM has them, each image's load (the texture, the
    tile that loads it and the one that draws it) replaced by the request texSelect
    would make. The first image is tile 0's; a second is tile 1's."""
    at = TABLE_ROM_START + offset
    out = bytearray(struct.pack(">B7x", UCODE_INDY_GE))
    images = 0
    skipping = False
    while True:
        w0, w1 = struct.unpack_from(">II", rom, at)
        at += 8
        opcode = w0 >> 24
        if opcode == _G_SETTIMG and (w1 >> 16) == _IMAGE_MARKER and not skipping:
            texture_id = w1 & 0xFFFF
            binding = textures(texture_id)
            if images == 0:
                request = (G_SETTEX << 24) | _REQUEST_TYPE_TILE_PRESWAPPED
            elif images == 1:
                request = _second_image_request(rom, at)
            else:
                raise HudImagesError(f"global display list {offset:#x} loads more than two images")
            out += struct.pack(">II", request, texture_id)
            out += _reference_command(
                OTR_G_SETTIMG_HASH,
                (binding.fmt << 21) | (binding.size << 19) | (binding.width - 1),
                binding.path,
            )
            images += 1
            skipping = True
            continue
        if skipping:
            # The load's commands, up to and including the render tile's size.
            if opcode == _G_SETTILESIZE:
                skipping = False
            continue
        out += struct.pack(">II", w0, w1)
        if opcode == _G_ENDDL:
            break
    if images == 0:
        raise HudImagesError(f"global display list {offset:#x} loads no image")
    return bytes(out)


def pack_hud_images(rom: bytes, textures: Callable[[int], TextureBinding], writer: O2rWriter) -> List[str]:
    """Writes one list per entry, and the table of their sizes, into `writer`,
    and the table's display lists that select an effect's images. Returns the resource paths."""
    paths = []
    entries = read_entries(rom)
    writer.add_resource(TABLE_PATH, "OBLB", version=0, resource_id=0, payload=encode_table(entries))
    paths.append(TABLE_PATH)
    for offset, texture_id, _width, _height, _level, s_flags, t_flags, _fmt in entries:
        path = image_path(offset)
        payload = encode_image_list(texture_id, s_flags, t_flags, textures(texture_id), is_effect(offset))
        writer.add_resource(
            path,
            DISPLAY_LIST_RESOURCE_TYPE,
            version=DISPLAY_LIST_RESOURCE_VERSION,
            resource_id=0,
            payload=payload,
        )
        paths.append(path)
    for offset in GLOBAL_LISTS:
        path = global_list_path(offset)
        writer.add_resource(
            path,
            DISPLAY_LIST_RESOURCE_TYPE,
            version=DISPLAY_LIST_RESOURCE_VERSION,
            resource_id=0,
            payload=encode_global_list(rom, offset, textures),
        )
        paths.append(path)
    return paths
