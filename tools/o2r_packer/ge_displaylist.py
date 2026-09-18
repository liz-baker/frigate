#!/usr/bin/env python3
"""Read-only walker over GoldenEye's native display-list stream. Part of
frigate issue #18 (step 1 of the revised plan: find what a display list
references before converting or extracting anything).

GoldenEye's RSP microcode uses the original F3D opcode numbering plus a
few Rare extensions (`G_TRI4`, `G_SETTEX`); opcode and word layouts are
in docs/formats/goldeneye-display-list.md. Every command is one 8-byte
word pair, so the stream can be walked without decoding triangles.

The walker only reports references (vertex arrays, textures, matrices,
child lists). It emits no resources: vertex extraction needs these
references to know where each array starts and how long it is, since
vertex data has no table of its own in any GoldenEye container.

Callers supply the raw bytes of one container plus a `resolve` function
mapping a segment address to an offset in that buffer. Unwrapping each
container (bg rooms, model files) into such a buffer is not done here.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Set, Tuple

G_MTX = 0x01
G_MOVEMEM = 0x03
G_VTX = 0x04
G_DL = 0x06
G_TRI4 = 0xB1
G_ENDDL = 0xB8
G_SETTIMG = 0xFD

G_DL_NOPUSH = 0x01  # in a G_DL's flag byte: jump instead of call

VTX_SIZE = 16
GFX_SIZE = 8

Resolve = Callable[[int], Optional[int]]


@dataclass(frozen=True)
class VertexRef:
    cmd_offset: int
    offset: int  # resolved offset of the first vertex in the buffer
    count: int
    dest_index: int  # first vertex-buffer slot the load fills


@dataclass(frozen=True)
class TextureRef:
    cmd_offset: int
    offset: int
    fmt: int
    size: int  # N64 pixel size code (0=4b, 1=8b, 2=16b, 3=32b)


@dataclass(frozen=True)
class AddrRef:
    cmd_offset: int
    offset: int


@dataclass
class DisplayListRefs:
    vertices: List[VertexRef] = field(default_factory=list)
    textures: List[TextureRef] = field(default_factory=list)
    matrices: List[AddrRef] = field(default_factory=list)
    movemem: List[AddrRef] = field(default_factory=list)
    child_lists: List[AddrRef] = field(default_factory=list)
    visited_lists: Set[int] = field(default_factory=set)
    # cmd_offset of anything the walker could not follow: an address that
    # does not resolve, a list that runs off the buffer, or a command cap hit.
    unresolved: List[int] = field(default_factory=list)
    command_count: int = 0
    opcode_counts: Dict[int, int] = field(default_factory=dict)


def _walk_one(data: bytes, start: int, resolve: Resolve, refs: DisplayListRefs, pending: List[int], max_commands: int) -> None:
    offset = start
    while True:
        if offset < 0 or offset + GFX_SIZE > len(data):
            refs.unresolved.append(offset)
            return
        if refs.command_count >= max_commands:
            refs.unresolved.append(offset)
            return

        w0, w1 = struct.unpack_from(">II", data, offset)
        opcode = w0 >> 24
        refs.command_count += 1
        refs.opcode_counts[opcode] = refs.opcode_counts.get(opcode, 0) + 1

        if opcode == G_ENDDL:
            return

        if opcode == G_VTX:
            target = resolve(w1)
            if target is None:
                refs.unresolved.append(offset)
            else:
                refs.vertices.append(VertexRef(offset, target, (w0 & 0xFFFF) // VTX_SIZE, (w0 >> 16) & 0xF))
        elif opcode == G_SETTIMG:
            target = resolve(w1)
            if target is None:
                refs.unresolved.append(offset)
            else:
                refs.textures.append(TextureRef(offset, target, (w0 >> 21) & 0x7, (w0 >> 19) & 0x3))
        elif opcode in (G_MTX, G_MOVEMEM):
            target = resolve(w1)
            if target is None:
                refs.unresolved.append(offset)
            else:
                (refs.matrices if opcode == G_MTX else refs.movemem).append(AddrRef(offset, target))
        elif opcode == G_DL:
            target = resolve(w1)
            if target is None:
                refs.unresolved.append(offset)
            else:
                refs.child_lists.append(AddrRef(offset, target))
                pending.append(target)
            if (w0 >> 16) & 0xFF == G_DL_NOPUSH:
                return  # a jump never returns to this list

        offset += GFX_SIZE


def walk_display_list(
    data: bytes,
    start: int,
    resolve: Resolve,
    max_commands: int = 1_000_000,
) -> DisplayListRefs:
    """Walks the display list at `start`, and every list it calls or
    jumps to, collecting each list once. `max_commands` bounds the walk
    over corrupt data that never reaches a terminator."""
    refs = DisplayListRefs()
    pending = [start]
    while pending:
        current = pending.pop()
        if current in refs.visited_lists:
            continue
        refs.visited_lists.add(current)
        _walk_one(data, current, resolve, refs, pending, max_commands)
    return refs


def merge_vertex_ranges(refs: DisplayListRefs) -> List[Tuple[int, int]]:
    """Collapses vertex loads into disjoint (offset, byte_length) ranges,
    merging overlapping or touching loads. Lists commonly load
    overlapping windows of one array."""
    spans = sorted((v.offset, v.offset + v.count * VTX_SIZE) for v in refs.vertices)
    merged: List[List[int]] = []
    for begin, end in spans:
        if merged and begin <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([begin, end])
    return [(begin, end - begin) for begin, end in merged]
