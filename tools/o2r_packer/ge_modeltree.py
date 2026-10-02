#!/usr/bin/env python3
"""Encodes a parsed GoldenEye model node tree as a `GEMD` resource (frigate
issue #12). Payload and matrix semantics: docs/formats/goldeneye-model-tree-resource.md.
"""

from __future__ import annotations

import struct
from typing import Callable, Dict, List, Optional

from ge_container import ModelImage
from ge_dlemit import TextureBinding
from ge_model import (
    NODE_BSP,
    NODE_DL,
    NODE_DL_COLLISION,
    NODE_GUNFIRE,
    NODE_LOD,
    NODE_SHADOW,
    NODE_SWITCH,
    GeModelError,
    ModelNode,
    ModelTree,
    effect_record,
    implied_children,
)

MODEL_TREE_RESOURCE_TYPE = "GEMD"
MODEL_TREE_RESOURCE_VERSION = 8

NODE_HEADER = 0x01
NODE_GROUP = 0x02
NODE_GROUP_ALT = 0x03
NODE_GROUP_SIMPLE = 0x15
NODE_INTERLINK = 0x0F
# Draw-order node types the game handles but no US ROM model contains.
_UNPACKED_SORT_NODE_TYPES = (0x03, 0x0B, 0x0E, 0x10)

NONE_INDEX = -1
NO_LIST = 0xFFFFFFFF

_NODE_STRUCT = struct.Struct(">IBBhhxxiii3fII")
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


# node record index, other node (record index or -1), point, second point;
# see "sort table" in docs/formats/goldeneye-model-tree-resource.md.
_SORT_ENTRY_STRUCT = struct.Struct(">Ii3f3f")
SORT_ENTRY_SIZE = _SORT_ENTRY_STRUCT.size

# node record index, size (3f), image present, width, height, mip level count,
# N64 image format; see "Effect table" in
# docs/formats/goldeneye-model-tree-resource.md.
_EFFECT_ENTRY_STRUCT = struct.Struct(">I3fBBBBBxxx")
EFFECT_ENTRY_SIZE = _EFFECT_ENTRY_STRUCT.size

# node record index, xmin, xmax, ymin, ymax, zmin, zmax; see "Bounds table" in
# docs/formats/goldeneye-model-tree-resource.md.
_BOUNDS_ENTRY_STRUCT = struct.Struct(">I6f")
BOUNDS_ENTRY_SIZE = _BOUNDS_ENTRY_STRUCT.size
NODE_BOUNDING_BOX = 0x0A
NODE_HIT_SPHERE = 0x11

# node record index, hit part, radius squared, centre (3f), other node (record
# index or -1), scale 1, scale 2, opcode flags; see "Hit table" in
# docs/formats/goldeneye-model-tree-resource.md.
_HIT_ENTRY_STRUCT = struct.Struct(">Iif3fiffHxx")
HIT_ENTRY_SIZE = _HIT_ENTRY_STRUCT.size

# node record index, vertex count, collision vertex count; then per collision
# vertex its position (3h), the vertex it stands for, the related node (record
# index or -1) and the related collision vertex; then each vertex's next use.
# See "Collision table" in docs/formats/goldeneye-model-tree-resource.md.
_COLLISION_HEADER_STRUCT = struct.Struct(">IHH")
_COLLISION_VERTEX_STRUCT = struct.Struct(">3hhihxx")
_COLLISION_USAGE_STRUCT = struct.Struct(">h")

MAX_RENDER_TYPE = 4


def _render_type(image: ModelImage, node: ModelNode) -> int:
    """A display-list node's `ModelType`, the pre-defined render-mode and
    combiner setup its lists are drawn with; 0 for any other node type."""
    if node.node_type == NODE_DL and node.data is not None:
        value = struct.unpack_from(">b", image.data, node.data + 0x12)[0]
    elif node.node_type == NODE_DL_COLLISION and node.data is not None:
        value = struct.unpack_from(">h", image.data, node.data + 0x18)[0]
    else:
        return 0
    if not 0 <= value <= MAX_RENDER_TYPE:
        raise GeModelError(f"node at {node.offset:#x}: unknown model type {value}")
    return value


# node record index, radius; see "Joint size table" in
# docs/formats/goldeneye-model-tree-resource.md.
_JOINT_SIZE_ENTRY_STRUCT = struct.Struct(">If")
JOINT_SIZE_ENTRY_SIZE = _JOINT_SIZE_ENTRY_STRUCT.size


def _joint_radius(image: ModelImage, node: ModelNode):
    """The bounding radius of a header, group or simple group node, or None
    for any other node type."""
    if node.data is None:
        return None
    if node.node_type == NODE_HEADER:
        return struct.unpack_from(">f", image.data, node.data + 0x08)[0]
    if node.node_type in (NODE_GROUP, NODE_GROUP_ALT):
        return struct.unpack_from(">f", image.data, node.data + 0x18)[0]
    if node.node_type == NODE_GROUP_SIMPLE:
        return struct.unpack_from(">f", image.data, node.data + 0x10)[0]
    return None


