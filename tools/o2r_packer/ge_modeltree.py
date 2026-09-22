#!/usr/bin/env python3
"""Encodes a parsed GoldenEye model node tree as a `GEMD` resource (frigate
issue #12). Payload and matrix semantics: docs/formats/goldeneye-model-tree-resource.md.
"""

from __future__ import annotations

import struct
from typing import Dict, List, Optional

from ge_container import ModelImage
from ge_model import NODE_BSP, NODE_LOD, NODE_SWITCH, GeModelError, ModelNode, ModelTree, implied_children

MODEL_TREE_RESOURCE_TYPE = "GEMD"
MODEL_TREE_RESOURCE_VERSION = 2

NODE_HEADER = 0x01
NODE_GROUP = 0x02
NODE_GROUP_ALT = 0x03
NODE_GROUP_SIMPLE = 0x15

NONE_INDEX = -1
NO_LIST = 0xFFFFFFFF

_NODE_STRUCT = struct.Struct(">IBxhhxxiii3fII")
NODE_RECORD_SIZE = _NODE_STRUCT.size

# node record index, min distance, max distance; see "LOD table" in
# docs/formats/goldeneye-model-tree-resource.md.
_LOD_ENTRY_STRUCT = struct.Struct(">Iff")
LOD_ENTRY_SIZE = _LOD_ENTRY_STRUCT.size


def _lod_distances(image: ModelImage, node: ModelNode):
    """(min, max) distance of an LOD node's `ModelRoData_LODRecord`, or
    None for any other node type."""
    if node.node_type != NODE_LOD or node.data is None:
        return None
    return struct.unpack_from(">2f", image.data, node.data)


# node record index, point (3f), vector (3f), transform mode, second
# child group's head (record index); see "BSP table" in
# docs/formats/goldeneye-model-tree-resource.md.
_BSP_ENTRY_STRUCT = struct.Struct(">I3f3fii")
BSP_ENTRY_SIZE = _BSP_ENTRY_STRUCT.size


def _bsp_fields(image: ModelImage, node: ModelNode):
    """(point, vector, transform mode, second child group's head offset
    or None) of a BSP node's `ModelRoData_BSPRecord`, or None for any
    other node type."""
    if node.node_type != NODE_BSP or node.data is None:
        return None
    point = struct.unpack_from(">3f", image.data, node.data)
    vector = struct.unpack_from(">3f", image.data, node.data + 0x0C)
    right_addr = struct.unpack_from(">I", image.data, node.data + 0x1C)[0]
    reserved = struct.unpack_from(">h", image.data, node.data + 0x20)[0]
    right_child = image.resolve(right_addr) if right_addr != 0 else None
    return point, vector, reserved, right_child


def _matrix_fields(image: ModelImage, node: ModelNode):
    """(origin, matrix index, bend matrix index) of a node; -1 for none."""
    data = image.data
    if node.data is None:
        return (0.0, 0.0, 0.0), NONE_INDEX, NONE_INDEX
    if node.node_type == NODE_HEADER:
        return (0.0, 0.0, 0.0), struct.unpack_from(">h", data, node.data + 0x02)[0], NONE_INDEX
    if node.node_type in (NODE_GROUP, NODE_GROUP_ALT):
        origin = struct.unpack_from(">3f", data, node.data)
        first, bend = struct.unpack_from(">2h", data, node.data + 0x0E)
        return origin, first, bend
    if node.node_type == NODE_GROUP_SIMPLE:
        origin = struct.unpack_from(">3f", data, node.data)
        return origin, struct.unpack_from(">h", data, node.data + 0x0C)[0], NONE_INDEX
    return (0.0, 0.0, 0.0), NONE_INDEX, NONE_INDEX


def _first_child(image: ModelImage, node: ModelNode) -> Optional[int]:
    if node.child is not None:
        return node.child
    targets = implied_children(image, node) if node.node_type in (NODE_LOD, NODE_SWITCH) else []
    return targets[0] if targets else None


def _preorder(image: ModelImage, tree: ModelTree) -> List[int]:
    order: List[int] = []
    stack = [tree.root]
    while stack:
        offset = stack.pop()
        order.append(offset)
        node = tree.nodes[offset]
        # Push the sibling first so the child is visited before it.
        if node.next is not None:
            stack.append(node.next)
        child = _first_child(image, node)
        if child is not None:
            stack.append(child)
    return order


def matrix_count(image: ModelImage, tree: ModelTree) -> int:
    highest = -1
    for node in tree.nodes.values():
        _, first, bend = _matrix_fields(image, node)
        highest = max(highest, first, bend)
    return highest + 1


def encode_model_tree_payload(image: ModelImage, tree: ModelTree) -> bytes:
    order = _preorder(image, tree)
    index_of: Dict[int, int] = {offset: i for i, offset in enumerate(order)}
    if len(index_of) != len(tree.nodes):
        raise GeModelError("node tree has nodes the child and next links do not reach")

    parents: Dict[int, int] = {}
    for offset in order:
        node = tree.nodes[offset]
        child = _first_child(image, node)
        while child is not None:
            parents[child] = offset
            child = tree.nodes[child].next

    out = bytearray(struct.pack(">II", len(order), matrix_count(image, tree)))
    lod_entries = bytearray()
    lod_count = 0
    bsp_entries = bytearray()
    bsp_count = 0
    for i, offset in enumerate(order):
        node = tree.nodes[offset]
        origin, first, bend = _matrix_fields(image, node)
        child = _first_child(image, node)
        record = node.display_list
        out += _NODE_STRUCT.pack(
            offset,
            node.node_type,
            first,
            bend,
            index_of[child] if child is not None else NONE_INDEX,
            index_of[node.next] if node.next is not None else NONE_INDEX,
            index_of[parents[offset]] if offset in parents else NONE_INDEX,
            *origin,
            record.primary if record and record.primary is not None else NO_LIST,
            record.secondary if record and record.secondary is not None else NO_LIST,
        )
        distances = _lod_distances(image, node)
        if distances is not None:
            lod_entries += _LOD_ENTRY_STRUCT.pack(i, *distances)
            lod_count += 1
        bsp = _bsp_fields(image, node)
        if bsp is not None:
            point, vector, reserved, right_child = bsp
            bsp_entries += _BSP_ENTRY_STRUCT.pack(
                i, *point, *vector, reserved, index_of[right_child] if right_child is not None else NONE_INDEX
            )
            bsp_count += 1
    out += struct.pack(">I", lod_count)
    out += lod_entries
    out += struct.pack(">I", bsp_count)
    out += bsp_entries
    return bytes(out)
