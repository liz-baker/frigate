#!/usr/bin/env python3
"""Compares `ge_texture.py`'s output against the GoldenEye decomp's own
texture decoder (`tools/mktex`'s libpdtex), as an independent check on
real ROM data. Used by test_rom_textures.py.

The reference decoder is a converter tool, not the game, and disagrees
with the decomp's game code in a few places; `compare()` skips those
rather than counting them as decoder bugs (see `_skip_reason`).
"""

from __future__ import annotations

import shutil
import struct
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from ge_texture import DecodedTexture, TexFormat, _ia16_to_rgba5551

_DUMPER_SOURCE = Path(__file__).with_name("reference_dump.c")

_LIBPDTEX_SOURCES = ("pdtex.c", "reader.c", "writer.c")


@dataclass(frozen=True)
class ReferenceTexture:
    format: TexFormat
    width: int
    height: int
    compression: int
    pixels: bytes
    palette: bytes


def build_reference_dumper(decomp_dir: Path, output: Path) -> bool:
    """Compiles reference_dump.c against the decomp's libpdtex. Returns
    False if there's no C compiler or the build fails, so callers can
    skip rather than fail on machines without one."""
    compiler = shutil.which("gcc") or shutil.which("cc")
    if compiler is None:
        return False

    libpdtex = Path(decomp_dir) / "tools" / "mktex" / "src"
    sources = [str(libpdtex / "libpdtex" / name) for name in _LIBPDTEX_SOURCES]
    result = subprocess.run(
        [compiler, "-I", str(libpdtex), str(_DUMPER_SOURCE), *sources, "-lz", "-O2", "-o", str(output)],
        capture_output=True,
    )
    return result.returncode == 0


def dump_reference(dumper: Path, texture_path: Path, scratch: Path) -> Optional[ReferenceTexture]:
    """Runs the reference decoder over one split texture file. None if it
    couldn't decode the file at all."""
    if subprocess.run([str(dumper), str(texture_path), str(scratch)], capture_output=True).returncode != 0:
        return None

    data = scratch.read_bytes()
    fmt, width, height, compression, num_colours = struct.unpack("<5i", data[:20])
    body = data[20:]
    pixel_bytes = len(body) - num_colours * 2 if fmt >= TexFormat.RGBA16_CI8 else len(body)
    return ReferenceTexture(
        format=TexFormat(fmt),
        width=width,
        height=height,
        compression=compression,
        pixels=body[:pixel_bytes],
        palette=body[pixel_bytes:],
    )


# Non-paletted compression methods whose reference output is wrong for
# these formats. Checked against the decomp's game code
# (`texInflateLookupFromBuffer`): for 8-bit/4-bit formats the reference
# reads the wrong byte of each table entry, and it drops the high byte of
# an index when a table has over 256 entries.
_LOOKUP_FROM_BUFFER_METHODS = (6, 7)
_LOOKUP_FROM_BUFFER_OK_FORMATS = (TexFormat.RGBA16, TexFormat.RGB15, TexFormat.IA16)

_UNCOMPRESSED_METHODS = (0, 1)


def _skip_reason(decoded: DecodedTexture, ref: ReferenceTexture) -> Optional[str]:
    if decoded.format == TexFormat.IA4:
        return "reference lays out IA4 rows differently to every other format"
    if decoded.palette_rgba5551 is None:
        if ref.compression in _UNCOMPRESSED_METHODS:
            return "reference has no uncompressed-texture support"
        if ref.compression in _LOOKUP_FROM_BUFFER_METHODS and decoded.format not in _LOOKUP_FROM_BUFFER_OK_FORMATS:
            return "reference mis-indexes lookup tables for this format"
    return None


def _flip_rows(data: bytes, row_bytes: int, height: int) -> bytes:
    """The reference flips rows to top-down for PNG output; this decoder
    keeps the game's own row order, so flip ours before comparing."""
    rows = [data[y * row_bytes : (y + 1) * row_bytes] for y in range(height)]
    return b"".join(reversed(rows))


def _unpack_nibbles(packed: bytes, width: int, height: int) -> bytes:
    row_bytes = (width + 1) // 2
    out = bytearray()
    for y in range(height):
        for x in range(width):
            byte = packed[y * row_bytes + x // 2]
            out.append(byte >> 4 if x % 2 == 0 else byte & 0xF)
    return bytes(out)


_BYTES_PER_PIXEL = {
    TexFormat.RGBA32: 4,
    TexFormat.RGBA16: 2,
    TexFormat.RGB15: 2,
    TexFormat.IA16: 2,
    TexFormat.IA8: 1,
    TexFormat.I8: 1,
    TexFormat.RGBA16_CI8: 1,
    TexFormat.IA16_CI8: 1,
}


def _comparable_pixels(decoded: DecodedTexture) -> bytes:
    """`decoded`'s pixels in the reference's in-memory layout: one byte
    per pixel for the 4-bit formats, three bytes for RGB24 (the game keeps
    a fourth, constant, alpha byte), rows top-down."""
    fmt, width, height = decoded.format, decoded.width, decoded.height
    if fmt in (TexFormat.I4, TexFormat.RGBA16_CI4, TexFormat.IA16_CI4):
        return _flip_rows(_unpack_nibbles(decoded.pixel_data, width, height), width, height)
    if fmt == TexFormat.RGB24:
        rgb = b"".join(decoded.pixel_data[i : i + 3] for i in range(0, len(decoded.pixel_data), 4))
        return _flip_rows(rgb, width * 3, height)
    return _flip_rows(decoded.pixel_data, width * _BYTES_PER_PIXEL[fmt], height)


def _comparable_palette(decoded: DecodedTexture, ref: ReferenceTexture) -> bytes:
    """The reference's palette, with IA16 entries put through the same
    normalisation ge_texture.py applies, so the two are comparable."""
    if decoded.format not in (TexFormat.IA16_CI8, TexFormat.IA16_CI4):
        return ref.palette
    words = struct.unpack(f">{len(ref.palette) // 2}H", ref.palette)
    return b"".join(struct.pack(">H", _ia16_to_rgba5551(w)) for w in words)


def compare(decoded: DecodedTexture, ref: ReferenceTexture) -> str:
    """Returns "match", "mismatch", or "skipped: <reason>"."""
    if (decoded.format, decoded.width, decoded.height) != (ref.format, ref.width, ref.height):
        return "mismatch"

    reason = _skip_reason(decoded, ref)
    if reason:
        return f"skipped: {reason}"

    if decoded.palette_rgba5551 is not None and decoded.palette_rgba5551 != _comparable_palette(decoded, ref):
        return "mismatch"

    return "match" if _comparable_pixels(decoded) == ref.pixels else "mismatch"