def _hit_fields(image: ModelImage, index_of: Dict[int, int], node: ModelNode):
    """(hit part, radius squared, centre, other node index, scale 1, scale 2,
    opcode flags) of a bounding box (0x0a) or hit sphere (0x11) node, or None
    for any other node type."""
    if node.node_type not in (NODE_BOUNDING_BOX, NODE_HIT_SPHERE) or node.data is None:
        return None
    flags = struct.unpack_from(">H", image.data, node.offset)[0] & 0xFF00
    hit_part = struct.unpack_from(">i", image.data, node.data)[0]
    if node.node_type == NODE_BOUNDING_BOX:
        return hit_part, 0.0, (0.0, 0.0, 0.0), NONE_INDEX, 0.0, 0.0, flags
    radius_squared = struct.unpack_from(">f", image.data, node.data + 0x04)[0]
    centre = struct.unpack_from(">3f", image.data, node.data + 0x08)
    other = _link_at(image, node, 0x14)
    if other is not None and other not in index_of:
        raise GeModelError(f"node at {node.offset:#x}: linked node {other:#x} is not a node of the tree")
    scale1, scale2 = struct.unpack_from(">2f", image.data, node.data + 0x18)
    return hit_part, radius_squared, centre, index_of[other] if other is not None else NONE_INDEX, scale1, scale2, flags


def _collision_record(image: ModelImage, index_of: Dict[int, int], node: ModelNode) -> Optional[bytes]:
    """The collision table entry of a collision display-list node (0x18), or
    None for any other node type."""
    if node.node_type != NODE_DL_COLLISION or node.data is None:
        return None
    vertex_count, collision_count = struct.unpack_from(">hh", image.data, node.data + 0x0C)
    vertices_at = _link_at(image, node, 0x10)
    usage_at = _link_at(image, node, 0x14)
    if vertices_at is None or usage_at is None or vertex_count < 0 or collision_count < 0:
        raise GeModelError(f"node at {node.offset:#x}: collision display list has no collision table")
    if vertices_at + collision_count * 16 > len(image.data) or usage_at + vertex_count * 2 > len(image.data):
        raise GeModelError(f"node at {node.offset:#x}: collision table runs off the file")
    out = bytearray(_COLLISION_HEADER_STRUCT.pack(0, vertex_count, collision_count))
    for i in range(collision_count):
        at = vertices_at + i * 16
        x, y, z, vertex = struct.unpack_from(">4h", image.data, at)
        related_address = struct.unpack_from(">I", image.data, at + 8)[0]
        related_index = struct.unpack_from(">h", image.data, at + 12)[0]
        related = image.resolve(related_address) if related_address != 0 else None
        if related_address != 0 and related not in index_of:
            raise GeModelError(f"node at {node.offset:#x}: related node {related_address:#x} is not a node of the tree")
        out += _COLLISION_VERTEX_STRUCT.pack(
            x, y, z, vertex, index_of[related] if related is not None else NONE_INDEX, related_index
        )
    for i in range(vertex_count):
        out += _COLLISION_USAGE_STRUCT.pack(struct.unpack_from(">h", image.data, usage_at + i * 2)[0])
    return bytes(out)


def _link_at(image: ModelImage, node: ModelNode, at: int) -> Optional[int]:
    """The node offset a pointer field of `node`'s data record names, or None."""
    address = struct.unpack_from(">I", image.data, node.data + at)[0]
    if address == 0:
        return None
    offset = image.resolve(address)
    if offset is None:
        raise GeModelError(f"node at {node.offset:#x}: {address:#x} is not an address inside the image")
    return offset


def _sort_fields(image: ModelImage, node: ModelNode):
    """(other node offset or None, point, second point) a draw-order sort
    value is computed from, or None for a node type that doesn't start a
    joint's draw entry. See "sort table" in the resource doc."""
    if node.node_type in _UNPACKED_SORT_NODE_TYPES:
        raise GeModelError(f"node at {node.offset:#x}: type {node.node_type:#x} has no draw-order data packed")
    zero = (0.0, 0.0, 0.0)
    if node.data is None:
        return None
    if node.node_type == NODE_HEADER:
        return _link_at(image, node, 0x04), zero, zero
    if node.node_type in (NODE_GROUP, NODE_GROUP_ALT):
        return _link_at(image, node, 0x14), zero, zero
    if node.node_type == NODE_GUNFIRE:
        return None, struct.unpack_from(">3f", image.data, node.data), zero
    if node.node_type == NODE_SHADOW:
        x, z = struct.unpack_from(">2f", image.data, node.data)
        return _link_at(image, node, 0x14), (x, 0.0, z), zero
    if node.node_type == NODE_INTERLINK:
        return (
            None,
            struct.unpack_from(">3f", image.data, node.data),
            struct.unpack_from(">3f", image.data, node.data + 0x0C),
        )
    return None


# node record index, joint ID; see "joint ID table" in
# docs/formats/goldeneye-model-tree-resource.md.
_JOINT_ENTRY_STRUCT = struct.Struct(">IH")
JOINT_ENTRY_SIZE = _JOINT_ENTRY_STRUCT.size

