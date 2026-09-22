import struct
import unittest

from ge_container import ModelImage
from ge_model import ModelCounts, parse_model_tree
from ge_modeltree import (
    LOD_ENTRY_SIZE,
    NODE_RECORD_SIZE,
    NO_LIST,
    encode_model_tree_payload,
    matrix_count,
)

SEG5 = 0x05000000


def addr(offset):
    return SEG5 + offset


def node(node_type, data=0, parent=0, next_=0, prev=0, child=0):
    return struct.pack(">HHIIIII", node_type, 0, data, parent, next_, prev, child)


def group_data(origin, first, bend=-1):
    return struct.pack(">3fHhhhIf", *origin, 0, first, bend, -1, 0, 0.0)


def lod_data(min_distance, max_distance, affects):
    return struct.pack(">ffIHH", min_distance, max_distance, addr(affects), 0, 0)


class EncodeModelTreeTest(unittest.TestCase):
    def build(self):
        # root group (matrix 0) -> switch -> group (matrix 2, bend 3); the
        # switch's file child link is null and its data names the group.
        root_data, group_at = 0x18 * 3, 0x18 * 3 + 0x1C
        switch_data = group_at + 0x1C
        blob = (
            node(0x02, data=addr(root_data), child=addr(0x18))
            + node(0x12, data=addr(switch_data), parent=addr(0))
            + node(0x02, data=addr(group_at), parent=addr(0x18))
            + group_data((1.0, 2.0, 3.0), 0)
            + group_data((4.0, 5.0, 6.0), 2, 3)
            + struct.pack(">I", addr(0x30))
        )
        return ModelImage(blob)

    def test_preorder_records_with_resolved_switch_child(self):
        image = self.build()
        tree = parse_model_tree(image, ModelCounts(0, 0))
        payload = encode_model_tree_payload(image, tree)
        count, matrices = struct.unpack_from(">II", payload)
        self.assertEqual((count, matrices), (3, 4))
        # 4 more bytes for the trailing LOD table's (empty) entry count.
        self.assertEqual(len(payload), 8 + 3 * NODE_RECORD_SIZE + 4)
        records = [
            struct.unpack_from(">IBxhhxxiii3fII", payload, 8 + i * NODE_RECORD_SIZE) for i in range(3)
        ]
        self.assertEqual(records[0][:7], (0, 0x02, 0, -1, 1, -1, -1))
        self.assertEqual(records[0][7:10], (1.0, 2.0, 3.0))
        self.assertEqual(records[1][:7], (0x18, 0x12, -1, -1, 2, -1, 0))
        self.assertEqual(records[2][:7], (0x30, 0x02, 2, 3, -1, -1, 1))
        self.assertEqual(records[2][10:], (NO_LIST, NO_LIST))

    def test_matrix_count_is_zero_without_matrix_nodes(self):
        image = ModelImage(node(0x12, data=addr(0x18)) + struct.pack(">I", 0) + b"\0" * 8)
        tree = parse_model_tree(image, ModelCounts(0, 0))
        self.assertEqual(matrix_count(image, tree), 0)

    def test_lod_table_carries_min_max_distance(self):
        # LOD node (0x08) at 0x00 -> its data's Affects names a group node.
        lod_data_at, group_at = 0x18, 0x18 + 0x10
        blob = (
            node(0x08, data=addr(lod_data_at))
            + lod_data(10.0, 100.0, group_at)
            + node(0x02, data=addr(group_at + 0x18), parent=addr(0))
            + group_data((0.0, 0.0, 0.0), 0)
        )
        image = ModelImage(blob)
        tree = parse_model_tree(image, ModelCounts(0, 0))
        payload = encode_model_tree_payload(image, tree)

        count, _ = struct.unpack_from(">II", payload)
        lod_table_at = 8 + count * NODE_RECORD_SIZE
        lod_count = struct.unpack_from(">I", payload, lod_table_at)[0]
        self.assertEqual(lod_count, 1)
        self.assertEqual(len(payload), lod_table_at + 4 + LOD_ENTRY_SIZE)
        node_index, min_distance, max_distance = struct.unpack_from(">Iff", payload, lod_table_at + 4)
        self.assertEqual((node_index, min_distance, max_distance), (0, 10.0, 100.0))

    def test_lod_table_empty_without_lod_nodes(self):
        image = self.build()
        tree = parse_model_tree(image, ModelCounts(0, 0))
        payload = encode_model_tree_payload(image, tree)
        count, _ = struct.unpack_from(">II", payload)
        lod_table_at = 8 + count * NODE_RECORD_SIZE
        self.assertEqual(len(payload), lod_table_at + 4)
        self.assertEqual(struct.unpack_from(">I", payload, lod_table_at)[0], 0)


if __name__ == "__main__":
    unittest.main()
