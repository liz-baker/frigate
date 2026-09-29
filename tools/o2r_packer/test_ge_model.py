import struct
import tempfile
import unittest
from pathlib import Path

from ge_container import ModelImage
from ge_model import (
    NODE_DL,
    NODE_DL_COLLISION,
    NODE_DL_PRIMARY,
    NODE_LOD,
    NODE_SWITCH,
    GeModelError,
    ModelCounts,
    parse_model_tree,
    read_model_header_counts,
)

SEG5 = 0x05000000


def addr(offset):
    return SEG5 + offset


def node(node_type, data=0, parent=0, next_=0, prev=0, child=0):
    return struct.pack(">HHIIIII", node_type, 0, data, parent, next_, prev, child)


def image(*chunks, size=None):
    data = b"".join(chunks)
    return ModelImage(data.ljust(size, b"\0") if size else data)


class ParseModelTreeTest(unittest.TestCase):
    def test_reads_links_switches_and_textures(self):
        # 1 switch pointer and 1 texture put the root at 0x10 and its child at 0x28.
        switch = struct.pack(">I", addr(0x28))
        texture = struct.pack(">IBBBBBBB", 0x1234, 32, 16, 1, 2, 3, 4, 5) + b"\0"
        root = node(0x02, parent=0, child=addr(0x28))
        leaf = node(0x0A, parent=addr(0x10))
        tree = parse_model_tree(image(switch, texture, root, leaf), ModelCounts(1, 1))

        self.assertEqual(tree.root, 0x10)
        self.assertEqual(tree.switches, [0x28])
        self.assertEqual(tree.textures[0].texture_id, 0x1234)
        self.assertEqual((tree.textures[0].width, tree.textures[0].height), (32, 16))
        self.assertEqual((tree.textures[0].render_depth, tree.textures[0].t_flags), (3, 5))
        self.assertEqual(sorted(tree.nodes), [0x10, 0x28])
        self.assertEqual(tree.nodes[0x10].child, 0x28)
        self.assertEqual(tree.nodes[0x28].parent, 0x10)
        self.assertIsNone(tree.nodes[0x28].next)

    def test_follows_next_links(self):
        first = node(0x02, next_=addr(0x18))
        second = node(0x0A, prev=addr(0))
        tree = parse_model_tree(image(first, second), ModelCounts(0, 0))
        self.assertEqual(sorted(tree.nodes), [0, 0x18])

    def test_switch_target_is_followed_when_child_link_is_null(self):
        switch_record = struct.pack(">IHH", addr(0x30), 0, 0)
        root = node(NODE_SWITCH, data=addr(0x18))
        target = node(0x0A, parent=addr(0))
        tree = parse_model_tree(image(root, switch_record.ljust(0x18, b"\0"), target), ModelCounts(0, 0))
        self.assertIn(0x30, tree.nodes)

    def test_switch_target_equal_to_child_link_is_one_edge(self):
        switch_record = struct.pack(">IHH", addr(0x30), 0, 0)
        root = node(NODE_SWITCH, data=addr(0x18), child=addr(0x30))
        target = node(0x0A, parent=addr(0))
        tree = parse_model_tree(image(root, switch_record.ljust(0x18, b"\0"), target), ModelCounts(0, 0))
        self.assertEqual(sorted(tree.nodes), [0, 0x30])

    def test_lod_target_is_read_from_its_third_field(self):
        lod_record = struct.pack(">ffIHH", 0.0, 1.0, addr(0x30), 0, 0)
        root = node(NODE_LOD, data=addr(0x18))
        target = node(0x0A, parent=addr(0))
        tree = parse_model_tree(image(root, lod_record.ljust(0x18, b"\0"), target), ModelCounts(0, 0))
        self.assertIn(0x30, tree.nodes)

    def test_plain_display_list_node(self):
        record = struct.pack(">IIIIH", addr(0x100), addr(0x108), 0, addr(0x120), 7)
        root = node(NODE_DL, data=addr(0x18))
        tree = parse_model_tree(image(root, record.ljust(0x20, b"\0"), size=0x130), ModelCounts(0, 0))
        dl = tree.nodes[0].display_list
        self.assertEqual((dl.primary, dl.secondary, dl.vertices, dl.num_vertices), (0x100, 0x108, 0x120, 7))
        self.assertEqual(dl.vertex_bytes, 7 * 16)
        self.assertEqual(dl.vertices_address, addr(0x120))

    def test_collision_display_list_node_has_signed_count_and_optional_secondary(self):
        record = struct.pack(">IIIh", addr(0x100), 0, addr(0x120), -2)
        root = node(NODE_DL_COLLISION, data=addr(0x18))
        tree = parse_model_tree(image(root, record.ljust(0x20, b"\0"), size=0x130), ModelCounts(0, 0))
        dl = tree.nodes[0].display_list
        self.assertIsNone(dl.secondary)
        self.assertEqual((dl.primary, dl.vertices, dl.num_vertices), (0x100, 0x120, -2))
        self.assertEqual(dl.vertex_bytes, 0)

    def test_primary_only_display_list_node(self):
        record = struct.pack(">iII", 12, addr(0x120), addr(0x100))
        root = node(NODE_DL_PRIMARY, data=addr(0x18))
        tree = parse_model_tree(image(root, record.ljust(0x20, b"\0"), size=0x130), ModelCounts(0, 0))
        dl = tree.nodes[0].display_list
        self.assertEqual((dl.primary, dl.secondary, dl.vertices, dl.num_vertices), (0x100, None, 0x120, 12))
        self.assertEqual(dl.vertex_bytes, 12 * 4 * 16)

    def test_null_vertex_pointer_is_kept_as_none(self):
        record = struct.pack(">IIIIH", addr(0x100), 0, 0, 0, 0)
        root = node(NODE_DL, data=addr(0x18))
        tree = parse_model_tree(image(root, record.ljust(0x20, b"\0"), size=0x130), ModelCounts(0, 0))
        self.assertIsNone(tree.nodes[0].display_list.vertices)

    def test_rejects_a_link_outside_the_image(self):
        with self.assertRaisesRegex(GeModelError, "child"):
            parse_model_tree(image(node(0x02, child=addr(0x1000))), ModelCounts(0, 0))

    def test_rejects_a_link_with_the_wrong_segment(self):
        with self.assertRaisesRegex(GeModelError, "next"):
            parse_model_tree(image(node(0x02, next_=0x06000000)), ModelCounts(0, 0))

    def test_rejects_a_cycle(self):
        cyclic = node(0x02, child=addr(0))
        with self.assertRaisesRegex(GeModelError, "reached twice"):
            parse_model_tree(image(cyclic), ModelCounts(0, 0))

    def test_rejects_a_node_reached_by_two_links(self):
        root = node(0x02, next_=addr(0x18), child=addr(0x18))
        with self.assertRaisesRegex(GeModelError, "reached twice"):
            parse_model_tree(image(root, node(0x0A)), ModelCounts(0, 0))

    def test_rejects_a_display_list_node_without_data(self):
        with self.assertRaisesRegex(GeModelError, "no data record"):
            parse_model_tree(image(node(NODE_DL)), ModelCounts(0, 0))

    def test_rejects_counts_that_run_past_the_image(self):
        with self.assertRaisesRegex(GeModelError, "past the image"):
            parse_model_tree(image(node(0x02)), ModelCounts(4, 4))

    def test_rejects_a_truncated_node(self):
        with self.assertRaisesRegex(GeModelError, "runs off"):
            parse_model_tree(image(node(0x02, next_=addr(0x0C))), ModelCounts(0, 0))


