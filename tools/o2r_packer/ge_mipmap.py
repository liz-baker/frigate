#!/usr/bin/env python3
"""Builds the lower mip levels GoldenEye generates when it loads a texture
(frigate issue #317), so the packer can write them as a `<name>.mip`
companion resource. Chain layout: docs/formats/goldeneye-texture-mip-chain.md.

Fresh-wording reimplementation of the GoldenEye decomp's `texShrinkPaletted`,
`texShrinkNonPaletted` and closest-palette-entry searches, written from the
algorithms' facts (their averaging, rounding, row alignment and edge
handling), not transcribed. Each quirk the game has is kept, because the
levels it shows are the ones those quirks produce.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import List, Optional, Sequence

from ge_texture import DecodedTexture, TexFormat

# The bytes of the base level plus every generated level must fit this for a
# paletted image; the game stops generating at the level that would exceed it.
PALETTED_CHAIN_LIMIT = 0x800
_PALETTE_TABLE_SIZE = 0x100

# Squared five-bit channel range: the cost of an alpha mismatch in the RGBA
# closest-colour search.
_ALPHA_WEIGHT = 31 * 31

_PALETTED = (TexFormat.RGBA16_CI8, TexFormat.RGBA16_CI4, TexFormat.IA16_CI8, TexFormat.IA16_CI4)
_IA_PALETTE = (TexFormat.IA16_CI8, TexFormat.IA16_CI4)
_FOUR_BIT = (TexFormat.IA4, TexFormat.I4, TexFormat.RGBA16_CI4, TexFormat.IA16_CI4)
_EIGHT_BIT = (TexFormat.IA8, TexFormat.I8, TexFormat.RGBA16_CI8, TexFormat.IA16_CI8)
_THIRTY_TWO_BIT = (TexFormat.RGBA32, TexFormat.RGB24)


@dataclass(frozen=True)
class MipLevel:
    width: int
    height: int
    # Rows padded to whole TMEM words (see aligned_row_texels).
    data: bytes


def aligned_row_texels(fmt: TexFormat, width: int) -> int:
    """Row length in texels once padded to a whole 64-bit TMEM word, as the
    game lays out every level in memory."""
    if fmt in _FOUR_BIT:
        return (width + 15) & ~15
    if fmt in _EIGHT_BIT:
        return (width + 7) & ~7
    return (width + 3) & ~3


def _bytes_per_row(fmt: TexFormat, texels: int) -> int:
    if fmt in _FOUR_BIT:
        return texels // 2
    if fmt in _EIGHT_BIT:
        return texels
    if fmt in _THIRTY_TWO_BIT:
        return texels * 4
    return texels * 2


def _native_row_bytes(fmt: TexFormat, width: int) -> int:
    if fmt in _FOUR_BIT:
        return (width + 1) // 2
    return _bytes_per_row(fmt, width)


def pad_rows(fmt: TexFormat, width: int, height: int, data: bytes) -> bytes:
    """Pads a decoded image's packed rows out to whole TMEM words."""
    src_row = _native_row_bytes(fmt, width)
    dst_row = _bytes_per_row(fmt, aligned_row_texels(fmt, width))
    out = bytearray()
    for y in range(height):
        row = data[y * src_row : (y + 1) * src_row]
        out += row + bytes(dst_row - len(row))
    return bytes(out)


def closest_rgba_index(palette: Sequence[int], r: int, g: int, b: int, a: int) -> int:
    """The game's RGBA5551 closest-entry search: an exact match if there is
    one, else a binary search over the palette by squared magnitude (which
    assumes the palette is sorted that way) and an exact distance check of
    the four entries either side of where it stops."""
    count = len(palette)
    target = (r << 11) | (g << 6) | (b << 1) | a
    for i, colour in enumerate(palette):
        if colour == target:
            return i

    low, high = 0, count - 1
    target_magnitude = r * r + g * g + b * b + a * _ALPHA_WEIGHT
    while high - low >= 2:
        mid = (high + low) >> 1
        colour = palette[mid]
        cr, cg, cb, ca = (colour >> 11) & 0x1F, (colour >> 6) & 0x1F, (colour >> 1) & 0x1F, colour & 1
        magnitude = cr * cr + cg * cg + cb * cb + ca * _ALPHA_WEIGHT
        if magnitude < target_magnitude:
            low = mid
        elif target_magnitude < magnitude:
            high = mid
        else:
            low = high = mid

    start = max(high - 4, 0)
    end = min(high + 4, count - 1)
    best_index, best_distance = 0, 999999
    for i in range(start, end + 1):
        colour = palette[i]
        dr = ((colour >> 11) & 0x1F) - r
        dg = ((colour >> 6) & 0x1F) - g
        db = ((colour >> 1) & 0x1F) - b
        distance = (0 if a == (colour & 1) else _ALPHA_WEIGHT) + dr * dr + dg * dg + db * db
        if distance < best_distance:
            best_index, best_distance = i, distance
    return best_index


