#!/usr/bin/env python3
"""Unit tests for ge_texture.py, against hand-built synthetic bitstreams
-- no GoldenEye ROM or real assets needed, matching this package's
existing testing convention (see README.md's Testing section)."""

import struct
import unittest
import zlib

from ge_texture import (
    GeTextureFormatError,
    TexFormat,
    _BitReader,
    _blur,
    _build_huffman_tree,
    decode_texture,
)


class _BitWriter:
    """Test-only companion to `_BitReader`: MSB-first bit writer, used to
    hand-build valid GoldenEye texture bitstreams for round-trip tests."""

    def __init__(self):
        self._bits = []

    def write(self, value: int, num_bits: int) -> "_BitWriter":
        for i in range(num_bits - 1, -1, -1):
            self._bits.append((value >> i) & 1)
        return self

    def extend(self, other: "_BitWriter") -> "_BitWriter":
        self._bits.extend(other._bits)
        return self

    def bytes(self) -> bytes:
        padded = self._bits + [0] * ((-len(self._bits)) % 8)
        out = bytearray()
        for i in range(0, len(padded), 8):
            byte = 0
            for bit in padded[i : i + 8]:
                byte = (byte << 1) | bit
            out.append(byte)
        return bytes(out)


def _texture_header(fmt_or_paletted: bool, has_explicit_lods: int = 0, lod: int = 0) -> _BitWriter:
    w = _BitWriter()
    w.write(has_explicit_lods, 1)
    w.write(1 if fmt_or_paletted else 0, 1)
    w.write(lod, 6)
    return w


class BitReaderTest(unittest.TestCase):
    def test_reads_msb_first_across_byte_boundaries(self):
        reader = _BitReader(bytes([0b10110010, 0b01010101]))
        self.assertEqual(reader.read(3), 0b101)
        self.assertEqual(reader.read(5), 0b10010)
        self.assertEqual(reader.read(8), 0b01010101)

    def test_raises_on_exhausted_data(self):
        reader = _BitReader(bytes([0xFF]))
        reader.read(8)
        with self.assertRaises(GeTextureFormatError):
            reader.read(1)