class ReadModelHeaderCountsTest(unittest.TestCase):
    def _decomp(self, files):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        for relative, text in files.items():
            path = root / "assets" / "obseg" / relative
            path.parent.mkdir(parents=True)
            path.write_text(text)
        return root

    def test_reads_switch_and_texture_counts_by_header_name(self):
        root = self._decomp(
            {
                "gun/ak47/modelFileHeader.inc.c": "//D:800XXXXX\nMODELFILEHEADER(ak47, 0, &SKELETON(standard_gun), 0, 0x24, 4, 941.9339, 0, 0x12)\n",
                "prop/alarm1/ModelFileHeader.inc.c": "MODELFILEHEADER(alarm1, 0, &SKELETON(standard_object), 0, 0, 1, 10.5, 0, 2)\n",
            }
        )
        counts = read_model_header_counts(root)
        self.assertEqual(counts["ak47"], ModelCounts(0x24, 0x12, "standard_gun", 941.9339))
        self.assertEqual(counts["alarm1"], ModelCounts(0, 2, "standard_object", 10.5))

    def test_a_null_skeleton_header_has_no_skeleton_name(self):
        root = self._decomp(
            {"chr/headjoe/modelFileHeader.inc.c": "MODELFILEHEADER(headjoe, 0, NULL, 0, 0, 1, 10.5, 0, 1)\n"}
        )
        counts = read_model_header_counts(root)
        self.assertIsNone(counts["headjoe"].skeleton_name)

    def test_rejects_a_header_with_the_wrong_argument_count(self):
        root = self._decomp({"gun/x/modelFileHeader.inc.c": "MODELFILEHEADER(x, 0, 0, 0, 1)\n"})
        with self.assertRaisesRegex(GeModelError, "header arguments"):
            read_model_header_counts(root)


if __name__ == "__main__":
    unittest.main()