def closest_ia_index(palette: Sequence[int], intensity: int, alpha: int) -> int:
    """The game's IA16 closest-entry search; same three stages as
    closest_rgba_index over (intensity, alpha)."""
    count = len(palette)
    target = ((intensity << 8) | alpha) & 0xFFFF
    for i, colour in enumerate(palette):
        if colour == target:
            return i

    low, high = 0, count - 1
    target_magnitude = intensity * intensity + alpha * alpha
    while high - low >= 2:
        mid = (high + low) >> 1
        colour = palette[mid]
        ci, ca = (colour >> 8) & 0xFF, colour & 0xFF
        magnitude = ci * ci + ca * ca
        if magnitude < target_magnitude:
            low = mid
        elif target_magnitude < magnitude:
            high = mid
        else:
            low = high = mid

    start = max(high - 4, 0)
    end = min(high + 4, count - 1)
    best_index, best_distance = 0, 999999
    for i in range(start, end + 1):
        colour = palette[i]
        di = ((colour >> 8) & 0xFF) - intensity
        da = (colour & 0xFF) - alpha
        distance = di * di + da * da
        if distance < best_distance:
            best_index, best_distance = i, distance
    return best_index


def _rgba5551_average(colours: Sequence[int]) -> tuple:
    r = (sum((c >> 11) & 0x1F for c in colours) >> 2) & 0x1F
    g = (sum((c >> 6) & 0x1F for c in colours) >> 2) & 0x1F
    b = (sum((c >> 1) & 0x1F for c in colours) >> 2) & 0x1F
    a = ((sum(c & 1 for c in colours) + 2) >> 2) & 1
    return r, g, b, a


def _ia16_average(colours: Sequence[int]) -> tuple:
    intensity = (sum((c >> 8) & 0xFF for c in colours) >> 2) & 0xFF
    alpha = ((sum(c & 0xFF for c in colours) + 1) >> 2) & 0xFF
    return intensity, alpha


