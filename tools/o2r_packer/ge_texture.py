#!/usr/bin/env python3
"""Decodes GoldenEye's own texture-bank bitstream format into raw pixel
bytes, for frigate issue #18 (step 1 of the runtime-resource-types work
tracked there -- see that issue for the full writeup).

Each texture in GoldenEye's bank (the `.bin` files `pack_textures.py`
reads from `assets/images/split/`) is not raw N64 pixel data: it is a
custom-compressed bitstream of Rare's own design. The first byte selects
one of two families (a palette-based family that's DEFLATE-compressed
despite decomp naming calling it "zlib", and a non-palette family with
per-image format/compression-method fields), and the non-palette family
has eight further compression schemes of its own (uncompressed, three
Huffman variants, three RLE variants, and a plain value-lookup table),
each operating on separate per-channel bit-planes rather than
interleaved pixels.

This is a fresh-wording reimplementation of that scheme, not a copy of
any existing source -- per `CLAUDE.md`, `external/007` (the GoldenEye
decomp) is unlicensed upstream, and while `external/007/tools/mktex`
("pd-tex", a standalone binary-to-PNG converter for GoldenEye/Perfect
Dark textures, at `tools/mktex/src/libpdtex/reader.c`) gave a far
cleaner read on the algorithm than the decomp's own `src/game/image.c`,
it's vendored inside the same submodule and carries the same
restriction. Every function here was written from bare algorithmic
facts (bitstream field widths, compression-method IDs, per-format
channel counts) cross-checked against both sources, not transcribed
from either.

Scope: this only decodes a texture's base image (LOD 0). GoldenEye's
format can embed further, lower-resolution LOD images after the first
(when the header's "has explicit LODs" bit is set), or have the engine
synthesize them at runtime from the base image (when it isn't) -- see
`external/007/src/game/image.c`'s `texLoad()`/`texShrinkPaletted()`.
Neither matters here: `Fast::Texture` (libultraship-wiiu's runtime
resource type this feeds, see `fast/resource/type/Texture.h`) has no
mip-chain concept, so only the base image is ever needed.

Output shape: pixel data is returned in GoldenEye's native N64-packed
byte layout per format (e.g. 2 bytes/pixel big-endian RGBA5551 for
RGBA16, 2 pixels/byte nibble-packed for CI4/I4/IA4), matching exactly
what `libultraship-wiiu`'s `Interpreter::ImportTexture*` family
(`external/libultraship-wiiu/src/fast/interpreter.cpp`) reads a
`Fast::Texture`'s `ImageData` as by default -- confirmed by reading
`ImportTextureRgba16`/`ImportTextureCi4`/`ImportTextureI4`, which all
byte-unpack `ImageData` directly rather than expecting it pre-converted
to true-colour RGBA. CI4/CI8 (paletted) textures decode to index bytes
plus a *separate* palette (see `DecodedTexture.palette_rgba5551`) --
this mirrors real N64 hardware, where a CI tile's colours come from a
separately-loaded TLUT, not from the tile's own texture data. GoldenEye
also has an IA16-flavoured palette variant (intensity/alpha, not
RGBA5551) for two of its four CI formats; libultraship-wiiu's CI4/CI8
importers only implement the RGBA5551 TLUT interpretation (see
`ImportTextureCi4`), so IA16 palettes are normalized to RGBA5551 here at
pack time (see `_ia16_to_rgba5551`) as a documented lossy approximation
pending that runtime gap being closed -- not a decode error.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass
from enum import IntEnum
from typing import List, Optional, Sequence


class GeTextureFormatError(ValueError):
    """Raised when a texture bitstream can't be decoded -- either it's
    corrupt, or it uses a (format, compression method) combination this
    module doesn't expect to occur in practice (see NonZlibCompression's
    docstring)."""


# --- Per-format facts -------------------------------------------------
#
# GoldenEye's 13 pixel formats, in on-disk field-value order. Values and
# per-format facts (channel count, whether a trailing 1-bit alpha plane
# is appended, the Huffman/RLE symbol-alphabet size, bits per pixel) are
# bare facts read from both `external/007/src/game/image.h` (the arrays
# `g_TexFormatNumChannels`/`g_TexFormatHas1BitAlpha`/
# `g_TexFormatChannelSizes`/`g_TexFormatBitsPerPixel`) and mktex's
# `libpdtex/pdtex.c` (the same four arrays, PD-side) -- both copies
# agree value-for-value.
class TexFormat(IntEnum):
    RGBA32 = 0
    RGBA16 = 1
    RGB24 = 2
    RGB15 = 3
    IA16 = 4
    IA8 = 5
    IA4 = 6
    I8 = 7
    I4 = 8
    RGBA16_CI8 = 9
    RGBA16_CI4 = 10
    IA16_CI8 = 11
    IA16_CI4 = 12


_NUM_CHANNELS = {
    TexFormat.RGBA32: 4,
    TexFormat.RGBA16: 3,
    TexFormat.RGB24: 3,
    TexFormat.RGB15: 3,
    TexFormat.IA16: 2,
    TexFormat.IA8: 2,
    TexFormat.IA4: 1,
    TexFormat.I8: 1,
    TexFormat.I4: 1,
}

_HAS_1BIT_ALPHA = {
    TexFormat.RGBA32: False,
    TexFormat.RGBA16: True,
    TexFormat.RGB24: False,
    TexFormat.RGB15: False,
    TexFormat.IA16: False,
    TexFormat.IA8: False,
    TexFormat.IA4: True,
    TexFormat.I8: False,
    TexFormat.I4: False,
}

# Huffman/RLE symbol-alphabet size per format (the value passed as
# `chansize` to texInflateHuffman/used to size the RLE fudge, and the
# frequency-table length read up front by Huffman decode).
_CHANNEL_SIZE = {
    TexFormat.RGBA32: 256,
    TexFormat.RGBA16: 32,
    TexFormat.RGB24: 256,
    TexFormat.RGB15: 32,
    TexFormat.IA16: 256,
    TexFormat.IA8: 16,
    TexFormat.IA4: 8,
    TexFormat.I8: 256,
    TexFormat.I4: 16,
}

# Bits per pixel -- used only to size a LOOKUP-method colour table entry
# (texBuildLookup reads this many bits per table entry, not per channel).
_BITS_PER_PIXEL = {
    TexFormat.RGBA32: 32,
    TexFormat.RGBA16: 16,
    TexFormat.RGB24: 24,
    TexFormat.RGB15: 15,
    TexFormat.IA16: 16,
    TexFormat.IA8: 8,
    TexFormat.IA4: 4,
    TexFormat.I8: 8,
    TexFormat.I4: 4,
}

# Fast::TextureType values this format's decoded bytes should be tagged
# with (see external/libultraship-wiiu/include/fast/resource/type/Texture.h).
# Confirmed against Interpreter::ImportTexture*'s native-byte-layout
# assumptions (interpreter.cpp), not guessed from the enum's naming alone.
_TEXTURE_TYPE = {
    TexFormat.RGBA32: 1,  # RGBA32bpp
    TexFormat.RGBA16: 2,  # RGBA16bpp
    TexFormat.RGB24: 1,  # RGBA32bpp (GE stores it 4 bytes/pixel, alpha forced 0xff)
    TexFormat.RGB15: 2,  # RGBA16bpp (alpha bit forced 1)
    TexFormat.IA16: 9,  # GrayscaleAlpha16bpp
    TexFormat.IA8: 8,  # GrayscaleAlpha8bpp
    TexFormat.IA4: 7,  # GrayscaleAlpha4bpp
    TexFormat.I8: 6,  # Grayscale8bpp
    TexFormat.I4: 5,  # Grayscale4bpp
    TexFormat.RGBA16_CI8: 4,  # Palette8bpp
    TexFormat.RGBA16_CI4: 3,  # Palette4bpp
    TexFormat.IA16_CI8: 4,  # Palette8bpp
    TexFormat.IA16_CI4: 3,  # Palette4bpp
}


class NonZlibCompression(IntEnum):
    """Compression methods for the non-paletted texture family (the
    "non-zlib" header branch). Values are bare facts from
    `external/007/src/game/image.h`'s `TEXCOMPMETHOD_*`/mktex's
    `PDCOMPRESSION_*` (both agree). UNCOMPRESSED0/1 are both plain,
    unentropy-coded pixel data -- nothing in either reference source
    distinguishes their decode behavior, only that two header values
    exist for it.

    CI formats (RGBA16_CI4/CI8, IA16_CI4/CI8) never appear with any of
    these methods in practice -- every N64-format/compression-method
    mapping function in both reference sources (texChannelsToPixels,
    texInflateLookup) only has cases for the 9 non-palette formats.
    """

    UNCOMPRESSED0 = 0
    UNCOMPRESSED1 = 1
    HUFFMAN = 2
    HUFFMAN_PER_CHANNEL = 3
    RLE = 4
    LOOKUP = 5
    HUFFMAN_LOOKUP = 6
    RLE_LOOKUP = 7
    HUFFMAN_BLUR = 8
    RLE_BLUR = 9


@dataclass(frozen=True)
class DecodedTexture:
    format: TexFormat
    width: int
    height: int
    texture_type: int
    pixel_data: bytes
    # Present only for CI4/CI8 formats: `numcolours` big-endian uint16
    # RGBA5551 entries (see module docstring re: IA16 normalization).
    palette_rgba5551: Optional[bytes] = None


# --- Bit reader ---------------------------------------------------------


class _BitReader:
    """MSB-first bit reader over a byte string. Matches the bit order of
    every reference source's `texReadBits`: bits are consumed from the
    most-significant end of each byte first."""

    def __init__(self, data: bytes):
        self._data = data
        self._byte_pos = 0
        self._bit_buffer = 0
        self._bits_available = 0

    def read(self, num_bits: int) -> int:
        while self._bits_available < num_bits:
            if self._byte_pos >= len(self._data):
                raise GeTextureFormatError("texture bitstream ran out of data")
            self._bit_buffer = (self._bit_buffer << 8) | self._data[self._byte_pos]
            self._byte_pos += 1
            self._bits_available += 8

        self._bits_available -= num_bits
        value = (self._bit_buffer >> self._bits_available) & ((1 << num_bits) - 1)
        return value

    def aligned_byte_offset(self) -> int:
        """Byte offset of the next unread byte. Only meaningful when the
        reader is currently byte-aligned (no bits buffered) -- true at
        every call site in this module, since every field read before a
        byte-aligned sub-stream (the DEFLATE payload in the paletted
        branch) is itself a multiple of 8 bits."""
        assert self._bits_available == 0, "reader is not byte-aligned"
        return self._byte_pos


# --- Huffman decode -------------------------------------------------------


def _build_huffman_tree(frequencies: List[int], alphabet_size: int) -> tuple:
    """Builds the same frequency-merge tree `_inflate_huffman` decodes
    against (see its docstring re: why this exact merge order matters).
    `frequencies` is consumed/mutated like the reference algorithm's
    working copy; callers that need the original frequencies afterward
    should pass a copy. Returns `(root, children)`, where
    `children[node] = [left, right]` and a child index >= 10000 denotes
    a leaf carrying the symbol value `(index - 10000)`."""
    children: List[List[int]] = [[-1, -1] for _ in range(alphabet_size)]

    def find_two_smallest():
        min1, min2 = 9999, 9999
        idx1, idx2 = -1, -1
        for i in range(alphabet_size):
            if frequencies[i] < min1:
                if min1 > min2:
                    min1, idx1 = frequencies[i], i
                else:
                    min2, idx2 = frequencies[i], i
            elif frequencies[i] < min2:
                min2, idx2 = frequencies[i], i
        return min1, idx1, min2, idx2

    min1, idx1, min2, idx2 = find_two_smallest()
    root = 0

    while min1 != 9999 and min2 != 9999:
        total = frequencies[idx1] + frequencies[idx2]
        if total == 0:
            total = 1
        frequencies[idx1] = 9999
        frequencies[idx2] = 9999

        if children[idx1] == [-1, -1]:
            children[idx1][0] = idx1 + 10000
            root = idx1
            frequencies[idx1] = total
            children[idx1][1] = idx2 + 10000 if children[idx2] == [-1, -1] else idx2
        elif children[idx2] == [-1, -1]:
            children[idx2][0] = idx2 + 10000
            root = idx2
            frequencies[idx2] = total
            children[idx2][1] = idx1 + 10000 if children[idx1] == [-1, -1] else idx1
        else:
            new_node = next(
                i
                for i in range(alphabet_size)
                if children[i] == [-1, -1] and frequencies[i] >= 9999
            )
            frequencies[new_node] = total
            children[new_node][0] = idx1
            children[new_node][1] = idx2
            root = new_node

        min1, idx1, min2, idx2 = find_two_smallest()

    return root, children


def _inflate_huffman(reader: _BitReader, num_symbols: int, alphabet_size: int) -> List[int]:
    """Decodes `num_symbols` values from a canonical-ish Huffman stream
    whose per-symbol frequency table (one byte per alphabet entry) is
    read up front. Tree-build algorithm and node-encoding convention
    (values >= 10000 are leaves) are a bare fact of the format, not an
    arbitrary implementation choice -- both reference sources build the
    identical tree from the identical frequency table, so any
    conforming implementation must reproduce this exact merge order to
    decode real data correctly."""
    frequencies = [reader.read(8) for _ in range(alphabet_size)]
    root, children = _build_huffman_tree(frequencies, alphabet_size)

    out = []
    for _ in range(num_symbols):
        node = root
        while node < 10000:
            node = children[node][reader.read(1)]
        out.append(node - 10000)
    return out


# --- RLE decode -------------------------------------------------------


def _inflate_rle(reader: _BitReader, num_values: int) -> List[int]:
    """Decodes a run-length-with-back-reference stream: each directive
    bit selects either a literal value or a run that copies
    `run_length` values starting `back_distance` values before the
    current write position, followed by one mandatory literal. Field
    widths (`back-distance` bits, `run-length` bits, value-width bits)
    and the "fudge" constant are read/derived from the stream itself,
    per both reference sources' `texInflateRle`."""
    back_bits = reader.read(3)
    run_bits = reader.read(3)
    value_bits = reader.read(4)

    cost = back_bits + run_bits + value_bits + 1
    fudge = 0
    while cost > 0:
        cost -= value_bits + 1
        fudge += 1

    out: List[int] = []
    while len(out) < num_values:
        if reader.read(1) == 0:
            out.append(reader.read(value_bits))
        else:
            start = len(out) - reader.read(back_bits) - 1
            run_length = reader.read(run_bits) + fudge
            for i in range(start, start + run_length):
                out.append(out[i])
            out.append(reader.read(value_bits))
    return out


