#!/usr/bin/env python3
"""Unit tests for ge_mipmap.py: the game's level generation, checked
against values worked out by hand from its averaging and rounding rules."""

import struct
import unittest

from ge_mipmap import (
    PALETTED_CHAIN_LIMIT,
    aligned_row_texels,
    build_mip_chain,
    closest_ia_index,
    closest_rgba_index,
    pad_rows,
    shrink_non_paletted,
    shrink_paletted,
)
from ge_texture import DecodedTexture, TexFormat


def rgba5551(r: int, g: int, b: int, a: int) -> int:
    return (r << 11) | (g << 6) | (b << 1) | a


def rgba16_image(texels, width: int, height: int) -> bytes:
    """Packs RGBA5551 texels into TMEM-padded rows."""
    aligned = aligned_row_texels(TexFormat.RGBA16, width)
    out = bytearray(aligned * height * 2)
    for y in range(height):
        for x in range(width):
            struct.pack_into(">H", out, (y * aligned + x) * 2, texels[y * width + x])
    return bytes(out)


def texel16(data: bytes, aligned: int, x: int, y: int) -> int:
    return struct.unpack_from(">H", data, (y * aligned + x) * 2)[0]


class AlignmentTest(unittest.TestCase):
    def test_rows_pad_to_whole_tmem_words(self):
        self.assertEqual(aligned_row_texels(TexFormat.RGBA16, 5), 8)
        self.assertEqual(aligned_row_texels(TexFormat.I8, 9), 16)
        self.assertEqual(aligned_row_texels(TexFormat.RGBA16_CI4, 5), 16)
        self.assertEqual(aligned_row_texels(TexFormat.RGBA32, 1), 4)

    def test_pad_rows_keeps_each_row_at_its_padded_offset(self):
        # CI4, width 5: three packed bytes a row, padded to 8.
        padded = pad_rows(TexFormat.RGBA16_CI4, 5, 2, bytes([1, 2, 3, 4, 5, 6]))
        self.assertEqual(padded, bytes([1, 2, 3, 0, 0, 0, 0, 0, 4, 5, 6, 0, 0, 0, 0, 0]))


class ShrinkNonPalettedTest(unittest.TestCase):
    def test_rgba16_averages_down_and_rounds_alpha_half_up(self):
        texels = [rgba5551(31, 8, 1, 1), rgba5551(31, 8, 2, 1), rgba5551(0, 8, 3, 0), rgba5551(0, 9, 4, 0)]
        out = shrink_non_paletted(TexFormat.RGBA16, 2, 2, rgba16_image(texels, 2, 2))
        # One row of four padded texels.
        self.assertEqual(len(out), 8)
        # r 62/4 -> 15, g 33/4 -> 8, b 10/4 -> 2, alpha (2 + 2) / 4 -> 1.
        self.assertEqual(texel16(out, 4, 0, 0), rgba5551(15, 8, 2, 1))

    def test_rgba16_alpha_needs_two_of_four(self):
        texels = [rgba5551(0, 0, 0, 1), 0, 0, 0]
        out = shrink_non_paletted(TexFormat.RGBA16, 2, 2, rgba16_image(texels, 2, 2))
        self.assertEqual(texel16(out, 4, 0, 0) & 1, 0)

    def test_odd_width_samples_the_last_column_twice(self):
        # 3x1: the second output texel averages column 2 with itself, and the
        # missing row below is the same row again.
        texels = [rgba5551(0, 0, 0, 1), rgba5551(0, 0, 0, 1), rgba5551(20, 0, 0, 1)]
        out = shrink_non_paletted(TexFormat.RGBA16, 3, 1, rgba16_image(texels, 3, 1))
        self.assertEqual(texel16(out, 4, 1, 0), rgba5551(20, 0, 0, 1))

    def test_i8_rounds_half_up(self):
        src = pad_rows(TexFormat.I8, 2, 2, bytes([1, 2, 3, 4]))
        out = shrink_non_paletted(TexFormat.I8, 2, 2, src)
        self.assertEqual(out[0], (1 + 2 + 3 + 4 + 1) >> 2)
        self.assertEqual(len(out), 8)

    def test_ia8_scales_intensity_and_rounds_alpha(self):
        src = pad_rows(TexFormat.IA8, 2, 2, bytes([0xF1, 0xF1, 0x10, 0x10]))
        out = shrink_non_paletted(TexFormat.IA8, 2, 2, src)
        # Intensity (15 + 15 + 1 + 1) << 2 = 128 -> 0x80; alpha (1 + 1 + 0 + 0 + 1) >> 2 = 0.
        self.assertEqual(out[0], 0x80)

    def test_ia16_averages_both_bytes(self):
        src = pad_rows(TexFormat.IA16, 2, 2, bytes([0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0x00, 0x00]))
        out = shrink_non_paletted(TexFormat.IA16, 2, 2, src)
        # Intensity 510 / 4 = 127; alpha (511 + 1) / 4 = 128.
        self.assertEqual(struct.unpack_from(">H", out, 0)[0], (127 << 8) | 128)

    def test_i4_pairs_bytes_the_way_the_game_does(self):
        # Width 4: two bytes a row. The high output nibble comes from both
        # nibbles of the left byte in both rows, the low one from the right byte.
        src = pad_rows(TexFormat.I4, 4, 2, bytes([0x31, 0x22, 0x11, 0x44]))
        out = shrink_non_paletted(TexFormat.I4, 4, 2, src)
        high = ((3 + 1 + 1 + 1) << 2) & 0xF0
        low = ((2 + 2 + 4 + 4) >> 2) & 0xF
        self.assertEqual(out[0], high | low)

    def test_ia4_keeps_the_games_bit_layout(self):
        src = pad_rows(TexFormat.IA4, 4, 2, bytes([0xFF, 0x00, 0xFF, 0x00]))
        out = shrink_non_paletted(TexFormat.IA4, 4, 2, src)
        high = ((7 + 7 + 7 + 7) << 3) & 0xE0
        high_alpha = ((1 + 1 + 1 + 1 + 1) << 2) & 0x10
        low_alpha = ((0 + 0 + 0 + 0 + 1) >> 2) & 1
        self.assertEqual(out[0], high | high_alpha | low_alpha)

    def test_level_size_is_padded_rows_times_halved_height(self):
        out = shrink_non_paletted(TexFormat.RGBA16, 8, 5, rgba16_image([0] * 40, 8, 5))
        self.assertEqual(len(out), 3 * aligned_row_texels(TexFormat.RGBA16, 4) * 2)


