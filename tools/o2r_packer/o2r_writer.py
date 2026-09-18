#!/usr/bin/env python3
"""O2R archive writer -- the generic, game-agnostic foundation for frigate
issue #10 (the offline O2R packer for GoldenEye's assets).

An O2R archive is just a zip file (see `libultraship-wiiu`'s
`O2rArchive.cpp`: it opens the file with `zip_open()` and indexes every
non-directory entry by its path -- there is no archive-level manifest).
What *is* structured is each individual resource's payload: every binary
resource inside the zip is prefixed with a small fixed header (20 bytes)
that `ResourceLoader::ReadResourceInitDataBinary()` reads before handing
the rest of the buffer to the matching `ResourceFactory`. This module
writes archives and resource entries that match that header exactly, so
anything built on top of it (the per-format packers in #10, e.g. for
`chr`/`gun`/`prop`/`bg`/textures) only has to produce raw payload bytes
and a `ResourceType`, not worry about the container format.

Header layout, transcribed field-for-field from
`libultraship-wiiu/src/ship/resource/ResourceLoader.cpp`
(`ReadResourceInitDataBinary`) -- this is *this repo's own dependency's*
documented format, not third-party code, so nothing here needs a
THIRD_PARTY_LICENSES entry:

    offset  size  field
    0       1     byte order (1 = big-endian, 0 = little-endian --
                   see Endianness below)
    1       1     is-custom flag (0/1; alt-asset override marker)
    2       2     reserved (unused, written as zero)
    4       4     resource type (big-endian uint32; a 4-char ASCII code,
                   e.g. b"OBLB" for Blob -- see ResourceType.h)
    8       4     resource version (big-endian uint32)
    12      8     resource ID (big-endian uint64; content hash / unique ID)
    20            payload begins here (OTR_HEADER_SIZE)

Multi-byte header fields are themselves written in the byte order the
first header byte declares, mirroring `BinaryReader::SetEndianness()`
being applied before the rest of the header is read.

Blob payloads (`ResourceType.Blob` / the `"OBLB"` fourcc) have one more
layer on top of the header, transcribed from
`ResourceFactoryBinaryBlobV0::ReadResource()`
(`libultraship-wiiu/src/ship/resource/factory/BlobFactory.cpp`): the bytes
right after the 20-byte header are a `uint32` `dataSize` (same byte order
as the header), followed by exactly that many raw bytes. The factory reads
`dataSize`, then that many bytes, then separately zero-pads its in-memory
copy by 16 bytes for N64 code that overreads slightly -- that padding is a
runtime detail, not part of the archived payload. `add_resource()` adds
this `uint32` prefix automatically whenever `resource_type` is Blob/`OBLB`,
so callers only ever need to pass raw payload bytes.
"""

from __future__ import annotations

import struct
import zipfile
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from typing import Union

OTR_HEADER_SIZE = 20

# ResourceType.Blob's fourcc ("OBLB"), packed the same way resource_type_code()
# packs any other 4-char resource type tag. Blob payloads carry an extra
# length prefix (see module docstring); add_resource() checks against this
# to know when to add it.
_BLOB_TYPE_CODE = struct.unpack(">I", b"OBLB")[0]


class Endianness(IntEnum):
    """Matches libultraship-wiiu's `Endianness` enum (see BinaryReader.h)."""

    LITTLE = 0
    BIG = 1


def resource_type_code(fourcc: Union[str, bytes]) -> int:
    """Packs a 4-character ASCII tag into the big-endian uint32 ResourceType
    value libultraship-wiiu expects (see ResourceType.h: each built-in type's
    underlying value is its 4-char code read as a big-endian uint32, e.g.
    Blob == 'OBLB' == 0x4F424C42)."""
    data = fourcc.encode("ascii") if isinstance(fourcc, str) else fourcc
    if len(data) != 4:
        raise ValueError(f"resource type code must be exactly 4 characters, got {data!r}")
    return struct.unpack(">I", data)[0]


def pack_resource_header(
    resource_type: Union[str, bytes, int],
    version: int,
    resource_id: int,
    *,
    byte_order: Endianness = Endianness.BIG,
    is_custom: bool = False,
) -> bytes:
    """Builds the 20-byte OTR-style header ResourceLoader expects at the
    start of every binary resource's bytes inside an O2R archive."""
    type_value = resource_type_code(resource_type) if not isinstance(resource_type, int) else resource_type

    endian_fmt = ">" if byte_order == Endianness.BIG else "<"
    header = bytearray()
    header += struct.pack("B", int(byte_order))
    header += struct.pack("B", 1 if is_custom else 0)
    header += b"\x00\x00"  # reserved
    header += struct.pack(f"{endian_fmt}I", type_value & 0xFFFFFFFF)
    header += struct.pack(f"{endian_fmt}I", version & 0xFFFFFFFF)
    header += struct.pack(f"{endian_fmt}Q", resource_id & 0xFFFFFFFFFFFFFFFF)
    assert len(header) == OTR_HEADER_SIZE
    return bytes(header)


