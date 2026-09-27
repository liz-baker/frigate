#!/usr/bin/env python3
"""Unit tests for ge_stan.py, on synthetic stan files."""

import struct
import unittest

from ge_stan import (
    LINK_BASE_BIAS,
    NO_LINK,
    StanError,
    StanPoint,
    StanTile,
    build_stan_payload,
    pack_stan_file,
    parse_stan,
    tile_size,
)

FILE_HEADER_SIZE = 0x20


def raw_tile(tile_id, room, mid, points, triangle=(0, 1, 2)):
    """A tile as the file stores it; `points` are (x, y, z, link) with link the
    file's own encoding."""
    tail = (len(points) << 12) | (triangle[0] << 8) | (triangle[1] << 4) | triangle[2]
    out = struct.pack(">IHH", (tile_id << 8) | room, mid, tail)
    for x, y, z, link in points:
        out += struct.pack(">hhhH", x, y, z, link)
    return out


def file_link(target_offset):
    """The link value naming the tile at `target_offset`, the file offset."""
    return (target_offset - (FILE_HEADER_SIZE - LINK_BASE_BIAS)) >> 3


def build_file(tiles):
    """A stan file: the header (a null word, the first-tile pointer, a null
    terminator), then the tiles and an all-zero end tile."""
    header = struct.pack(">II", 0, FILE_HEADER_SIZE).ljust(FILE_HEADER_SIZE, b"\0")
    return header + b"".join(tiles) + b"\0" * 8


class ParseStanTest(unittest.TestCase):
    def test_links_become_tile_indices(self):
        # Tile 0 is a triangle at 0x20, tile 1 a quad after it at 0x38.
        second = FILE_HEADER_SIZE + tile_size(3)
        tiles = [
            raw_tile(0x08CC32, 0x88, 0x0FFF, [(1, 2, 3, file_link(second)), (4, 5, 6, 0), (-7, -8, -9, 0)]),
            raw_tile(0x08CC31, 0x89, 0x1000, [(0, 0, 0, file_link(FILE_HEADER_SIZE)), (1, 1, 1, 0), (2, 2, 2, 0), (3, 3, 3, 0)]),
        ]

        parsed = parse_stan(build_file(tiles))

        self.assertEqual(len(parsed), 2)
        self.assertEqual((parsed[0].id, parsed[0].room, parsed[0].mid), (0x08CC32, 0x88, 0x0FFF))
        self.assertEqual(parsed[0].points, [StanPoint(1, 2, 3, 1), StanPoint(4, 5, 6, NO_LINK), StanPoint(-7, -8, -9, NO_LINK)])
        self.assertEqual(len(parsed[1].points), 4)
        self.assertEqual(parsed[1].points[0].link, 0)
        self.assertEqual(parsed[1].tail >> 12, 4)

    def test_a_link_with_no_target_bits_is_no_link(self):
        parsed = parse_stan(build_file([raw_tile(1, 1, 0, [(0, 0, 0, 0x000F), (0, 0, 0, 0), (0, 0, 0, 0)])]))
        self.assertEqual([p.link for p in parsed[0].points], [NO_LINK] * 3)

    def test_rejects_a_link_that_is_not_a_tile(self):
        tiles = [raw_tile(1, 1, 0, [(0, 0, 0, file_link(FILE_HEADER_SIZE) + 1), (0, 0, 0, 0), (0, 0, 0, 0)])]
        with self.assertRaises(StanError):
            parse_stan(build_file(tiles))

    def test_rejects_a_tile_that_runs_off_the_end(self):
        data = build_file([raw_tile(1, 1, 0, [(0, 0, 0, 0)] * 3)])
        with self.assertRaises(StanError):
            parse_stan(data[: FILE_HEADER_SIZE + 12])

    def test_rejects_a_missing_terminator(self):
        data = build_file([raw_tile(1, 1, 0, [(0, 0, 0, 0)] * 3)])
        with self.assertRaises(StanError):
            parse_stan(data[:-8])

    def test_rejects_a_bad_point_count(self):
        with self.assertRaises(StanError):
            parse_stan(build_file([raw_tile(1, 1, 0, [(0, 0, 0, 0)] * 2)]))

    def test_an_empty_file_has_no_tiles(self):
        self.assertEqual(parse_stan(build_file([])), [])


class BuildStanPayloadTest(unittest.TestCase):
    def test_layout(self):
        tile = StanTile(0x08CC32, 0x88, 0x0FFF, 0x3012, [StanPoint(1, -2, 3, 5), StanPoint(0, 0, 0, NO_LINK), StanPoint(9, 9, 9, 0)])

        payload = build_stan_payload([tile])

        self.assertEqual(payload[:4], struct.pack(">I", 1))
        self.assertEqual(payload[4:12], struct.pack(">IHH", 0x08CC3288, 0x0FFF, 0x3012))
        self.assertEqual(payload[12:20], struct.pack(">hhhH", 1, -2, 3, 5))
        self.assertEqual(struct.unpack(">H", payload[26:28])[0], NO_LINK)
        self.assertEqual(len(payload), 4 + 8 + 3 * 8)

    def test_pack_stan_file_round_trips_a_file(self):
        second = FILE_HEADER_SIZE + tile_size(3)
        data = build_file(
            [
                raw_tile(2, 3, 0x0FFF, [(0, 0, 0, file_link(second)), (5, 0, 0, 0), (0, 0, 5, 0)]),
                raw_tile(4, 3, 0x0FFF, [(5, 0, 0, file_link(FILE_HEADER_SIZE)), (5, 0, 5, 0), (0, 0, 5, 0)]),
            ]
        )

        payload = pack_stan_file(data)

        self.assertEqual(struct.unpack(">I", payload[:4])[0], 2)
        # The second tile's first point links back to tile 0.
        self.assertEqual(struct.unpack(">H", payload[4 + 32 + 8 + 6 : 4 + 32 + 8 + 8])[0], 0)


if __name__ == "__main__":
    unittest.main()