class ClosestColourTest(unittest.TestCase):
    # Sorted by squared magnitude, as the search assumes.
    PALETTE = [rgba5551(0, 0, 0, 1), rgba5551(8, 8, 8, 1), rgba5551(16, 16, 16, 1), rgba5551(31, 31, 31, 1)]

    def test_exact_match(self):
        self.assertEqual(closest_rgba_index(self.PALETTE, 16, 16, 16, 1), 2)

    def test_nearest_entry(self):
        self.assertEqual(closest_rgba_index(self.PALETTE, 9, 7, 9, 1), 1)

    def test_alpha_mismatch_outweighs_colour(self):
        # Same colour with the wrong alpha costs 961; (20, 20, 20) with the right one costs 300.
        palette = [rgba5551(10, 10, 10, 1), rgba5551(20, 20, 20, 0)]
        self.assertEqual(closest_rgba_index(palette, 10, 10, 10, 0), 1)

    def test_ia_nearest_entry(self):
        palette = [0x0000, 0x40FF, 0x80FF, 0xFFFF]
        self.assertEqual(closest_ia_index(palette, 0x42, 0xF0), 1)
        self.assertEqual(closest_ia_index(palette, 0x80, 0xFF), 2)


class ShrinkPalettedTest(unittest.TestCase):
    PALETTE = [rgba5551(0, 0, 0, 1), rgba5551(31, 31, 31, 1), rgba5551(15, 15, 15, 1)]

    def test_ci8_maps_the_average_back_into_the_palette(self):
        src = pad_rows(TexFormat.RGBA16_CI8, 2, 2, bytes([0, 1, 0, 1]))
        out = shrink_paletted(TexFormat.RGBA16_CI8, 2, 2, src, self.PALETTE)
        # (0 + 31 + 0 + 31) / 4 = 15: palette entry 2.
        self.assertEqual(out[0], 2)
        self.assertEqual(len(out), 8)

    def test_ci4_packs_two_indices_a_byte(self):
        # 4x2: left pair black/white in both rows, right pair white/white.
        src = pad_rows(TexFormat.RGBA16_CI4, 4, 2, bytes([0x01, 0x11, 0x01, 0x11]))
        out = shrink_paletted(TexFormat.RGBA16_CI4, 4, 2, src, self.PALETTE)
        self.assertEqual(out[0], (2 << 4) | 1)

    def test_ia_ci4_right_texels_read_entry_0_or_4(self):
        # The IA path's precedence slip: a nonzero source byte reads entry 0
        # for its right-hand texels, whatever its low nibble.
        palette = [0x0000] + [0xFFFF] * 15
        src = pad_rows(TexFormat.IA16_CI4, 4, 2, bytes([0x11, 0x11, 0x11, 0x11]))
        out = shrink_paletted(TexFormat.IA16_CI4, 4, 2, src, palette)
        # Each block averages two 0xFFFF entries with two entry-0 reads: intensity
        # 510 / 4 = 127, alpha (510 + 1) / 4 = 127. Index 0 (distance 2 * 127^2)
        # beats the 0xFFFF entries (distance 2 * 128^2).
        self.assertEqual(out[0], 0x00)


    def test_ia_ci4_entry_4_of_a_smaller_palette_reads_as_zero(self):
        # A source byte of 1 shifts to 0, so the right-hand texels read entry
        # 4, past this two-entry palette.
        palette = [0x0000, 0xFFFF]
        src = pad_rows(TexFormat.IA16_CI4, 4, 2, bytes([0x01, 0x01, 0x01, 0x01]))
        out = shrink_paletted(TexFormat.IA16_CI4, 4, 2, src, palette)
        self.assertEqual(out[0], 0x00)