def shrink_paletted(fmt: TexFormat, width: int, height: int, src: bytes, palette: Sequence[int]) -> bytes:
    """Halves a paletted level (rows TMEM-padded) by averaging each 2x2 block
    of palette colours and mapping the average back to the closest entry of
    the same palette. `palette` is the image's own entries as the game stores
    them (IA16 for the IA formats, not converted)."""
    src_aligned = aligned_row_texels(fmt, width)
    dst_aligned = aligned_row_texels(fmt, (width + 1) >> 1)
    dst_height = (height + 1) >> 1
    ia = fmt in _IA_PALETTE

    def closest(colours: Sequence[int]) -> int:
        if ia:
            return closest_ia_index(palette, *_ia16_average(colours))
        return closest_rgba_index(palette, *_rgba5551_average(colours))

    # The game reads colours from a 256-entry table holding only the image's
    # own entries; the IA quirk below can read entry 4 of a smaller palette,
    # whose contents the game leaves to whatever an earlier load put there.
    # That can't be known when packing, so those entries read as 0.
    table = list(palette) + [0] * (_PALETTE_TABLE_SIZE - len(palette))

    out = bytearray(_bytes_per_row(fmt, dst_aligned) * dst_height)

    if fmt in _EIGHT_BIT:
        for out_row, y in enumerate(range(0, height, 2)):
            row = y * src_aligned
            below = row + (src_aligned if y + 1 < height else 0)
            for x in range(0, src_aligned, 2):
                right = x + 1 if x + 1 < width else x
                colours = [table[src[row + x]], table[src[row + right]], table[src[below + x]],
                           table[src[below + right]]]
                out[out_row * dst_aligned + (x >> 1)] = closest(colours)
        return bytes(out)

    row_bytes = src_aligned >> 1
    dst_row_bytes = dst_aligned >> 1
    for out_row, y in enumerate(range(0, height, 2)):
        row = y * row_bytes
        below = row + (row_bytes if y + 1 < height else 0)
        for x in range(0, src_aligned, 4):
            byte = 0
            for pair, last in ((x, x + 1), (x + 2, x + 3)):
                top = src[row + (pair >> 1)]
                bottom = src[below + (pair >> 1)]
                if ia:
                    # The IA path's precedence slip: the shift by the in-range
                    # test feeds the ?: as its condition, so the right-hand
                    # texels read palette entry 0 or 4 instead of their own.
                    in_range = 1 if last < width else 0
                    right_top = table[0 if (top >> in_range) else 4]
                    right_bottom = table[0 if (bottom >> in_range) else 4]
                else:
                    shift = 0 if last < width else 4
                    right_top = table[(top >> shift) & 0xF]
                    right_bottom = table[(bottom >> shift) & 0xF]
                colours = [table[(top >> 4) & 0xF], right_top, table[(bottom >> 4) & 0xF], right_bottom]
                index = closest(colours)
                byte = ((index << 4) & 0xFF) if pair == x else (byte | (index & 0xFF))
            out[out_row * dst_row_bytes + (x >> 2)] = byte & 0xFF
    return bytes(out)


