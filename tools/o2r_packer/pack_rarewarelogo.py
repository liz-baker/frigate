#!/usr/bin/env python3
"""Packs the Rareware logo segment: the spinning-logo display lists,
vertex arrays and textures the front-end's Rareware screen draws.

Unlike models and bg rooms, the segment is not a file-list entry: the
decomp links it as its own ROM segment (`rarewarelogo`, mapped at segment
address 0x02000000), so its ROM range is fixed here and checked against the
bytes found there rather than read from `scripts/filelist.u.csv`. It is
plain display-list data with no container. Resource paths:
docs/formats/goldeneye-display-list-resource.md.
"""

from __future__ import annotations

from typing import List, Optional

from ge_displaylist import merge_vertex_ranges, walk_display_list
from ge_dlemit import (
    DISPLAY_LIST_RESOURCE_TYPE,
    DISPLAY_LIST_RESOURCE_VERSION,
    EmbeddedImage,
    GeDisplayListEmitError,
    TextureBinding,
    encode_embedded_image_payload,
    reemit_segment_lists,
)
from ge_vertex import VERTEX_RESOURCE_TYPE, VERTEX_RESOURCE_VERSION, decode_vertices, encode_vertex_payload
from o2r_writer import O2rWriter

BASE_PATH = "rarewarelogo"

# US ROM range of the segment; it ends where the next segment (`romfiles2`)
# begins.
SEGMENT_ROM_START = 0x29E560
SEGMENT_ROM_END = 0x2A4D50
SEGMENT_ADDRESS = 0x02000000

# The three lists the screen draws (the game's D_020043E8, DL_RAREWARETEXT
# and D_02004758), as offsets into the segment.
LIST_OFFSETS = (0x43E8, 0x44B0, 0x4758)

# 32x32 RGBA16 images the screen loads from its own code rather than from any
# list, as offsets into the segment.
CODE_LOADED_IMAGE_OFFSETS = (0x4FE8, 0x5FF0)
_CODE_LOADED_IMAGE_WIDTH = 32
_CODE_LOADED_IMAGE_TEXELS = 32 * 32

_N64_FORMAT_RGBA = 0
_N64_SIZE_16B = 2

# The segment opens with two light records (ambient, directional colour) and
# an empty list; a mismatch means the ROM is not the US release.
_SIGNATURE = bytes.fromhex("dcdcdc00dcdcdc00ffffff00ffffff00007f000000000000b800000000000000")


class RarewareLogoError(ValueError):
    """Raised when the ROM does not hold the Rareware logo segment."""


def read_segment(rom: bytes) -> bytes:
    """The segment's bytes from a US ROM, after checking they are it."""
    if len(rom) < SEGMENT_ROM_END:
        raise RarewareLogoError("ROM is too small to hold the Rareware logo segment")
    segment = rom[SEGMENT_ROM_START:SEGMENT_ROM_END]
    if not segment.startswith(_SIGNATURE):
        raise RarewareLogoError(f"no Rareware logo segment at ROM offset {SEGMENT_ROM_START:#x} (US ROM only)")
    return segment


def _resolve(segment: bytes):
    def resolve(address: int) -> Optional[int]:
        if address >> 24 != SEGMENT_ADDRESS >> 24:
            return None
        offset = address & 0x00FFFFFF
        return offset if offset < len(segment) else None

    return resolve


def _no_texture_ids(texture_id: int) -> TextureBinding:
    raise GeDisplayListEmitError(f"G_SETTEX {texture_id:#x}: the segment's lists load images by address")


def pack_rarewarelogo(segment: bytes, writer: O2rWriter) -> List[str]:
    """Writes the segment's lists, vertex arrays and images into `writer`.
    Returns the resource paths written."""
    resolve = _resolve(segment)
    refs = walk_display_list(segment, LIST_OFFSETS[0], resolve)
    for start in LIST_OFFSETS[1:]:
        more = walk_display_list(segment, start, resolve)
        refs.vertices.extend(more.vertices)
        refs.textures.extend(more.textures)
    if refs.unresolved:
        raise GeDisplayListEmitError("rarewarelogo: a list has unresolved references")

    vertex_ranges = merge_vertex_ranges(refs)
    emitted = reemit_segment_lists(BASE_PATH, segment, list(LIST_OFFSETS), resolve, vertex_ranges, _no_texture_ids)

    written: List[str] = []

    def write(path: str, resource_type: str, version: int, payload: bytes) -> None:
        writer.add_resource(path, resource_type, version=version, resource_id=0, payload=payload)
        written.append(path)

    for begin, length in vertex_ranges:
        vertices = decode_vertices(segment[begin : begin + length])
        write(f"{BASE_PATH}/vtx/{begin:06x}", VERTEX_RESOURCE_TYPE, VERTEX_RESOURCE_VERSION, encode_vertex_payload(vertices))
    for item in emitted.lists.values():
        write(item.path, DISPLAY_LIST_RESOURCE_TYPE, DISPLAY_LIST_RESOURCE_VERSION, item.payload)

    images = dict(emitted.images)
    for offset in CODE_LOADED_IMAGE_OFFSETS:
        path = f"{BASE_PATH}/img/{offset:06x}"
        length = _CODE_LOADED_IMAGE_TEXELS * 2
        images[path] = EmbeddedImage(
            path,
            offset,
            _N64_FORMAT_RGBA,
            _N64_SIZE_16B,
            _CODE_LOADED_IMAGE_WIDTH,
            _CODE_LOADED_IMAGE_TEXELS,
            bytes(segment[offset : offset + length]),
        )
    for image in images.values():
        write(image.path, "OTEX", 1, encode_embedded_image_payload(image))
    return written
