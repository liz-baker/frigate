import struct
import unittest

from ge_container import BgRoom, ModelImage
from ge_displaylist import G_ENDDL, G_VTX, VTX_SIZE
from ge_model import NODE_DL, NODE_DL_PRIMARY, ModelCounts, parse_model_tree
from ge_vertex import (
    BgVertexKey,
    GeVertexError,
    ModelVertexKey,
    Vertex,
    decode_vertices,
    encode_vertex_payload,
    extract_bg_vertices,
    extract_model_vertices,
)

SEG5 = 0x05000000


def cmd(w0, w1=0):
    return struct.pack(">II", w0, w1)


def load(count, addr):
    return cmd((G_VTX << 24) | (count * VTX_SIZE), addr)


END = cmd(G_ENDDL << 24)


def raw_vertex(seed):
    return struct.pack(">3hH2h4B", -seed, seed, 2 * seed, 0, seed + 1, -seed - 1, seed, 255 - seed, 7, 255)


def raw_vertices(count, first=1):
    return b"".join(raw_vertex(first + i) for i in range(count))


def node(node_type, data):
    return struct.pack(">HHIIIII", node_type, 0, SEG5 + data, 0, 0, 0, 0)


class DecodeTest(unittest.TestCase):
    def test_fields(self):
        (v,) = decode_vertices(bytes.fromhex("fff6000a0014" "8001" "ff9c0064" "10ff2001"))
        self.assertEqual((v.x, v.y, v.z), (-10, 10, 20))
        self.assertEqual(v.flag, 0x8001)
        self.assertEqual((v.s, v.t), (-100, 100))
        self.assertEqual((v.r, v.g, v.b, v.a), (0x10, 0xFF, 0x20, 0x01))

    def test_rejects_partial_record(self):
        with self.assertRaises(GeVertexError):
            decode_vertices(b"\0" * 20)

    def test_payload_is_count_then_original_bytes(self):
        raw = raw_vertices(3)
        self.assertEqual(encode_vertex_payload(decode_vertices(raw)), struct.pack(">I", 3) + raw)


class ModelExtractTest(unittest.TestCase):
    def build(self, node_type, record, lists, vertices):
        """Root at 0 (no switches or textures), then its record, lists, vertices."""
        root_at = 0
        record_at = 0x18
        lists_at = record_at + len(record)
        vertices_at = lists_at + len(lists)
        blob = node(node_type, record_at) + record + lists + vertices
        return ModelImage(blob), record_at, lists_at, vertices_at

    def test_dl_node_uses_stored_count(self):
        lists = load(2, 0x04000000) + END
        # record: primary, secondary, ?, vertices, count
        vertices_at = 0x18 + 0x14 + len(lists)
        record = struct.pack(">IIIIH", SEG5 + 0x18 + 0x14, 0, 0, SEG5 + vertices_at, 2).ljust(0x14, b"\0")
        image, *_ = self.build(NODE_DL, record, lists, raw_vertices(3))
        tree = parse_model_tree(image, ModelCounts(0, 0))
        (block,) = extract_model_vertices(image, tree)
        self.assertEqual(block.key, ModelVertexKey(0))
        self.assertEqual(len(block.vertices), 2)  # the count, not the 3 present
        self.assertEqual(block.vertices[0], decode_vertices(raw_vertex(1))[0])

    def test_primary_only_node_loads_four_times_count(self):
        lists = load(4, 0x04000000) + END
        vertices_at = 0x18 + 0x0C + len(lists)
        record = struct.pack(">iII", 1, SEG5 + vertices_at, SEG5 + 0x18 + 0x0C)
        image, *_ = self.build(NODE_DL_PRIMARY, record, lists, raw_vertices(4))
        (block,) = extract_model_vertices(image, parse_model_tree(image, ModelCounts(0, 0)))
        self.assertEqual(len(block.vertices), 4)

    def test_zero_count_sized_from_lists(self):
        lists = load(2, 0x04000000) + load(3, 0x04000010) + END  # window ends at vertex 4
        vertices_at = 0x18 + 0x14 + len(lists)
        record = struct.pack(">IIIIH", SEG5 + 0x18 + 0x14, 0, 0, SEG5 + vertices_at, 0).ljust(0x14, b"\0")
        image, *_ = self.build(NODE_DL, record, lists, raw_vertices(6))
        (block,) = extract_model_vertices(image, parse_model_tree(image, ModelCounts(0, 0)))
        self.assertEqual(len(block.vertices), 4)

    def test_array_past_image_is_an_error(self):
        lists = END
        vertices_at = 0x18 + 0x14 + len(lists)
        record = struct.pack(">IIIIH", SEG5 + 0x18 + 0x14, 0, 0, SEG5 + vertices_at, 5).ljust(0x14, b"\0")
        image, *_ = self.build(NODE_DL, record, lists, raw_vertices(2))
        with self.assertRaises(GeVertexError):
            extract_model_vertices(image, parse_model_tree(image, ModelCounts(0, 0)))


class BgExtractTest(unittest.TestCase):
    def room(self, primary, secondary, vertices):
        return BgRoom(index=3, vertices=vertices, primary=primary, secondary=secondary)

    def test_merges_ranges_across_both_lists(self):
        primary = load(2, 0x0E000000) + END
        secondary = load(2, 0x0E000020) + load(1, 0x0E000060) + END
        blocks = extract_bg_vertices(self.room(primary, secondary, raw_vertices(8)))
        self.assertEqual([b.key for b in blocks], [BgVertexKey(3, 0), BgVertexKey(3, 0x60)])
        self.assertEqual([len(b.vertices) for b in blocks], [4, 1])
        self.assertEqual(blocks[1].vertices[0], decode_vertices(raw_vertex(7))[0])

    def test_unloaded_vertices_are_skipped(self):
        blocks = extract_bg_vertices(self.room(load(1, 0x0E000010) + END, None, raw_vertices(4)))
        self.assertEqual([b.key for b in blocks], [BgVertexKey(3, 0x10)])

    def test_room_without_lists_loads_nothing(self):
        self.assertEqual(extract_bg_vertices(self.room(END, None, b"")), [])

    def test_range_past_blob_is_an_error(self):
        with self.assertRaises(GeVertexError):
            extract_bg_vertices(self.room(load(4, 0x0E000000) + END, None, raw_vertices(2)))


if __name__ == "__main__":
    unittest.main()