# --- Lookup-table decode -------------------------------------------------


def _build_lookup_table(reader: _BitReader, bits_per_pixel: int) -> List[int]:
    """Reads a value-lookup table: an entry count, then that many
    entries at `bits_per_pixel` bits each (matching each format's
    native packed-pixel width, per `_BITS_PER_PIXEL`)."""
    num_entries = reader.read(11)
    return [reader.read(bits_per_pixel) for _ in range(num_entries)]


def _inflate_lookup_indices(reader: _BitReader, num_pixels: int, num_entries: int) -> List[int]:
    bits_per_index = max(1, (num_entries - 1).bit_length())
    return [reader.read(bits_per_index) for _ in range(num_pixels)]


# --- Blur post-process -------------------------------------------------


def _blur(values: List[int], width: int, height: int, method: int, alphabet_size: int) -> List[int]:
    out = list(values)

    def at(x: int, y: int) -> int:
        return values[y * width + x] if 0 <= x < width and 0 <= y < height else 0

    for y in range(height):
        for x in range(width):
            cur = values[y * width + x] + alphabet_size * 2
            left = at(x - 1, y)
            above = at(x, y - 1)
            above_left = at(x - 1, y - 1)
            if method == 0:
                out[y * width + x] = (cur + left) % alphabet_size
            elif method == 1:
                out[y * width + x] = (cur + above) % alphabet_size
            elif method == 2:
                out[y * width + x] = (cur + above_left) % alphabet_size
            elif method == 3:
                out[y * width + x] = (cur + (left + above - above_left)) % alphabet_size
            elif method == 4:
                out[y * width + x] = (cur + ((above - above_left) // 2 + left)) % alphabet_size
            elif method == 5:
                out[y * width + x] = (cur + ((left - above_left) // 2 + above)) % alphabet_size
            elif method == 6:
                out[y * width + x] = (cur + ((left + above) // 2)) % alphabet_size
            else:
                raise GeTextureFormatError(f"unknown blur method {method}")
    return out


# --- Channel planes -> native packed pixel bytes ------------------------


def _channels_to_native_bytes(channels: Sequence[Sequence[int]], fmt: TexFormat, width: int, height: int) -> bytes:
    """Assembles per-channel bit-planes (each `width*height` values, one
    plane per colour channel plus, when `_HAS_1BIT_ALPHA[fmt]`, a
    trailing 1-bit alpha plane) into GoldenEye's native N64-packed pixel
    bytes -- the same layout `external/007/src/game/image.c`'s
    `texChannelsToPixels` produces and what
    `libultraship-wiiu`'s `Interpreter::ImportTexture*` reads directly.
    """
    n = width * height
    out = bytearray()

    if fmt == TexFormat.RGBA32:
        r, g, b, a = channels
        for i in range(n):
            out += bytes((r[i], g[i], b[i], a[i]))
    elif fmt == TexFormat.RGB24:
        r, g, b = channels
        for i in range(n):
            out += bytes((r[i], g[i], b[i], 0xFF))
    elif fmt == TexFormat.RGBA16:
        r, g, b, a = channels
        for i in range(n):
            word = (r[i] << 11) | (g[i] << 6) | (b[i] << 1) | a[i]
            out += struct.pack(">H", word)
    elif fmt == TexFormat.RGB15:
        r, g, b = channels
        for i in range(n):
            word = (r[i] << 11) | (g[i] << 6) | (b[i] << 1) | 1
            out += struct.pack(">H", word)
    elif fmt == TexFormat.IA16:
        intensity, alpha = channels
        for i in range(n):
            out += bytes((intensity[i], alpha[i]))
    elif fmt == TexFormat.IA8:
        intensity, alpha = channels
        for i in range(n):
            out.append(((intensity[i] & 0xF) << 4) | (alpha[i] & 0xF))
    elif fmt == TexFormat.IA4:
        (intensity,) = channels[:1]
        alpha = channels[-1]
        nibbles = [((intensity[i] & 0x7) << 1) | (alpha[i] & 1) for i in range(n)]
        return _pack_nibbles(nibbles, width, height)
    elif fmt == TexFormat.I8:
        (intensity,) = channels
        out += bytes(intensity[i] for i in range(n))
    elif fmt == TexFormat.I4:
        (intensity,) = channels
        return _pack_nibbles(list(intensity[:n]), width, height)
    else:
        raise GeTextureFormatError(f"format {fmt!r} has no channel-plane layout (paletted formats decode separately)")

    return bytes(out)


def _lookup_entry_to_native_bytes(fmt: TexFormat, entry: int) -> bytes:
    """A LOOKUP-method table's entries are already-packed native pixel
    words (not separate channel planes) -- this is the identity
    transform for the widths `_build_lookup_table` already read, kept
    as its own function so the byte order is defined in exactly one
    place."""
    if fmt == TexFormat.RGBA32:
        return struct.pack(">I", entry)
    if fmt == TexFormat.RGB24:
        return bytes(((entry >> 16) & 0xFF, (entry >> 8) & 0xFF, entry & 0xFF, 0xFF))
    if fmt in (TexFormat.RGBA16, TexFormat.IA16):
        return struct.pack(">H", entry)
    if fmt == TexFormat.RGB15:
        return struct.pack(">H", (entry << 1) | 1)
    if fmt in (TexFormat.IA8, TexFormat.I8):
        return bytes((entry & 0xFF,))
    if fmt in (TexFormat.IA4, TexFormat.I4):
        return bytes((entry & 0xF,))
    raise GeTextureFormatError(f"format {fmt!r} has no LOOKUP entry layout")


def _assemble_from_lookup_indices(fmt: TexFormat, table: Sequence[int], indices: Sequence[int], width: int, height: int) -> bytes:
    """Resolves a stream of table indices (LOOKUP/HUFFMAN_LOOKUP/RLE_LOOKUP)
    into native packed pixel bytes. 4-bit formats pack 2 pixels/byte;
    every other format's table entries are already one native pixel
    word each (see `_lookup_entry_to_native_bytes`)."""
    if fmt in (TexFormat.IA4, TexFormat.I4):
        return _pack_nibbles([table[i] & 0xF for i in indices], width, height)
    return b"".join(_lookup_entry_to_native_bytes(fmt, table[i]) for i in indices)


def _pack_nibbles(nibble_values: Sequence[int], width: int, height: int) -> bytes:
    """Packs 2 nibble values/byte, resetting to a fresh byte at the start
    of each row (an odd-width row's last nibble is zero-padded, never
    carried into the next row) -- both reference sources agree 4-bit
    formats are laid out `ceil(width/2)` bytes/row, matching what
    `libultraship-wiiu`'s `Interpreter::ImportTextureI4`/`ImportTextureCi4`
    (interpreter.cpp) read: a row stride derived from the display list's
    own tile state, independent of any other row's contents."""
    out = bytearray()
    for y in range(height):
        row = nibble_values[y * width : (y + 1) * width]
        for i in range(0, width, 2):
            hi = row[i] & 0xF
            lo = (row[i + 1] & 0xF) if i + 1 < width else 0
            out.append((hi << 4) | lo)
    return bytes(out)


# --- Non-paletted ("non-zlib") branch ------------------------------------


def _read_uncompressed_channels(reader: _BitReader, fmt: TexFormat, width: int, height: int) -> bytes:
    """UNCOMPRESSED0/1: plain, unencoded pixel data. Unlike every other
    compression method, this reads already-packed native pixel words
    directly (one native-width read per pixel), not separate channel
    planes -- a bare fact of `texReadUncompressed` in both reference
    sources, which is structured differently from every other
    `texInflateNonZlib` case for this reason."""
    n = width * height
    if fmt == TexFormat.RGBA32:
        return b"".join(struct.pack(">I", reader.read(32)) for _ in range(n))
    if fmt == TexFormat.RGB24:
        out = bytearray()
        for _ in range(n):
            rgb = reader.read(24)
            out += bytes(((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 0xFF))
        return bytes(out)
    if fmt in (TexFormat.RGBA16, TexFormat.IA16):
        return b"".join(struct.pack(">H", reader.read(16)) for _ in range(n))
    if fmt == TexFormat.RGB15:
        return b"".join(struct.pack(">H", (reader.read(15) << 1) | 1) for _ in range(n))
    if fmt in (TexFormat.IA8, TexFormat.I8):
        return bytes(reader.read(8) for _ in range(n))
    if fmt in (TexFormat.IA4, TexFormat.I4):
        return _pack_nibbles([reader.read(4) for _ in range(n)], width, height)
    raise GeTextureFormatError(f"format {fmt!r} not valid for the non-paletted branch")


def _decode_non_zlib_image(reader: _BitReader) -> DecodedTexture:
    fmt = TexFormat(reader.read(4))
    width = reader.read(8)
    height = reader.read(8)
    compression = NonZlibCompression(reader.read(4))
    n = width * height

    has_alpha = _HAS_1BIT_ALPHA[fmt]
    num_channels = _NUM_CHANNELS[fmt]
    alphabet_size = _CHANNEL_SIZE[fmt]

    def read_alpha_plane() -> List[int]:
        return [reader.read(1) for _ in range(n)] if has_alpha else []

    def channels_from_planes(planes: List[List[int]]) -> bytes:
        if has_alpha:
            planes = planes + [read_alpha_plane()]
        return _channels_to_native_bytes(planes, fmt, width, height)

    if compression in (NonZlibCompression.UNCOMPRESSED0, NonZlibCompression.UNCOMPRESSED1):
        pixel_data = _read_uncompressed_channels(reader, fmt, width, height)

    elif compression == NonZlibCompression.HUFFMAN:
        flat = _inflate_huffman(reader, num_channels * n, alphabet_size)
        planes = [flat[c * n : (c + 1) * n] for c in range(num_channels)]
        pixel_data = channels_from_planes(planes)

    elif compression == NonZlibCompression.HUFFMAN_PER_CHANNEL:
        planes = [_inflate_huffman(reader, n, alphabet_size) for _ in range(num_channels)]
        pixel_data = channels_from_planes(planes)

    elif compression == NonZlibCompression.RLE:
        flat = _inflate_rle(reader, num_channels * n)
        planes = [flat[c * n : (c + 1) * n] for c in range(num_channels)]
        pixel_data = channels_from_planes(planes)

    elif compression == NonZlibCompression.LOOKUP:
        table = _build_lookup_table(reader, _BITS_PER_PIXEL[fmt])
        indices = _inflate_lookup_indices(reader, n, len(table))
        pixel_data = _assemble_from_lookup_indices(fmt, table, indices, width, height)

    elif compression == NonZlibCompression.HUFFMAN_LOOKUP:
        table = _build_lookup_table(reader, _BITS_PER_PIXEL[fmt])
        indices = _inflate_huffman(reader, n, len(table))
        pixel_data = _assemble_from_lookup_indices(fmt, table, indices, width, height)

    elif compression == NonZlibCompression.RLE_LOOKUP:
        table = _build_lookup_table(reader, _BITS_PER_PIXEL[fmt])
        indices = _inflate_rle(reader, n)
        pixel_data = _assemble_from_lookup_indices(fmt, table, indices, width, height)

    elif compression in (NonZlibCompression.HUFFMAN_BLUR, NonZlibCompression.RLE_BLUR):
        blur_method = reader.read(3)
        if compression == NonZlibCompression.HUFFMAN_BLUR:
            flat = _inflate_huffman(reader, num_channels * n, alphabet_size)
        else:
            flat = _inflate_rle(reader, num_channels * n)
        blurred = _blur(flat, width, num_channels * height, blur_method, alphabet_size)
        planes = [blurred[c * n : (c + 1) * n] for c in range(num_channels)]
        pixel_data = channels_from_planes(planes)

    else:
        raise GeTextureFormatError(f"unhandled compression method {compression!r}")

    return DecodedTexture(
        format=fmt,
        width=width,
        height=height,
        texture_type=_TEXTURE_TYPE[fmt],
        pixel_data=pixel_data,
    )


# --- Paletted ("zlib") branch --------------------------------------------


def _rzip_inflate(data: bytes) -> bytes:
    """GoldenEye's "rarezip" container is plain raw DEFLATE (RFC 1951)
    behind a 2- or 5-byte header identifying the container version --
    not actually zlib-wrapped despite the decomp's own naming. Bare fact
    confirmed directly against both `external/007/src/game/decompress.c`
    (`rz_header_bytes`) and mktex's `rzipInflate()`
    (`libpdtex/reader.c`), which calls `inflateInit2(&strm, -15)` --
    zlib's raw-deflate mode -- on exactly this data."""
    if len(data) >= 2 and data[0] == 0x11 and data[1] == 0x73:
        payload = data[5:]
    elif len(data) >= 2 and data[0] == 0x11 and data[1] == 0x72:
        payload = data[2:]
    else:
        raise GeTextureFormatError(f"not a recognized rzip header: {data[:2]!r}")

    return zlib.decompressobj(wbits=-15).decompress(payload)


def _ia16_to_rgba5551(word: int) -> int:
    """Normalizes an IA16 (intensity8/alpha8) palette entry to RGBA5551
    -- see module docstring re: this being a documented lossy
    approximation, not a decode of real N64 IA16-TLUT hardware
    behavior."""
    intensity = (word >> 8) & 0xFF
    alpha = word & 0xFF
    channel5 = intensity >> 3
    alpha1 = 1 if alpha >= 128 else 0
    return (channel5 << 11) | (channel5 << 6) | (channel5 << 1) | alpha1


def _decode_zlib_image(reader: _BitReader, data: bytes) -> DecodedTexture:
    fmt = TexFormat(reader.read(8))
    if fmt not in (TexFormat.RGBA16_CI8, TexFormat.RGBA16_CI4, TexFormat.IA16_CI8, TexFormat.IA16_CI4):
        raise GeTextureFormatError(f"paletted branch used with non-paletted format {fmt!r}")

    num_colours = reader.read(8) + 1
    is_ia16 = fmt in (TexFormat.IA16_CI8, TexFormat.IA16_CI4)
    palette_words = [reader.read(16) for _ in range(num_colours)]
    if is_ia16:
        palette_words = [_ia16_to_rgba5551(w) for w in palette_words]
    palette_bytes = b"".join(struct.pack(">H", w) for w in palette_words)

    width = reader.read(8)
    height = reader.read(8)

    deflate_start = reader.aligned_byte_offset()
    indices = _rzip_inflate(data[deflate_start:])

    # The inflated index stream is already in GoldenEye's native N64-TMEM
    # packing -- 1 byte/index for CI8, 2 nibble-packed indices/byte for
    # CI4 (`ceil(width/2)` bytes/row) -- confirmed against
    # `external/007/src/game/image.c`'s `texAlignIndices`, which copies
    # these bytes through unchanged (only padding rows to an 8-byte
    # boundary, which this packer's tightly-packed output doesn't need).
    # This differs from `external/007/tools/mktex`'s own `texAlignIndices`,
    # which additionally unpacks CI4 to 1 byte/index for that tool's own
    # internal convenience -- not evidence about the wire format, since
    # both agree on how many *source* bytes each output row consumes.
    is_ci4 = fmt in (TexFormat.RGBA16_CI4, TexFormat.IA16_CI4)
    row_bytes = (width + 1) // 2 if is_ci4 else width
    pixel_data = bytes(indices[: row_bytes * height])

    return DecodedTexture(
        format=fmt,
        width=width,
        height=height,
        texture_type=_TEXTURE_TYPE[fmt],
        pixel_data=pixel_data,
        palette_rgba5551=palette_bytes,
    )


# --- Entry point ----------------------------------------------------------


def decode_texture(data: bytes) -> DecodedTexture:
    """Decodes a single split `.bin` texture's base image (LOD 0) from
    its raw GoldenEye-native bitstream. See module docstring."""
    if not data:
        raise GeTextureFormatError("empty texture data")

    reader = _BitReader(data)
    reader.read(1)  # "has explicit LODs" -- irrelevant for LOD 0, see module docstring
    is_paletted = reader.read(1)
    reader.read(6)  # LOD count -- ditto

    if is_paletted:
        return _decode_zlib_image(reader, data)
    return _decode_non_zlib_image(reader)
