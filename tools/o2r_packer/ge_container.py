#!/usr/bin/env python3
"""Unwraps GoldenEye's compressed model files and bg room data into plain
buffers that `ge_displaylist.walk_display_list` can consume (frigate
issue #18, container-unwrap step). On-disk layout:
docs/formats/goldeneye-containers.md.

Both container kinds use the same compression the texture code already
handles (`ge_texture.rzip_inflate`), so nothing here inflates by itself.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass
from typing import List, Optional

from ge_displaylist import Resolve
from ge_texture import GeTextureFormatError, rzip_inflate

MODEL_SEGMENT_BASE = 0x05000000
MODEL_VERTEX_SEGMENT = 0x04
MODEL_MATRIX_SEGMENT = 0x03
BG_HEADER_ROOM_TABLE_WORD = 1
BG_ROOM_ENTRY_SIZE = 24
BG_ROOM_VERTEX_SEGMENT = 0x0E
BG_FILE_SEGMENT = 0x0F
_SEGMENT_MASK = 0xFF000000
_OFFSET_MASK = 0x00FFFFFF


class GeContainerError(ValueError):
    """Raised when a model file or bg file does not match its layout."""


@dataclass(frozen=True)
class ModelImage:
    """A decompressed model file. Its node, display-list and vertex
    pointers are segment-5 addresses over `data`."""

    data: bytes

    def resolve(self, address: int) -> Optional[int]:
        """Segment address to buffer offset; None for null or an address
        outside this file."""
        if address & _SEGMENT_MASK != MODEL_SEGMENT_BASE:
            return None
        offset = address & _OFFSET_MASK
        return offset if offset < len(self.data) else None

    def display_list_resolver(self, vertices_address: int) -> Resolve:
        """Resolver for one display-list node's lists. The game points
        segment 4 at the node's own vertex array before drawing it, so
        `G_VTX` addresses in a node's lists are relative to that array;
        pass the node's vertex pointer to bind it. Segment 3 holds
        matrices the game builds at run time, so its addresses have no
        bytes in the file: they resolve to their bare segment offset."""
        vertices_at = self.resolve(vertices_address)

        def resolve(address: int) -> Optional[int]:
            segment = address >> 24
            offset = address & _OFFSET_MASK
            if segment == MODEL_VERTEX_SEGMENT:
                return None if vertices_at is None else vertices_at + offset
            if segment == MODEL_MATRIX_SEGMENT:
                return offset
            return self.resolve(address)

        return resolve


@dataclass(frozen=True)
class BgRoom:
    """One bg room's decompressed blobs. `vertices` is what its display
    lists' `G_VTX` addresses index into, and is empty for a room with no
    geometry; `secondary` is None when the room has no secondary list."""

    index: int
    vertices: bytes
    primary: bytes
    secondary: Optional[bytes]

    def resolve(self, address: int) -> Optional[int]:
        """Offset into `vertices` for a segment-0x0e address; None for
        anything else or an address past the vertex blob."""
        if address & _SEGMENT_MASK != BG_ROOM_VERTEX_SEGMENT << 24:
            return None
        offset = address & _OFFSET_MASK
        return offset if offset < len(self.vertices) else None


def _inflate(data: bytes, where: str) -> bytes:
    try:
        return rzip_inflate(data)
    except (GeTextureFormatError, zlib.error) as error:
        raise GeContainerError(f"{where}: {error}") from error


def unwrap_model_file(compressed: bytes) -> ModelImage:
    """Decompresses a chr/gun/prop model file."""
    return ModelImage(_inflate(compressed, "model file"))


def _bg_file_offset(pointer: int, file_size: int, what: str) -> Optional[int]:
    if pointer == 0:
        return None
    if pointer & _SEGMENT_MASK != BG_FILE_SEGMENT << 24:
        raise GeContainerError(f"{what}: {pointer:#x} is not a segment-0x0f address")
    offset = pointer & _OFFSET_MASK
    if offset >= file_size:
        raise GeContainerError(f"{what}: {pointer:#x} is past the end of the file")
    return offset


def bg_header_region(data: bytes) -> bytes:
    """The bytes the game reads before any room: everything up to the first
    room's vertex blob, rounded up to 16. It holds the header words, room
    table, portal table and the level's visibility data; its addresses are
    offsets into this same region."""
    table = _bg_file_offset(
        struct.unpack_from(">I", data, 4 * BG_HEADER_ROOM_TABLE_WORD)[0], len(data), "room table"
    ) if len(data) >= 8 else None
    if table is None:
        raise GeContainerError("bg header has no room table")
    first_entry = table + BG_ROOM_ENTRY_SIZE
    if first_entry + 4 > len(data):
        raise GeContainerError("room table runs off the end of the file")
    first_vertices = _bg_file_offset(struct.unpack_from(">I", data, first_entry)[0], len(data), "room 1 vertices")
    if first_vertices is None:
        raise GeContainerError("room 1 has no vertex blob to size the header region by")
    return data[: ((first_vertices - 1) | 0xF) + 1]


def unwrap_bg_file(data: bytes) -> List[BgRoom]:
    """Decompresses every room of a bg file (the whole, uncompressed
    `bg_*_all_p` blob). Returns rooms in index order, skipping the
    placeholder entry 0."""
    if len(data) < 8:
        raise GeContainerError("bg file shorter than its header")
    table = _bg_file_offset(
        struct.unpack_from(">I", data, 4 * BG_HEADER_ROOM_TABLE_WORD)[0], len(data), "room table"
    )
    if table is None:
        raise GeContainerError("bg header has no room table")

    entries = []
    while True:
        entry = table + (len(entries) + 1) * BG_ROOM_ENTRY_SIZE
        if entry + BG_ROOM_ENTRY_SIZE > len(data):
            raise GeContainerError("room table runs off the end of the file")
        pointers = struct.unpack_from(">III", data, entry)
        if pointers[1] == 0:  # the game ends the table on a null primary list
            break
        entries.append(pointers)

    # The last non-null entry is not a room: the game reads its pointers
    # only to size the final room's compressed blobs.
    rooms: List[BgRoom] = []
    for index, (vertices_ptr, primary_ptr, secondary_ptr) in enumerate(entries[:-1], start=1):
        where = f"room {index}"
        vertices_at = _bg_file_offset(vertices_ptr, len(data), f"{where} vertices")
        primary_at = _bg_file_offset(primary_ptr, len(data), f"{where} primary list")
        secondary_at = _bg_file_offset(secondary_ptr, len(data), f"{where} secondary list")
        rooms.append(
            BgRoom(
                index=index,
                vertices=b"" if vertices_at is None else _inflate(data[vertices_at:], f"{where} vertices"),
                primary=_inflate(data[primary_at:], f"{where} primary list"),
                secondary=None if secondary_at is None else _inflate(data[secondary_at:], f"{where} secondary list"),
            )
        )
    return rooms