@dataclass(frozen=True)
class ParsedResourceHeader:
    byte_order: Endianness
    is_custom: bool
    resource_type: int
    version: int
    resource_id: int


def unpack_resource_header(data: bytes) -> ParsedResourceHeader:
    """Inverse of pack_resource_header(); mirrors
    ResourceLoader::ReadResourceInitDataBinary()'s read order exactly, for
    self-checking what this writer produces."""
    if len(data) < OTR_HEADER_SIZE:
        raise ValueError(f"resource header too short: got {len(data)} bytes, need {OTR_HEADER_SIZE}")

    byte_order = Endianness(data[0])
    is_custom = bool(data[1])
    # data[2:4] reserved, ignored
    endian_fmt = ">" if byte_order == Endianness.BIG else "<"
    resource_type = struct.unpack(f"{endian_fmt}I", data[4:8])[0]
    version = struct.unpack(f"{endian_fmt}I", data[8:12])[0]
    resource_id = struct.unpack(f"{endian_fmt}Q", data[12:20])[0]

    return ParsedResourceHeader(
        byte_order=byte_order,
        is_custom=is_custom,
        resource_type=resource_type,
        version=version,
        resource_id=resource_id,
    )


def unpack_blob_payload(data_after_header: bytes, byte_order: Endianness = Endianness.BIG) -> bytes:
    """Inverse of the Blob-specific prefix `O2rWriter.add_resource()` adds:
    given the bytes right after a Blob resource's 20-byte header, reads
    the `uint32 dataSize` and returns exactly that many following bytes
    -- the original payload passed to `add_resource()`. For
    self-checking / test use."""
    endian_fmt = ">" if byte_order == Endianness.BIG else "<"
    (data_size,) = struct.unpack_from(f"{endian_fmt}I", data_after_header, 0)
    return data_after_header[4 : 4 + data_size]


class O2rWriter:
    """Builds a `.o2r` archive (a plain zip -- see module docstring) by
    adding resources at their virtual paths, each with a header
    ResourceLoader can parse.

    Usage:
        with O2rWriter("goldeneye.o2r") as writer:
            writer.add_resource("textures/ui/font.tex", "TXTR", version=1,
                                 resource_id=..., payload=raw_texture_bytes)
    """

    def __init__(self, archive_path: Union[str, Path]):
        self._archive_path = Path(archive_path)
        self._zip: zipfile.ZipFile | None = None

    def __enter__(self) -> "O2rWriter":
        self._zip = zipfile.ZipFile(self._archive_path, mode="w", compression=zipfile.ZIP_DEFLATED)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        if self._zip is not None:
            self._zip.close()
            self._zip = None

    def add_resource(
        self,
        virtual_path: str,
        resource_type: Union[str, bytes, int],
        version: int,
        resource_id: int,
        payload: bytes,
        *,
        byte_order: Endianness = Endianness.BIG,
        is_custom: bool = False,
    ) -> None:
        """Writes one resource's header + payload into the archive at
        `virtual_path` (the path GoldenEye's game code / the runtime
        resource factories will look it up by).

        For Blob resources (`resource_type` == `"OBLB"`), `payload` is
        the raw bytes to store -- this prepends the `uint32 dataSize`
        length prefix `ResourceFactoryBinaryBlobV0::ReadResource()`
        expects (see module docstring); callers never write that prefix
        themselves."""
        if self._zip is None:
            raise RuntimeError("O2rWriter must be used as a context manager")

        type_value = resource_type_code(resource_type) if not isinstance(resource_type, int) else resource_type
        header = pack_resource_header(type_value, version, resource_id, byte_order=byte_order, is_custom=is_custom)

        if type_value == _BLOB_TYPE_CODE:
            endian_fmt = ">" if byte_order == Endianness.BIG else "<"
            payload = struct.pack(f"{endian_fmt}I", len(payload)) + payload

        self._zip.writestr(virtual_path, header + payload)

    def add_raw_file(self, virtual_path: str, data: bytes) -> None:
        """Writes bytes into the archive with no header -- for resource
        types that don't use the OTR header layout at all (this
        repo has none yet; provided for completeness/testability)."""
        if self._zip is None:
            raise RuntimeError("O2rWriter must be used as a context manager")
        self._zip.writestr(virtual_path, data)