class DecodeUncompressedTest(unittest.TestCase):
    def _build_uncompressed(self, fmt: TexFormat, width: int, height: int, pixel_writer) -> bytes:
        w = _texture_header(False)
        w.write(fmt.value, 4)
        w.write(width, 8)
        w.write(height, 8)
        w.write(0, 4)  # UNCOMPRESSED0
        pixel_writer(w)
        return w.bytes()

    def test_rgba32_round_trip(self):
        pixels = [(0x11, 0x22, 0x33, 0x44), (0xAA, 0xBB, 0xCC, 0xDD)]

        def write_pixels(w):
            for r, g, b, a in pixels:
                w.write(r, 8)
                w.write(g, 8)
                w.write(b, 8)
                w.write(a, 8)

        data = self._build_uncompressed(TexFormat.RGBA32, 2, 1, write_pixels)
        decoded = decode_texture(data)

        self.assertEqual(decoded.format, TexFormat.RGBA32)
        self.assertEqual((decoded.width, decoded.height), (2, 1))
        self.assertEqual(decoded.texture_type, 1)  # RGBA32bpp
        self.assertEqual(decoded.pixel_data, b"".join(bytes(p) for p in pixels))
        self.assertIsNone(decoded.palette_rgba5551)

    def test_rgba16_round_trip(self):
        # 5-bit R,G,B + 1-bit A per pixel.
        pixels = [(0x1F, 0x00, 0x0A, 1), (0x00, 0x1F, 0x00, 0)]

        def write_pixels(w):
            for r, g, b, a in pixels:
                w.write(r, 5)
                w.write(g, 5)
                w.write(b, 5)
                w.write(a, 1)

        data = self._build_uncompressed(TexFormat.RGBA16, 2, 1, write_pixels)
        decoded = decode_texture(data)

        expected = b"".join(
            struct.pack(">H", (r << 11) | (g << 6) | (b << 1) | a) for r, g, b, a in pixels
        )
        self.assertEqual(decoded.texture_type, 2)  # RGBA16bpp
        self.assertEqual(decoded.pixel_data, expected)

    def test_ia8_round_trip(self):
        pixels = [(0xA, 0x5), (0x1, 0xF)]

        def write_pixels(w):
            for i, a in pixels:
                w.write(i, 4)
                w.write(a, 4)

        data = self._build_uncompressed(TexFormat.IA8, 2, 1, write_pixels)
        decoded = decode_texture(data)

        expected = bytes(((i << 4) | a) for i, a in pixels)
        self.assertEqual(decoded.texture_type, 8)  # GrayscaleAlpha8bpp
        self.assertEqual(decoded.pixel_data, expected)

    def test_i4_round_trip_odd_width_pads_last_nibble(self):
        # 3 pixels -> 2 output bytes, last nibble padded with 0.
        values = [0x1, 0xE, 0x7]

        def write_pixels(w):
            for v in values:
                w.write(v, 4)

        data = self._build_uncompressed(TexFormat.I4, 3, 1, write_pixels)
        decoded = decode_texture(data)

        self.assertEqual(decoded.texture_type, 5)  # Grayscale4bpp
        self.assertEqual(decoded.pixel_data, bytes([(0x1 << 4) | 0xE, (0x7 << 4) | 0x0]))

    def test_i4_odd_width_resets_nibble_packing_per_row(self):
        # 3x2 image: each row must start a fresh byte (ceil(3/2)=2
        # bytes/row), not carry a nibble across the row boundary.
        rows = [[0x1, 0x2, 0x3], [0x4, 0x5, 0x6]]

        def write_pixels(w):
            for row in rows:
                for v in row:
                    w.write(v, 4)

        data = self._build_uncompressed(TexFormat.I4, 3, 2, write_pixels)
        decoded = decode_texture(data)

        expected = bytes([0x12, 0x30, 0x45, 0x60])
        self.assertEqual(decoded.pixel_data, expected)

    def test_ia4_round_trip(self):
        # 3-bit intensity + 1-bit alpha per pixel.
        pixels = [(0x5, 1), (0x2, 0)]

        def write_pixels(w):
            for i, a in pixels:
                w.write(i, 3)
                w.write(a, 1)

        data = self._build_uncompressed(TexFormat.IA4, 2, 1, write_pixels)
        decoded = decode_texture(data)

        expected_byte = ((pixels[0][0] << 1 | pixels[0][1]) << 4) | (pixels[1][0] << 1 | pixels[1][1])
        self.assertEqual(decoded.texture_type, 7)  # GrayscaleAlpha4bpp
        self.assertEqual(decoded.pixel_data, bytes([expected_byte]))

    def test_rgb24_forces_alpha_0xff(self):
        def write_pixels(w):
            w.write(0x10, 8)
            w.write(0x20, 8)
            w.write(0x30, 8)

        data = self._build_uncompressed(TexFormat.RGB24, 1, 1, write_pixels)
        decoded = decode_texture(data)

        self.assertEqual(decoded.texture_type, 1)  # RGBA32bpp
        self.assertEqual(decoded.pixel_data, bytes([0x10, 0x20, 0x30, 0xFF]))

    def test_rgb15_forces_alpha_bit(self):
        def write_pixels(w):
            w.write(0x1F, 5)
            w.write(0x00, 5)
            w.write(0x0A, 5)

        data = self._build_uncompressed(TexFormat.RGB15, 1, 1, write_pixels)
        decoded = decode_texture(data)

        expected = struct.pack(">H", (0x1F << 11) | (0x00 << 6) | (0x0A << 1) | 1)
        self.assertEqual(decoded.pixel_data, expected)


class DecodeHuffmanTest(unittest.TestCase):
    def _encode_symbols(self, frequencies, alphabet_size, symbols):
        root, children = _build_huffman_tree(list(frequencies), alphabet_size)
        codes = {}

        def walk(node, path):
            if node >= 10000:
                codes[node - 10000] = path
                return
            walk(children[node][0], path + [0])
            walk(children[node][1], path + [1])

        walk(root, [])

        w = _BitWriter()
        for f in frequencies:
            w.write(f, 8)
        for s in symbols:
            for bit in codes[s]:
                w.write(bit, 1)
        return w

    def test_huffman_channel_round_trip(self):
        # Small alphabet (I4 format: chansize=16) so the tree is cheap to
        # hand-verify; symbols chosen to exercise multiple leaves.
        alphabet_size = 16
        frequencies = [0] * alphabet_size
        frequencies[1] = 10
        frequencies[2] = 5
        frequencies[3] = 1
        symbols = [1, 2, 1, 3, 1, 2]  # I4 has 1 channel, 2x3 image

        body = self._encode_symbols(frequencies, alphabet_size, symbols)

        w = _texture_header(False)
        w.write(TexFormat.I4.value, 4)
        w.write(3, 8)  # width
        w.write(2, 8)  # height
        w.write(2, 4)  # HUFFMAN
        w.extend(body)
        data = w.bytes()

        decoded = decode_texture(data)

        # width=3 is odd, so each 2-row packs to ceil(3/2)=2 bytes with
        # the last nibble padded, not a flat 3-byte run across rows.
        expected = bytes(
            [
                (symbols[0] << 4) | symbols[1],
                (symbols[2] << 4) | 0,
                (symbols[3] << 4) | symbols[4],
                (symbols[5] << 4) | 0,
            ]
        )
        self.assertEqual(decoded.pixel_data, expected)


