import struct
import unittest

from ge_container import ModelImage
from ge_dlemit import TextureBinding
from ge_model import GeModelError, ModelCounts, parse_model_tree
from ge_modeltree import (
    BSP_ENTRY_SIZE,
    EFFECT_ENTRY_SIZE,
    JOINT_ENTRY_SIZE,
    LOD_ENTRY_SIZE,
    NODE_RECORD_SIZE,
    NO_LIST,
    SORT_ENTRY_SIZE,
    SWITCH_ENTRY_SIZE,
    encode_model_tree_payload,
    matrix_count,
)

SEG5 = 0x05000000


def addr(offset):
    return SEG5 + offset


def node(node_type, data=0, parent=0, next_=0, prev=0, child=0):
    return struct.pack(">HHIIIII", node_type, 0, data, parent, next_, prev, child)


def group_data(origin, first, bend=-1, joint_id=0):
    return struct.pack(">3fHhhhIf", *origin, joint_id, first, bend, -1, 0, 0.0)


def lod_data(min_distance, max_distance, affects):
    return struct.pack(">ffIHH", min_distance, max_distance, addr(affects), 0, 0)


def bsp_data(point, vector, left, right, reserved):
    return struct.pack(">3f3fIIhH", *point, *vector, addr(left), addr(right) if right is not None else 0, reserved, 0)


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
        # 8 more bytes for the trailing LOD and BSP tables' (empty) entry
        # counts, plus a joint table entry for each of the 2 group nodes.
        self.assertEqual(len(payload), 8 + 3 * NODE_RECORD_SIZE + 8 + 4 + 2 * JOINT_ENTRY_SIZE + 4 + 4 + 2 * SORT_ENTRY_SIZE + 4)
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
        # 4 more bytes for the trailing BSP table's (empty) entry count, plus
        # a joint table entry for the one group node.
        self.assertEqual(len(payload), lod_table_at + 4 + LOD_ENTRY_SIZE + 4 + 4 + JOINT_ENTRY_SIZE + 4 + 4 + 1 * SORT_ENTRY_SIZE + 4)
        node_index, min_distance, max_distance = struct.unpack_from(">Iff", payload, lod_table_at + 4)
        self.assertEqual((node_index, min_distance, max_distance), (0, 10.0, 100.0))

    def test_lod_table_empty_without_lod_nodes(self):
        image = self.build()
        tree = parse_model_tree(image, ModelCounts(0, 0))
        payload = encode_model_tree_payload(image, tree)
        count, _ = struct.unpack_from(">II", payload)
        lod_table_at = 8 + count * NODE_RECORD_SIZE
        # empty LOD count, empty BSP count, joint table entry for each of the 2 group nodes
        self.assertEqual(len(payload), lod_table_at + 4 + 4 + 4 + 2 * JOINT_ENTRY_SIZE + 4 + 4 + 2 * SORT_ENTRY_SIZE + 4)
        self.assertEqual(struct.unpack_from(">I", payload, lod_table_at)[0], 0)

    def test_bsp_table_carries_plane_and_second_child(self):
        # BSP node (0x09) at 0x00 -> group1 (its own child link) -> group2
        # (group1's next sibling, and the BSP data's rightChild).
        bsp_data_at = 0x18
        group1_at = bsp_data_at + 0x24
        group2_at = group1_at + 0x18 + 0x1C
        blob = (
            node(0x09, data=addr(bsp_data_at), child=addr(group1_at))
            + bsp_data((1.0, 2.0, 3.0), (4.0, 5.0, 6.0), group1_at, group2_at, 2)
            + node(0x02, data=addr(group1_at + 0x18), parent=addr(0), next_=addr(group2_at))
            + group_data((0.0, 0.0, 0.0), 0)
            + node(0x02, data=addr(group2_at + 0x18), parent=addr(0))
            + group_data((0.0, 0.0, 0.0), 1)
        )
        image = ModelImage(blob)
        tree = parse_model_tree(image, ModelCounts(0, 0))
        payload = encode_model_tree_payload(image, tree)

        count, _ = struct.unpack_from(">II", payload)
        self.assertEqual(count, 3)
        # LOD table is empty (no LOD nodes) but still takes its 4-byte count.
        bsp_table_at = 8 + count * NODE_RECORD_SIZE + 4
        bsp_count = struct.unpack_from(">I", payload, bsp_table_at)[0]
        self.assertEqual(bsp_count, 1)
        # plus a joint table entry for each of the 2 group nodes.
        self.assertEqual(len(payload), bsp_table_at + 4 + BSP_ENTRY_SIZE + 4 + 2 * JOINT_ENTRY_SIZE + 4 + 4 + 2 * SORT_ENTRY_SIZE + 4)
        node_index, px, py, pz, vx, vy, vz, reserved, second_child = struct.unpack_from(
            ">I3f3fii", payload, bsp_table_at + 4
        )
        self.assertEqual((node_index, (px, py, pz), (vx, vy, vz), reserved), (0, (1.0, 2.0, 3.0), (4.0, 5.0, 6.0), 2))
        self.assertEqual(second_child, 2)  # group2 is the third node visited (index 2)

    def test_bsp_table_empty_without_bsp_nodes(self):
        image = self.build()
        tree = parse_model_tree(image, ModelCounts(0, 0))
        payload = encode_model_tree_payload(image, tree)
        count, _ = struct.unpack_from(">II", payload)
        bsp_table_at = 8 + count * NODE_RECORD_SIZE + 4
        # empty BSP count, joint table entry for each of the 2 group nodes.
        self.assertEqual(len(payload), bsp_table_at + 4 + 4 + 2 * JOINT_ENTRY_SIZE + 4 + 4 + 2 * SORT_ENTRY_SIZE + 4)
        self.assertEqual(struct.unpack_from(">I", payload, bsp_table_at)[0], 0)

    def test_joint_table_carries_joint_id_per_group_node(self):
        root_data, group_at = 0x18 * 2, 0x18 * 2 + 0x1C
        blob = (
            node(0x02, data=addr(root_data), child=addr(0x18))
            + node(0x02, data=addr(group_at), parent=addr(0))
            + group_data((0.0, 0.0, 0.0), 0, joint_id=0)
            + group_data((0.0, 0.0, 0.0), 1, joint_id=5)
        )
        image = ModelImage(blob)
        tree = parse_model_tree(image, ModelCounts(0, 0))
        payload = encode_model_tree_payload(image, tree)

        count, _ = struct.unpack_from(">II", payload)
        self.assertEqual(count, 2)
        joint_table_at = 8 + count * NODE_RECORD_SIZE + 4 + 4  # past the (empty) LOD and BSP tables
        joint_count = struct.unpack_from(">I", payload, joint_table_at)[0]
        self.assertEqual(joint_count, 2)
        self.assertEqual(len(payload), joint_table_at + 4 + 2 * JOINT_ENTRY_SIZE + 4 + 4 + 2 * SORT_ENTRY_SIZE + 4)
        entries = [struct.unpack_from(">IH", payload, joint_table_at + 4 + i * JOINT_ENTRY_SIZE) for i in range(2)]
        self.assertEqual(entries, [(0, 0), (1, 5)])

    def test_joint_table_empty_without_group_nodes(self):
        image = ModelImage(node(0x12, data=addr(0x18)) + struct.pack(">I", 0) + b"\0" * 8)
        tree = parse_model_tree(image, ModelCounts(0, 0))
        payload = encode_model_tree_payload(image, tree)
        count, _ = struct.unpack_from(">II", payload)
        joint_table_at = 8 + count * NODE_RECORD_SIZE + 4 + 4
        self.assertEqual(len(payload), joint_table_at + 4 + 4 + 4 + 4)
        self.assertEqual(struct.unpack_from(">I", payload, joint_table_at)[0], 0)

    def test_header_node_joint_entry_is_its_anim_part(self):
        # Header (0x01): AnimPart u16 at 0, then the matrix index at 2. The
        # child group has joint 5.
        header_data, group_at = 0x18 * 2, 0x18 * 2 + 0x0C
        blob = (
            node(0x01, data=addr(header_data), child=addr(0x18))
            + node(0x02, data=addr(group_at), parent=addr(0))
            + struct.pack(">HhIIHHI", 3, 0, 0, 0, 0, 0, 0)[:0x0C]
            + group_data((0.0, 0.0, 0.0), 1, joint_id=5)
        )
        image = ModelImage(blob)
        tree = parse_model_tree(image, ModelCounts(0, 0))
        payload = encode_model_tree_payload(image, tree)

        count, _ = struct.unpack_from(">II", payload)
        joint_table_at = 8 + count * NODE_RECORD_SIZE + 4 + 4
        self.assertEqual(struct.unpack_from(">I", payload, joint_table_at)[0], 2)
        entries = [struct.unpack_from(">IH", payload, joint_table_at + 4 + i * JOINT_ENTRY_SIZE) for i in range(2)]
        self.assertEqual(entries, [(0, 3), (1, 5)])

    def test_switch_table_maps_the_switch_array_to_record_indices(self):
        # Switch array (3 pointers: node 1, null, node 0), then two group nodes.
        first, second = 12, 12 + 0x18
        data_at = 12 + 0x18 * 2
        blob = (
            struct.pack(">III", addr(second), 0, addr(first))
            + node(0x02, data=addr(data_at), child=addr(second))
            + node(0x02, data=addr(data_at + 0x1C), parent=addr(first))
            + group_data((0.0, 0.0, 0.0), 0)
            + group_data((0.0, 0.0, 0.0), 1)
        )
        image = ModelImage(blob)
        tree = parse_model_tree(image, ModelCounts(3, 0))
        payload = encode_model_tree_payload(image, tree)

        count, _ = struct.unpack_from(">II", payload)
        switch_table_at = 8 + count * NODE_RECORD_SIZE + 4 + 4 + 4 + 2 * JOINT_ENTRY_SIZE
        self.assertEqual(struct.unpack_from(">I", payload, switch_table_at)[0], 3)
        self.assertEqual(len(payload), switch_table_at + 4 + 3 * SWITCH_ENTRY_SIZE + 4 + 2 * SORT_ENTRY_SIZE + 4)
        entries = struct.unpack_from(">3i", payload, switch_table_at + 4)
        self.assertEqual(entries, (1, -1, 0))

    def sort_table(self, payload, count):
        """(node index, other, point, point2) entries of the trailing sort table."""
        at = 8 + count * NODE_RECORD_SIZE
        for _ in range(3):  # LOD, BSP, joint tables: each a count then entries
            entries = struct.unpack_from(">I", payload, at)[0]
            at += 4
            at += entries * {0: LOD_ENTRY_SIZE, 1: BSP_ENTRY_SIZE, 2: JOINT_ENTRY_SIZE}[_]
        switches = struct.unpack_from(">I", payload, at)[0]
        at += 4 + switches * SWITCH_ENTRY_SIZE
        total = struct.unpack_from(">I", payload, at)[0]
        return [
            struct.unpack_from(">Ii3f3f", payload, at + 4 + i * SORT_ENTRY_SIZE) for i in range(total)
        ]

    def effect_table(self, payload, count):
        """Entries of the trailing effect table: (node index, size, image
        present, width, height, mip levels, format)."""
        at = 8 + count * NODE_RECORD_SIZE
        for entry_size in (LOD_ENTRY_SIZE, BSP_ENTRY_SIZE, JOINT_ENTRY_SIZE):
            entries = struct.unpack_from(">I", payload, at)[0]
            at += 4 + entries * entry_size
        switches = struct.unpack_from(">I", payload, at)[0]
        at += 4 + switches * SWITCH_ENTRY_SIZE
        sorts = struct.unpack_from(">I", payload, at)[0]
        at += 4 + sorts * SORT_ENTRY_SIZE
        total = struct.unpack_from(">I", payload, at)[0]
        return [
            struct.unpack_from(">I3fBBBBB", payload, at + 4 + i * EFFECT_ENTRY_SIZE) for i in range(total)
        ]

    def test_gunfire_effect_entry_carries_its_size_and_image(self):
        # A gunfire node whose image is the one texture table entry (32x16,
        # 3 mip levels); the file has no switches, so the table starts at 0.
        table = struct.pack(">I7Bx", 7, 32, 16, 3, 0, 2, 0, 0)
        gunfire_data = 12 + 0x18
        blob = (
            table
            + node(0x0C, data=addr(gunfire_data))
            + struct.pack(">3f3fIfHHI", 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, addr(0), 1.0, 0, 0, 0)
        )
        image = ModelImage(blob)
        tree = parse_model_tree(image, ModelCounts(0, 1))
        binding = TextureBinding("textures/FLASH", fmt=0, size=3, width=32)
        entries = self.effect_table(encode_model_tree_payload(image, tree, lambda texture_id: binding), 1)
        self.assertEqual(entries, [(0, 4.0, 5.0, 6.0, 1, 32, 16, 3, 0)])

    def test_shadow_effect_entry_has_no_depth_and_can_be_untextured(self):
        header_data, shadow_data = 0x18 * 2, 0x18 * 2 + 0x0C
        blob = (
            node(0x01, data=addr(header_data), next_=addr(0x18))
            + node(0x0D, data=addr(shadow_data), prev=addr(0))
            + struct.pack(">HhII", 0, 0, 0, 0)
            + struct.pack(">2f2fII f I", 5.0, 6.0, 7.0, 8.0, 0, addr(0), 1.0, 0)
        )
        image = ModelImage(blob)
        tree = parse_model_tree(image, ModelCounts(0, 0))
        entries = self.effect_table(encode_model_tree_payload(image, tree), 2)
        self.assertEqual(entries, [(1, 7.0, 8.0, 0.0, 0, 0, 0, 0, 0)])

    def test_a_textured_effect_needs_a_texture_lookup(self):
        table = struct.pack(">I7Bx", 7, 32, 16, 0, 0, 2, 0, 0)
        blob = (
            table
            + node(0x0C, data=addr(12 + 0x18))
            + struct.pack(">3f3fIfHHI", 0, 0, 0, 1, 1, 1, addr(0), 1.0, 0, 0, 0)
        )
        image = ModelImage(blob)
        tree = parse_model_tree(image, ModelCounts(0, 1))
        with self.assertRaises(GeModelError):
            encode_model_tree_payload(image, tree)

    def test_an_effect_image_that_is_not_a_texture_table_entry_is_rejected(self):
        table = struct.pack(">I7Bx", 7, 32, 16, 0, 0, 2, 0, 0)
        blob = (
            table
            + node(0x0C, data=addr(12 + 0x18))
            + struct.pack(">3f3fIfHHI", 0, 0, 0, 1, 1, 1, addr(5), 1.0, 0, 0, 0)
        )
        image = ModelImage(blob)
        tree = parse_model_tree(image, ModelCounts(0, 1))
        with self.assertRaises(GeModelError):
            encode_model_tree_payload(image, tree, lambda texture_id: None)

    def test_group_and_header_sort_entries_name_their_child_group(self):
        # Header (0x01) whose FirstGroup is the group at record 1; that group's
        # ChildGroup is itself null.
        header_data, group_at = 0x18 * 2, 0x18 * 2 + 0x0C
        blob = (
            node(0x01, data=addr(header_data), child=addr(0x18))
            + node(0x02, data=addr(group_at), parent=addr(0))
            + struct.pack(">HhII", 0, 0, addr(0x18), 0)
            + group_data((0.0, 0.0, 0.0), 1)
        )
        image = ModelImage(blob)
        tree = parse_model_tree(image, ModelCounts(0, 0))
        payload = encode_model_tree_payload(image, tree)
        entries = self.sort_table(payload, 2)
        self.assertEqual([(e[0], e[1]) for e in entries], [(0, 1), (1, -1)])

    def test_gunfire_sort_entry_carries_its_offset(self):
        blob = node(0x0C, data=addr(0x18)) + struct.pack(">3f3fIfHHI", 1.0, 2.0, 3.0, 0.0, 0.0, 0.0, 0, 1.0, 0, 0, 0)
        image = ModelImage(blob)
        tree = parse_model_tree(image, ModelCounts(0, 0))
        entries = self.sort_table(encode_model_tree_payload(image, tree), 1)
        self.assertEqual(entries, [(0, -1, 1.0, 2.0, 3.0, 0.0, 0.0, 0.0)])

    def test_shadow_sort_entry_carries_its_position_and_header_node(self):
        header_data, shadow_data = 0x18 * 2, 0x18 * 2 + 0x0C
        blob = (
            node(0x01, data=addr(header_data), next_=addr(0x18))
            + node(0x0D, data=addr(shadow_data), prev=addr(0))
            + struct.pack(">HhII", 0, 0, 0, 0)
            + struct.pack(">2f2fII f I", 5.0, 6.0, 1.0, 1.0, 0, addr(0), 1.0, 0)
        )
        image = ModelImage(blob)
        tree = parse_model_tree(image, ModelCounts(0, 0))
        entries = self.sort_table(encode_model_tree_payload(image, tree), 2)
        self.assertEqual(entries[1], (1, 0, 5.0, 0.0, 6.0, 0.0, 0.0, 0.0))

    def test_display_list_nodes_carry_their_model_type(self):
        # A plain display-list node (0x04): primary, secondary, base, vertices,
        # count (u16 at 0x10), model type (s8 at 0x12).
        blob = node(0x04, data=addr(0x18)) + struct.pack(">IIIIHbx", 0, 0, 0, 0, 0, 3)
        image = ModelImage(blob)
        tree = parse_model_tree(image, ModelCounts(0, 0))
        payload = encode_model_tree_payload(image, tree)
        self.assertEqual(payload[8 + 5], 3)

    def test_an_unknown_model_type_is_rejected(self):
        blob = node(0x04, data=addr(0x18)) + struct.pack(">IIIIHbx", 0, 0, 0, 0, 0, 9)
        image = ModelImage(blob)
        tree = parse_model_tree(image, ModelCounts(0, 0))
        with self.assertRaises(GeModelError):
            encode_model_tree_payload(image, tree)

    def test_a_node_type_with_no_packed_sort_data_is_rejected(self):
        image = ModelImage(node(0x10, data=addr(0x18)) + b"\0" * 0x20)
        tree = parse_model_tree(image, ModelCounts(0, 0))
        with self.assertRaises(GeModelError):
            encode_model_tree_payload(image, tree)


if __name__ == "__main__":
    unittest.main()