def shrink_non_paletted(fmt: TexFormat, width: int, height: int, src: bytes) -> bytes:
    """Halves a non-paletted level (rows TMEM-padded) by averaging each 2x2
    block, channel by channel, with the game's per-format rounding."""
    src_aligned = aligned_row_texels(fmt, width)
    dst_aligned = aligned_row_texels(fmt, (width + 1) >> 1)
    dst_height = (height + 1) >> 1
    out = bytearray(_bytes_per_row(fmt, dst_aligned) * dst_height)

    def quad(y: int, x: int):
        below_offset = src_aligned if y + 1 < height else 0
        right = x + 1 if x + 1 < width else x
        return (y * src_aligned + x, y * src_aligned + right, y * src_aligned + below_offset + x,
                y * src_aligned + below_offset + right)

    if fmt in _THIRTY_TWO_BIT:
        for out_row, y in enumerate(range(0, height, 2)):
            for x in range(0, src_aligned, 2):
                words = [struct.unpack_from(">I", src, i * 4)[0] for i in quad(y, x)]
                r = (sum((w >> 24) & 0xFF for w in words) >> 2) & 0xFF
                g = (sum((w >> 16) & 0xFF for w in words) >> 2) & 0xFF
                b = (sum((w >> 8) & 0xFF for w in words) >> 2) & 0xFF
                a = ((sum(w & 0xFF for w in words) + 1) >> 2) & 0xFF
                struct.pack_into(">I", out, (out_row * dst_aligned + (x >> 1)) * 4, (r << 24) | (g << 16) | (b << 8) | a)
        return bytes(out)

    if fmt in (TexFormat.RGBA16, TexFormat.RGB15, TexFormat.IA16):
        for out_row, y in enumerate(range(0, height, 2)):
            for x in range(0, src_aligned, 2):
                texels = [struct.unpack_from(">H", src, i * 2)[0] for i in quad(y, x)]
                if fmt == TexFormat.IA16:
                    intensity = sum((t >> 8) & 0xFF for t in texels) >> 2
                    alpha = (sum(t & 0xFF for t in texels) + 1) >> 2
                    value = ((intensity & 0xFF) << 8) | (alpha & 0xFF)
                else:
                    r, g, b, a = _rgba5551_average(texels)
                    value = (r << 11) | (g << 6) | (b << 1) | a
                struct.pack_into(">H", out, (out_row * dst_aligned + (x >> 1)) * 2, value)
        return bytes(out)

    if fmt in (TexFormat.IA8, TexFormat.I8):
        for out_row, y in enumerate(range(0, height, 2)):
            for x in range(0, src_aligned, 2):
                texels = [src[i] for i in quad(y, x)]
                if fmt == TexFormat.IA8:
                    intensity = (sum((t >> 4) & 0xF for t in texels) << 2) & 0xF0
                    alpha = ((sum(t & 0xF for t in texels) + 1) >> 2) & 0xF
                    value = intensity | alpha
                else:
                    value = (sum(texels) + 1) >> 2
                out[out_row * dst_aligned + (x >> 1)] = value & 0xFF
        return bytes(out)

    # 4-bit: each output byte comes from two source bytes in the row and the
    # two below them. The game averages the two nibbles of the left byte with
    # those of the one below for the high output nibble, and likewise for the
    # right pair, rather than forming true 2x2 blocks.
    row_bytes = src_aligned >> 1
    dst_row_bytes = dst_aligned >> 1
    for out_row, y in enumerate(range(0, height, 2)):
        row = y * row_bytes
        below = row + (row_bytes if y + 1 < height else 0)
        for x in range(0, src_aligned, 4):
            tl = src[row + (x >> 1)]
            tr = src[below + (x >> 1)]
            bl = src[row + (x >> 1) + 1]
            br = src[below + (x >> 1) + 1]
            if fmt == TexFormat.IA4:
                high = ((((tl >> 5) & 7) + ((tl >> 1) & 7) + ((tr >> 5) & 7) + ((tr >> 1) & 7)) << 3) & 0xE0
                low = ((((bl >> 5) & 7) + ((bl >> 1) & 7) + ((br >> 5) & 7) + ((br >> 1) & 7)) >> 1) & 0xE
                high_alpha = ((((tl >> 4) & 1) + (tl & 1) + ((tr >> 4) & 1) + (tr & 1) + 1) << 2) & 0x10
                low_alpha = ((((bl >> 4) & 1) + (bl & 1) + ((br >> 4) & 1) + (br & 1) + 1) >> 2) & 1
                value = high | low | high_alpha | low_alpha
            else:
                high = ((((tl >> 4) & 0xF) + (tl & 0xF) + ((tr >> 4) & 0xF) + (tr & 0xF)) << 2) & 0xF0
                low = ((((bl >> 4) & 0xF) + (bl & 0xF) + ((br >> 4) & 0xF) + (br & 0xF)) >> 2) & 0xF
                value = high | low
            out[out_row * dst_row_bytes + (x >> 2)] = value & 0xFF
    return bytes(out)


def build_mip_chain(decoded: DecodedTexture) -> Optional[List[MipLevel]]:
    """The levels the game holds for `decoded` once loaded, base level first,
    or None when it holds only the base level. The level count comes from
    the texture's header; a paletted chain stops at the level that would
    take it past PALETTED_CHAIN_LIMIT bytes."""
    if decoded.lod_count < 2:
        return None
    fmt = decoded.format
    # TODO(https://github.com/liz-baker/frigate/issues/458): a texture with explicit LODs carries its
    # own lower levels in the bitstream; until those are decoded, its chain is generated like any other.
    if fmt in _THIRTY_TWO_BIT:
        # TODO(https://github.com/liz-baker/frigate/issues/459): a 32-bit level splits across TMEM's two banks,
        # which libultraship's mip chain path doesn't read.
        return None

    width, height = decoded.width, decoded.height
    base = pad_rows(fmt, width, height, decoded.pixel_data)
    levels = [MipLevel(width, height, base)]
    total = len(base)
    paletted = fmt in _PALETTED

    for _ in range(1, decoded.lod_count):
        src = levels[-1]
        if paletted:
            data = shrink_paletted(fmt, src.width, src.height, src.data, decoded.palette_native)
            if total + len(data) > PALETTED_CHAIN_LIMIT:
                break
        else:
            data = shrink_non_paletted(fmt, src.width, src.height, src.data)
        total += len(data)
        levels.append(MipLevel((src.width + 1) >> 1, (src.height + 1) >> 1, data))

    return levels if len(levels) >= 2 else None