class DecodeRleTest(unittest.TestCase):
    def _build_rle_stream(self, back_bits, run_bits, value_bits, directives):
        """`directives` is a list of either ("lit", value) or
        ("run", back_distance, run_length, trailing_literal) matching
        `_inflate_rle`'s format: a run directive's copy is always
        immediately followed by one literal value with no directive bit
        of its own (`_inflate_rle` reads it unconditionally after the
        copy loop) -- so that trailing value is part of the run tuple,
        not a separate top-level directive."""
        w = _BitWriter()
        w.write(back_bits, 3)
        w.write(run_bits, 3)
        w.write(value_bits, 4)
        for d in directives:
            if d[0] == "lit":
                w.write(0, 1)
                w.write(d[1], value_bits)
            else:
                _, back_distance, run_length_field, trailing_literal = d
                w.write(1, 1)
                w.write(back_distance, back_bits)
                w.write(run_length_field, run_bits)
                w.write(trailing_literal, value_bits)
        return w

    def test_rle_literal_only_round_trip(self):
        # I8 format (1 channel, chansize=256): all-literal stream, the
        # simplest correct RLE encoding of arbitrary data.
        values = [5, 250, 0, 128]
        body = self._build_rle_stream(3, 3, 8, [("lit", v) for v in values])

        w = _texture_header(False)
        w.write(TexFormat.I8.value, 4)
        w.write(4, 8)
        w.write(1, 8)
        w.write(4, 4)  # RLE
        w.extend(body)
        data = w.bytes()

        decoded = decode_texture(data)
        self.assertEqual(decoded.pixel_data, bytes(values))

    def test_rle_run_directive_copies_back_reference(self):
        # fudge for back_bits=0,run_bits=0,value_bits=8: cost=0+0+8+1=9,
        # each iteration subtracts (value_bits+1)=9 -> fudge=1.
        # A run with run_length_field=0 copies (0+fudge)=1 value.
        body = self._build_rle_stream(
            0,
            0,
            8,
            [
                ("lit", 42),
                # copies 1 value (back-distance 0 == the literal above),
                # then its own mandatory trailing literal (99) -- no
                # separate directive bit for that trailing value.
                ("run", 0, 0, 99),
            ],
        )

        w = _texture_header(False)
        w.write(TexFormat.I8.value, 4)
        w.write(3, 8)
        w.write(1, 8)
        w.write(4, 4)  # RLE
        w.extend(body)
        data = w.bytes()

        decoded = decode_texture(data)
        # literal 42, then a 1-value run copying [42] -> 42, then the run's mandatory trailing literal 99
        self.assertEqual(decoded.pixel_data, bytes([42, 42, 99]))


class DecodeLookupTest(unittest.TestCase):
    def test_lookup_round_trip(self):
        table = [10, 200, 55]  # I8: 8 bits/entry
        indices = [0, 2, 1, 0]

        w = _texture_header(False)
        w.write(TexFormat.I8.value, 4)
        w.write(4, 8)
        w.write(1, 8)
        w.write(5, 4)  # LOOKUP
        w.write(len(table), 11)
        for v in table:
            w.write(v, 8)
        bits_per_index = (len(table) - 1).bit_length()
        for i in indices:
            w.write(i, bits_per_index)
        data = w.bytes()

        decoded = decode_texture(data)
        self.assertEqual(decoded.pixel_data, bytes(table[i] for i in indices))

    def test_lookup_round_trip_4bit_format(self):
        table = [0x3, 0xC]
        indices = [1, 0, 1]

        w = _texture_header(False)
        w.write(TexFormat.I4.value, 4)
        w.write(3, 8)
        w.write(1, 8)
        w.write(5, 4)  # LOOKUP
        w.write(len(table), 11)
        for v in table:
            w.write(v, 4)
        bits_per_index = (len(table) - 1).bit_length()
        for i in indices:
            w.write(i, bits_per_index)
        data = w.bytes()

        decoded = decode_texture(data)
        expected = bytes([(table[indices[0]] << 4) | table[indices[1]], table[indices[2]] << 4])
        self.assertEqual(decoded.pixel_data, expected)


