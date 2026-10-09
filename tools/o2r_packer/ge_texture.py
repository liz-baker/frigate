#!/usr/bin/env python3
"""Decodes GoldenEye's texture-bank bitstream into pixel bytes for
`Fast::Texture` O2R resources (frigate issue #18). On-disk layout:
docs/formats/goldeneye-texture-bitstream.md.

Fresh-wording reimplementation, not adapted from existing source: the
GoldenEye decomp and its own vendored `tools/mktex` reference tool are
both unlicensed upstream, so every function here was written from bare
algorithmic facts cross-checked against both, not transcribed from
either.

Output is native N64-packed bytes (not true-colour RGBA) because that's
what libultraship's `Interpreter::ImportTexture*` family
(`external/libultraship-wiiu/src/fast/interpreter.cpp`) reads a
`Fast::Texture`'s `ImageData` as by default. CI4/CI8 (paletted) textures
decode to index bytes plus a *separate* palette
(`DecodedTexture.palette_rgba5551`), mirroring real N64 hardware, where
a CI tile's colours come from a separately-loaded TLUT rather than its
own texture data.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass
from enum import IntEnum
from typing import List, Optional, Sequence


class GeTextureFormatError(ValueError):
    """Raised when a texture bitstream can't be decoded -- either it's
    corrupt, or it uses a (format, compression method) combination that
    shouldn't occur in practice (a CI format via the non-paletted
    branch, say)."""


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


# TexFormat and every dict below it, through _TEXTURE_TYPE: on-disk
# layout docs/formats/goldeneye-texture-bitstream.md.
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
    """Compression methods for the non-paletted branch. On-disk layout:
    docs/formats/goldeneye-texture-bitstream.md."""

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
    # RGBA5551 entries (see `_ia16_to_rgba5551` re: the two IA16 formats).
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
    """Builds the tree `_inflate_huffman` walks to decode symbols.
    Reproduces the exact tie-breaking/merge order the bare frequency
    table doesn't otherwise pin down -- the decoder has to rebuild the
    identical tree the original encoder assigned codes against, so this
    isn't just "a" valid Huffman tree. Mutates `frequencies` in place;
    pass a copy if the caller needs the original values afterward.
    Returns `(root, children)`, where `children[node] = [left, right]`
    and a child index >= 10000 denotes a leaf carrying symbol value
    `(index - 10000)`."""
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
    """Decodes `num_symbols` values from a Huffman stream: a per-symbol
    frequency table (see `_build_huffman_tree`), then one bit-walk from
    the tree root per symbol."""
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
    """On-disk layout: docs/formats/goldeneye-texture-bitstream.md. A
    run is always immediately followed by one literal value with no
    directive bit of its own -- easy to miss since every other value in
    the stream is directive-prefixed."""
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
    num_entries = reader.read(11)
    return [reader.read(bits_per_pixel) for _ in range(num_entries)]


def _inflate_lookup_indices(reader: _BitReader, num_pixels: int, num_entries: int) -> List[int]:
    bits_per_index = (num_entries - 1).bit_length()
    return [reader.read(bits_per_index) for _ in range(num_pixels)]


# --- Blur post-process -------------------------------------------------


def _blur(values: List[int], width: int, height: int, method: int, alphabet_size: int) -> List[int]:
    """Predictive filter: each value is stored as a delta against a
    prediction built from its already-reconstructed neighbours, so this
    must read and write the same array in scan order. The `/ 2` terms
    also have to truncate toward zero like C's, not floor."""
    out = list(values)

    def at(x: int, y: int) -> int:
        return out[y * width + x] if x >= 0 and y >= 0 else 0

    def half(v: int) -> int:
        return int(v / 2)

    for y in range(height):
        for x in range(width):
            cur = out[y * width + x] + alphabet_size * 2
            left = at(x - 1, y)
            above = at(x, y - 1)
            above_left = at(x - 1, y - 1)
            if method == 0:
                predicted = left
            elif method == 1:
                predicted = above
            elif method == 2:
                predicted = above_left
            elif method == 3:
                predicted = left + above - above_left
            elif method == 4:
                predicted = half(above - above_left) + left
            elif method == 5:
                predicted = half(left - above_left) + above
            elif method == 6:
                predicted = half(left + above)
            else:
                raise GeTextureFormatError(f"unknown blur method {method}")
            out[y * width + x] = (cur + predicted) % alphabet_size
    return out


# --- Channel planes -> native packed pixel bytes ------------------------


def _channels_to_native_bytes(channels: Sequence[Sequence[int]], fmt: TexFormat, width: int, height: int) -> bytes:
    """Assembles per-channel bit-planes into native packed pixel bytes.
    Layout: docs/formats/goldeneye-texture-bitstream.md."""
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
    """A LOOKUP-method table's entries are already native pixel words,
    not channel planes -- this just fixes the byte order per format."""
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
    if fmt in (TexFormat.IA4, TexFormat.I4):
        return _pack_nibbles([table[i] & 0xF for i in indices], width, height)
    return b"".join(_lookup_entry_to_native_bytes(fmt, table[i]) for i in indices)


def _pack_nibbles(nibble_values: Sequence[int], width: int, height: int) -> bytes:
    """Packs 2 nibble values/byte, resetting to a fresh byte at the
    start of each row -- an odd-width row's last nibble is zero-padded,
    never carried into the next row (libultraship's texture importers
    derive a row's byte stride independent of any other row)."""
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
    """UNCOMPRESSED0/1: unlike every other compression method, this
    reads one already-packed native pixel word per pixel directly, not
    separate channel planes."""
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


def rzip_inflate(data: bytes) -> bytes:
    """"rarezip" is plain raw DEFLATE despite the GoldenEye decomp's own
    "zlib" naming for it -- not actually zlib-wrapped."""
    if len(data) >= 2 and data[0] == 0x11 and data[1] == 0x73:
        payload = data[5:]
    elif len(data) >= 2 and data[0] == 0x11 and data[1] == 0x72:
        payload = data[2:]
    else:
        raise GeTextureFormatError(f"not a recognized rzip header: {data[:2]!r}")

    return zlib.decompressobj(wbits=-15).decompress(payload)


def _ia16_to_rgba5551(word: int) -> int:
    """Lossy approximation, not a decode of real N64 IA16-TLUT hardware
    behavior -- libultraship's CI4/CI8 importers only implement the
    RGBA5551 TLUT interpretation, so an IA16 palette is normalized here
    rather than passed through as something nothing can read yet."""
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
    indices = rzip_inflate(data[deflate_start:])

    # Already GoldenEye's native N64-TMEM index packing -- see
    # docs/formats/goldeneye-texture-bitstream.md re: this NOT matching
    # what tools/mktex's own reader does with the same bytes.
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
    """Decodes a single split `.bin` texture's base image. Only the base
    image (LOD 0) is ever decoded -- `Fast::Texture` has no mip-chain
    concept, so further LOD images the bitstream might carry (see
    docs/formats/goldeneye-texture-bitstream.md) are irrelevant here."""
    if not data:
        raise GeTextureFormatError("empty texture data")

    reader = _BitReader(data)
    reader.read(1)  # has-explicit-LODs bit -- irrelevant, see above
    is_paletted = reader.read(1)
    reader.read(6)  # LOD count -- ditto

    if is_paletted:
        return _decode_zlib_image(reader, data)
    return _decode_non_zlib_image(reader)