# node record index or -1; see "switch table" in
# docs/formats/goldeneye-model-tree-resource.md.
_SWITCH_ENTRY_STRUCT = struct.Struct(">i")
SWITCH_ENTRY_SIZE = _SWITCH_ENTRY_STRUCT.size


def _joint_id(image: ModelImage, node: ModelNode):
    """A group node's `ModelRoData_GroupRecord.JointID`, or a header node's
    `ModelRoData_HeaderRecord.AnimPart` (the joint its root motion is read
    from); None for any other node type."""
    if node.data is None:
        return None
    if node.node_type == NODE_HEADER:
        return struct.unpack_from(">H", image.data, node.data)[0]
    if node.node_type in (NODE_GROUP, NODE_GROUP_ALT):
        return struct.unpack_from(">H", image.data, node.data + 0x0C)[0]
    return None


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


def encode_model_tree_payload(
    image: ModelImage,
    tree: ModelTree,
    textures: Optional[Callable[[int], TextureBinding]] = None,
    radius: float = 0.0,
) -> bytes:
    """`textures` resolves the image of a textured gunfire or shadow node to
    its N64 format; a tree with none needs no lookup. `radius` is the model's
    bounding volume radius."""
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
    joint_entries = bytearray()
    joint_count = 0
    sort_entries = bytearray()
    sort_count = 0
    effect_entries = bytearray()
    effect_count = 0
    bounds_entries = bytearray()
    bounds_count = 0
    hit_entries = bytearray()
    hit_count = 0
    collision_entries = bytearray()
    collision_count = 0
    size_entries = bytearray()
    size_count = 0
    for i, offset in enumerate(order):
        node = tree.nodes[offset]
        origin, first, bend = _matrix_fields(image, node)
        child = _first_child(image, node)
        record = node.display_list
        out += _NODE_STRUCT.pack(
            offset,
            node.node_type,
            _render_type(image, node),
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
        joint_id = _joint_id(image, node)
        if joint_id is not None:
            joint_entries += _JOINT_ENTRY_STRUCT.pack(i, joint_id)
            joint_count += 1
        sort = _sort_fields(image, node)
        if sort is not None:
            other, point, point2 = sort
            if other is not None and other not in index_of:
                raise GeModelError(f"node at {offset:#x}: linked node {other:#x} is not a node of the tree")
            sort_entries += _SORT_ENTRY_STRUCT.pack(
                i, index_of[other] if other is not None else NONE_INDEX, *point, *point2
            )
            sort_count += 1
        if node.node_type == NODE_BOUNDING_BOX and node.data is not None:
            if node.data + 28 > len(image.data):
                raise GeModelError(f"node at {offset:#x}: bounding box runs off the file")
            bounds_entries += _BOUNDS_ENTRY_STRUCT.pack(i, *struct.unpack_from(">6f", image.data, node.data + 4))
            bounds_count += 1
        joint_radius = _joint_radius(image, node)
        if joint_radius is not None:
            size_entries += _JOINT_SIZE_ENTRY_STRUCT.pack(i, joint_radius)
            size_count += 1
        hit = _hit_fields(image, index_of, node)
        if hit is not None:
            part, radius_squared, centre, other, scale1, scale2, flags = hit
            hit_entries += _HIT_ENTRY_STRUCT.pack(i, part, radius_squared, *centre, other, scale1, scale2, flags)
            hit_count += 1
        collision = _collision_record(image, index_of, node)
        if collision is not None:
            collision_entries += struct.pack(">I", i) + collision[4:]
            collision_count += 1
        effect = effect_record(image, tree, node)
        if effect is not None:
            texture = effect.texture
            if texture is not None and textures is None:
                raise GeModelError(f"node at {offset:#x}: a textured effect needs a texture lookup")
            effect_entries += _EFFECT_ENTRY_STRUCT.pack(
                i,
                *effect.size,
                1 if texture is not None else 0,
                texture.width if texture is not None else 0,
                texture.height if texture is not None else 0,
                texture.mip_map_tiles if texture is not None else 0,
                textures(texture.texture_id).fmt if texture is not None else 0,
            )
            effect_count += 1
    out += struct.pack(">I", lod_count)
    out += lod_entries
    out += struct.pack(">I", bsp_count)
    out += bsp_entries
    out += struct.pack(">I", joint_count)
    out += joint_entries
    out += struct.pack(">I", len(tree.switches))
    for target in tree.switches:
        if target is not None and target not in index_of:
            raise GeModelError(f"switch array entry {target:#x} is not a node of the tree")
        out += _SWITCH_ENTRY_STRUCT.pack(index_of[target] if target is not None else NONE_INDEX)
    out += struct.pack(">I", sort_count)
    out += sort_entries
    out += struct.pack(">I", effect_count)
    out += effect_entries
    out += struct.pack(">I", bounds_count)
    out += bounds_entries
    out += struct.pack(">I", hit_count)
    out += hit_entries
    out += struct.pack(">I", collision_count)
    out += collision_entries
    out += struct.pack(">I", size_count)
    out += size_entries
    out += struct.pack(">f", radius)
    return bytes(out)