class BlurTest(unittest.TestCase):
    def test_predicts_from_already_reconstructed_neighbours(self):
        # Method 0 (left prediction) over a row of equal deltas is a
        # running sum; reading the un-filtered row instead would give
        # [1, 2, 2].
        self.assertEqual(_blur([1, 1, 1], 3, 1, 0, 16), [1, 2, 3])

    def test_half_truncates_toward_zero_like_c(self):
        # Method 4 at (1,1): above=4, above_left=9, so (above - above_left)
        # is -5; C's -5 / 2 is -2 where Python's -5 // 2 would be -3.
        self.assertEqual(_blur([9, 251, 0, 0], 2, 2, 4, 256), [9, 4, 4, 2])


class DecodeSingleEntryLookupTest(unittest.TestCase):
    def test_single_entry_table_reads_no_index_bits(self):
        w = _texture_header(False)
        w.write(TexFormat.I8.value, 4)
        w.write(3, 8)
        w.write(1, 8)
        w.write(5, 4)  # LOOKUP
        w.write(1, 11)
        w.write(0x7F, 8)
        data = w.bytes()

        decoded = decode_texture(data)
        self.assertEqual(decoded.pixel_data, bytes([0x7F, 0x7F, 0x7F]))


class DecodePalettedTest(unittest.TestCase):
    def _rzip_compress(self, data: bytes) -> bytes:
        co = zlib.compressobj(9, zlib.DEFLATED, -15)
        return b"\x11\x72" + co.compress(data) + co.flush()

    def test_ci8_round_trip(self):
        palette = [0b1111100000000001, 0b0000011111000001]  # 2 RGBA5551 colours
        indices = [0, 1, 1, 0]  # 2x2 image

        w = _texture_header(True)
        w.write(TexFormat.RGBA16_CI8.value, 8)
        w.write(len(palette) - 1, 8)
        for c in palette:
            w.write(c, 16)
        w.write(2, 8)  # width
        w.write(2, 8)  # height
        header_bytes = w.bytes()

        deflated = self._rzip_compress(bytes(indices))
        data = header_bytes + deflated

        decoded = decode_texture(data)

        self.assertEqual(decoded.format, TexFormat.RGBA16_CI8)
        self.assertEqual(decoded.texture_type, 4)  # Palette8bpp
        self.assertEqual(decoded.pixel_data, bytes(indices))
        self.assertEqual(
            decoded.palette_rgba5551,
            b"".join(struct.pack(">H", c) for c in palette),
        )

    def test_ci4_round_trip_packs_two_indices_per_byte(self):
        palette = [0x1234, 0x5678, 0x9ABC, 0xDEF1]
        # Already nibble-packed, matching GE's native CI4 wire format:
        # pixel pairs (0,1) and (2,3) for a 4x1 image.
        packed_indices = bytes([0x01, 0x23])

        w = _texture_header(True)
        w.write(TexFormat.RGBA16_CI4.value, 8)
        w.write(len(palette) - 1, 8)
        for c in palette:
            w.write(c, 16)
        w.write(4, 8)  # width
        w.write(1, 8)  # height
        header_bytes = w.bytes()

        deflated = self._rzip_compress(packed_indices)
        data = header_bytes + deflated

        decoded = decode_texture(data)

        self.assertEqual(decoded.texture_type, 3)  # Palette4bpp
        self.assertEqual(decoded.pixel_data, packed_indices)

    def test_ia16_palette_normalized_to_rgba5551(self):
        # IA16 palette entry: intensity=0xF8 (>>3 == 0x1F), alpha=0xFF (>=128 -> bit 1)
        ia16_entry = (0xF8 << 8) | 0xFF

        w = _texture_header(True)
        w.write(TexFormat.IA16_CI8.value, 8)
        w.write(0, 8)  # numcolours - 1 == 0 -> 1 colour
        w.write(ia16_entry, 16)
        w.write(1, 8)
        w.write(1, 8)
        header_bytes = w.bytes()

        deflated = self._rzip_compress(bytes([0]))
        data = header_bytes + deflated

        decoded = decode_texture(data)

        expected_rgba5551 = (0x1F << 11) | (0x1F << 6) | (0x1F << 1) | 1
        self.assertEqual(decoded.palette_rgba5551, struct.pack(">H", expected_rgba5551))

    def test_rejects_non_paletted_format_in_paletted_branch(self):
        w = _texture_header(True)
        w.write(TexFormat.RGBA32.value, 8)
        data = w.bytes()
        with self.assertRaises(GeTextureFormatError):
            decode_texture(data)


class DecodeErrorHandlingTest(unittest.TestCase):
    def test_empty_data_raises(self):
        with self.assertRaises(GeTextureFormatError):
            decode_texture(b"")


if __name__ == "__main__":
    unittest.main()
