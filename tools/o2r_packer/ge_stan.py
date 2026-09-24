#!/usr/bin/env python3
"""Decodes a GoldenEye stan file (a stage's collision tiles) into the `GEST`
resource payload. Layout of both: docs/formats/goldeneye-stan-resource.md.

The stan file links its tiles by byte offset; the payload links them by tile
index, so the runtime never does pointer arithmetic on the file.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Dict, List, Sequence

# A tile is an 8-byte header and 8 bytes a point.
TILE_HEADER_SIZE = 8
POINT_SIZE = 8
MAX_POINTS = 10

# Offset of the first-tile pointer in the file header.
FIRST_TILE_POINTER_OFFSET = 4
# The game addresses a link relative to the first tile minus this.
LINK_BASE_BIAS = 0x80

NO_LINK = 0xFFFF


class StanError(ValueError):
    """Raised when a stan file does not decode."""


@dataclass
class StanPoint:
    x: int
    y: int
    z: int
    # Index of the tile across this point's edge, or NO_LINK.
    link: int


@dataclass
class StanTile:
    # The 24-bit tile name, and the room it belongs to.
    id: int
    room: int
    # Raw header halves: special/colour nibbles, and point count/triangle indices.
    mid: int
    tail: int
    points: List[StanPoint]


def tile_size(point_count: int) -> int:
    """The bytes a tile with `point_count` (3 to 10) points takes in the file."""
    return TILE_HEADER_SIZE + POINT_SIZE * point_count


def parse_stan(data: bytes) -> List[StanTile]:
    """The file's tiles in order. Links resolve to tile indices; a link the game
    reads as "none" (`link >> 4 == 0`) becomes NO_LINK."""
    if len(data) < FIRST_TILE_POINTER_OFFSET + 4:
        raise StanError("stan file is shorter than its header")
    first = struct.unpack_from(">I", data, FIRST_TILE_POINTER_OFFSET)[0]

    raw: List[tuple] = []
    offsets: Dict[int, int] = {}
    cursor = first
    while True:
        if cursor + 4 > len(data):
            raise StanError(f"tile list runs off the end of the file at 0x{cursor:x}")
        # The list ends at a tile whose id and room are all zero.
        if struct.unpack_from(">I", data, cursor)[0] == 0:
            break
        if cursor + TILE_HEADER_SIZE > len(data):
            raise StanError(f"tile at 0x{cursor:x} is cut off")
        id_room, mid, tail = struct.unpack_from(">IHH", data, cursor)
        count = tail >> 12
        if not 3 <= count <= MAX_POINTS:
            raise StanError(f"tile at 0x{cursor:x} has {count} points")
        size = tile_size(count)
        if cursor + size > len(data):
            raise StanError(f"tile at 0x{cursor:x} is cut off")
        offsets[cursor] = len(raw)
        raw.append((cursor, id_room, mid, tail, count))
        cursor += size

    if len(raw) >= NO_LINK:
        raise StanError(f"{len(raw)} tiles do not fit a 16-bit link")

    base = first - LINK_BASE_BIAS
    tiles: List[StanTile] = []
    for cursor, id_room, mid, tail, count in raw:
        points: List[StanPoint] = []
        for i in range(count):
            x, y, z, link = struct.unpack_from(">hhhH", data, cursor + TILE_HEADER_SIZE + POINT_SIZE * i)
            target = NO_LINK
            if link >> 4 != 0:
                address = base + (link << 3)
                if address not in offsets:
                    raise StanError(f"tile at 0x{cursor:x} links to 0x{address:x}, which is not a tile")
                target = offsets[address]
            points.append(StanPoint(x, y, z, target))
        tiles.append(StanTile(id_room >> 8, id_room & 0xFF, mid, tail, points))
    return tiles


def build_stan_payload(tiles: Sequence[StanTile]) -> bytes:
    """The `GEST` payload: a tile count, then each tile as its 8-byte file
    header (id, room, mid, tail) followed by its points."""
    out = bytearray(struct.pack(">I", len(tiles)))
    for tile in tiles:
        out += struct.pack(">IHH", (tile.id << 8) | tile.room, tile.mid, tile.tail)
        for point in tile.points:
            out += struct.pack(">hhhH", point.x, point.y, point.z, point.link)
    return bytes(out)


def pack_stan_file(data: bytes) -> bytes:
    return build_stan_payload(parse_stan(data))
