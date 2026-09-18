#!/usr/bin/env python3
"""Vertex extraction for GoldenEye models and bg rooms (frigate issue
#18, vertex step). Decodes the N64 `Vtx` records the display lists load
into blocks with a stable identity, and encodes a block as the payload
of libultraship's `Fast::Vertex` resource (layout:
docs/formats/goldeneye-vertex.md).

Vertex data has no table of its own, so what counts as "used" comes from
the two containers differently: a model node owns the array its data
record points at, while a bg room's blob is bounded only by what its
display lists load.
"""

from __future__ import annotations

import struct
from dataclasses import astuple, dataclass
from typing import List, Tuple, Union

from ge_container import BgRoom, ModelImage
from ge_displaylist import VTX_SIZE, DisplayListRefs, merge_vertex_ranges, walk_display_list
from ge_model import ModelTree

_VTX = struct.Struct(">3hH2h4B")
VERTEX_RESOURCE_TYPE = "OVTX"
VERTEX_RESOURCE_VERSION = 0


class GeVertexError(ValueError):
    """Raised when vertex bytes cannot be extracted as whole records."""


@dataclass(frozen=True)
class Vertex:
    x: int
    y: int
    z: int
    flag: int
    s: int
    t: int
    # Colour (r, g, b) or a signed normal, by the lighting mode of the
    # triangles that use it; the raw bytes are kept undecided.
    r: int
    g: int
    b: int
    a: int


@dataclass(frozen=True)
class ModelVertexKey:
    """Which node's array: the node's offset in the model image."""

    node: int


@dataclass(frozen=True)
class BgVertexKey:
    """Which range of which room: the room index and the range's byte
    offset within that room's vertex blob."""

    room: int
    offset: int


@dataclass(frozen=True)
class VertexBlock:
    key: Union[ModelVertexKey, BgVertexKey]
    vertices: Tuple[Vertex, ...]


def decode_vertices(data: bytes) -> Tuple[Vertex, ...]:
    if len(data) % VTX_SIZE:
        raise GeVertexError(f"{len(data)} bytes is not a whole number of {VTX_SIZE}-byte vertices")
    return tuple(Vertex(*_VTX.unpack_from(data, at)) for at in range(0, len(data), VTX_SIZE))


def encode_vertex_payload(vertices: Tuple[Vertex, ...]) -> bytes:
    """Payload of a `Fast::Vertex` resource, after the 20-byte header:
    a big-endian count, then each vertex."""
    body = b"".join(_VTX.pack(*astuple(v)) for v in vertices)
    return struct.pack(">I", len(vertices)) + body


def _model_extent(image: ModelImage, node) -> int:
    """Byte length of a display-list node's vertex array."""
    record = node.display_list
    if record.vertex_bytes:
        return record.vertex_bytes
    # A count of zero cannot bound the array (one prop node stores 0 yet
    # loads 16 vertices), so use the furthest byte its lists load.
    resolve = image.display_list_resolver(record.vertices_address)
    end = 0
    for start in (record.primary, record.secondary):
        if start is not None:
            for offset, length in merge_vertex_ranges(walk_display_list(image.data, start, resolve)):
                end = max(end, offset + length - record.vertices)
    return end


def extract_model_vertices(image: ModelImage, tree: ModelTree) -> List[VertexBlock]:
    """One block per display-list node that has an array, in node-offset
    order. Nodes that share an array each get their own block, since
    their lists address it relative to the node."""
    blocks = []
    for offset in sorted(tree.nodes):
        node = tree.nodes[offset]
        record = node.display_list
        if record is None or record.vertices is None:
            continue
        length = _model_extent(image, node)
        if record.vertices + length > len(image.data):
            raise GeVertexError(f"node at {offset:#x}: vertex array runs off the image")
        raw = image.data[record.vertices : record.vertices + length]
        blocks.append(VertexBlock(ModelVertexKey(offset), decode_vertices(raw)))
    return blocks


def extract_bg_vertices(room: BgRoom) -> List[VertexBlock]:
    """One block per disjoint range of the room's vertex blob that its
    primary or secondary list loads."""
    combined = DisplayListRefs()
    for blob in (room.primary, room.secondary):
        if blob is None:
            continue
        refs = walk_display_list(blob, 0, room.resolve)
        if refs.unresolved:
            raise GeVertexError(f"room {room.index}: list has unresolved references")
        combined.vertices.extend(refs.vertices)

    blocks = []
    for begin, length in merge_vertex_ranges(combined):
        end = begin + length
        if end > len(room.vertices):
            raise GeVertexError(f"room {room.index}: range at {begin:#x} runs off the vertex blob")
        blocks.append(VertexBlock(BgVertexKey(room.index, begin), decode_vertices(room.vertices[begin:end])))
    return blocks