def decoded(fmt, width, height, pixel_data, lod_count, palette=None):
    return DecodedTexture(
        format=fmt,
        width=width,
        height=height,
        texture_type=0,
        pixel_data=pixel_data,
        palette_native=palette,
        lod_count=lod_count,
    )


class BuildMipChainTest(unittest.TestCase):
    def test_no_chain_below_two_levels(self):
        self.assertIsNone(build_mip_chain(decoded(TexFormat.I8, 4, 4, bytes(16), 0)))
        self.assertIsNone(build_mip_chain(decoded(TexFormat.I8, 4, 4, bytes(16), 1)))

    def test_chain_has_the_headers_level_count_with_halved_sizes(self):
        chain = build_mip_chain(decoded(TexFormat.RGBA16, 8, 8, bytes(128), 4))
        self.assertEqual([(level.width, level.height) for level in chain], [(8, 8), (4, 4), (2, 2), (1, 1)])
        # Padded rows: 16 + 8 + 8 + 8 bytes, times the level heights.
        self.assertEqual([len(level.data) for level in chain], [128, 32, 16, 8])

    def test_odd_sizes_round_up_like_the_game(self):
        chain = build_mip_chain(decoded(TexFormat.I8, 5, 3, bytes(15), 3))
        self.assertEqual([(level.width, level.height) for level in chain], [(5, 3), (3, 2), (2, 1)])

    def test_paletted_chain_stops_before_the_limit(self):
        palette = [rgba5551(0, 0, 0, 1)]
        # 40x40 CI8: 1600 bytes, and the 20x20 level (rows padded to 24) adds 480.
        self.assertGreater(1600 + 480, PALETTED_CHAIN_LIMIT)
        self.assertIsNone(build_mip_chain(decoded(TexFormat.RGBA16_CI8, 40, 40, bytes(1600), 4, palette)))

        chain = build_mip_chain(decoded(TexFormat.RGBA16_CI8, 32, 32, bytes(1024), 6, palette))
        self.assertEqual([(level.width, level.height) for level in chain], [(32, 32), (16, 16), (8, 8), (4, 4),
                                                                           (2, 2), (1, 1)])

    def test_paletted_chain_truncates_at_the_level_that_overflows(self):
        palette = [rgba5551(0, 0, 0, 1)]
        # 32x48 CI8: 1536 + 384 (16x24) = 1920 fits, the 8x12 level (96) does too,
        # but the 4x6 level padded to 8 (48) takes it to 2064.
        chain = build_mip_chain(decoded(TexFormat.RGBA16_CI8, 32, 48, bytes(1536), 5, palette))
        self.assertEqual(len(chain), 3)

    def test_32_bit_images_get_no_chain(self):
        self.assertIsNone(build_mip_chain(decoded(TexFormat.RGBA32, 4, 4, bytes(64), 3)))


if __name__ == "__main__":
    unittest.main()
