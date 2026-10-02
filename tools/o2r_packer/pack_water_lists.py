#!/usr/bin/env python3
"""Packs the two display lists the game calls after a room loads the water
textures (`MipMap2C_Something_Setup` for the Caverns water, image 1508, and
`MipMap2C_Something2_Setup` for the water in Dam and Complex, image 1511).

The game's `texLoadFromGdl` emits a call to the list after the texture's own
setup; the list redeclares tiles 0 and 1 as two views of the same image, and
`sub_GAME_7F092E50` rewrites their offsets and the primitive colour's level of
detail fraction every tick, so the water scrolls and cross-fades between its
layers. The room lists call these (ge_dlemit.water_list_path); the game code
rewrites the words each frame (src/level/Sky.cpp). Layout of the packed lists:
docs/formats/goldeneye-water-lists.md.
"""

from __future__ import annotations

import struct
from typing import List, Tuple

from ge_dlemit import (
    DISPLAY_LIST_RESOURCE_TYPE,
    DISPLAY_LIST_RESOURCE_VERSION,
    UCODE_INDY_GE,
    WATER_TEXTURES,
    water_list_path,
)
from o2r_writer import O2rWriter

# The commands, as the game's F3D-numbered microcode reads them. The game
# rewrites the two tile sizes (commands 2 and 3) and the primitive colour
# (command 4).
# gsDPSetTile(fmt, siz, line, 0, tile, 0, WRAP, 5 or 6, 0, WRAP, 5 or 6, 0).
_SETTILE_CAVERNS = ((0xF5800800, 0x00018060), (0xF5800800, 0x01018060))
_SETTILE_COMPLEX = ((0xF5480400, 0x00014050), (0xF5480400, 0x01014050))
# gsDPSetTileSize(tile, 2, 2, 0, 0) twice, gsDPSetPrimColor(0, 15, 255, 255, 255, 255),
# gsDPSetTextureDetail(G_TD_CLAMP), gsDPSetTextureFilter(G_TF_BILERP),
# gsDPSetCombineLERP(TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0, TEXEL1, TEXEL0,
# PRIM_LOD_FRAC, TEXEL0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0),
# gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_OPA_SURF2), gsDPSetTextureLOD(G_TL_TILE),
# gsDPSetCycleType(G_CYC_2CYCLE), gsSPSetGeometryMode(G_CULL_BACK), gsSPEndDisplayList().
_COMMON = (
    (0xF2002002, 0x00000000),
    (0xF2002002, 0x01000000),
    (0xFA00000F, 0xFFFFFFFF),
    (0xBA001102, 0x00000000),
    (0xBA000C02, 0x00002000),
    (0xFC272C04, 0x1F1093FF),
    (0xB900031D, 0x0C192078),
    (0xBA001001, 0x00000000),
    (0xBA001402, 0x00100000),
    (0xB7000000, 0x00002000),
    (0xB8000000, 0x00000000),
)


class WaterListsError(ValueError):
    """Raised for an image id that has no water list."""


def water_commands(texture_id: int) -> List[Tuple[int, int]]:
    """The list's commands as (w0, w1) pairs."""
    if texture_id == WATER_TEXTURES[0]:
        tiles = _SETTILE_CAVERNS
    elif texture_id == WATER_TEXTURES[1]:
        tiles = _SETTILE_COMPLEX
    else:
        raise WaterListsError(f"image {texture_id} has no water list")
    return list(tiles) + list(_COMMON)


def encode_water_list(texture_id: int) -> bytes:
    out = bytearray(struct.pack(">B7x", UCODE_INDY_GE))
    for w0, w1 in water_commands(texture_id):
        out += struct.pack(">II", w0, w1)
    return bytes(out)


def pack_water_lists(writer: O2rWriter) -> List[str]:
    paths = []
    for texture_id in WATER_TEXTURES:
        path = water_list_path(texture_id)
        writer.add_resource(
            path,
            DISPLAY_LIST_RESOURCE_TYPE,
            version=DISPLAY_LIST_RESOURCE_VERSION,
            resource_id=0,
            payload=encode_water_list(texture_id),
        )
        paths.append(path)
    return paths
