#!/usr/bin/env python3
"""Parses the node tree of a GoldenEye model image (frigate issue #18,
node-tree step). Input is the `ModelImage` from `ge_container`; on-disk
layout: docs/formats/goldeneye-model-nodes.md.

The file has no header, so the caller supplies the two counts the game
keeps in code. `read_model_header_counts` recovers them for every
chr/gun/prop file from the GoldenEye decomp's header tables.
"""

from __future__ import annotations

import re
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from ge_container import ModelImage

NODE_SIZE = 0x18
TEXTURE_ENTRY_SIZE = 12
POINTER_SIZE = 4

NODE_DL = 0x04
NODE_LOD = 0x08
NODE_SWITCH = 0x12
NODE_DL_PRIMARY = 0x16
NODE_DL_COLLISION = 0x18

_NODE_DATA_OFFSET = 0x04
_NODE_PARENT_OFFSET = 0x08
_NODE_NEXT_OFFSET = 0x0C
_NODE_PREV_OFFSET = 0x10
_NODE_CHILD_OFFSET = 0x14

# node type -> (primary, secondary, vertex pointer, vertex count, count
# struct format), each an offset into the node's data record; the
# secondary offset is None when the record has no second list.
_DL_LAYOUTS: Dict[int, Tuple[int, Optional[int], int, int, str]] = {
    NODE_DL: (0x00, 0x04, 0x0C, 0x10, ">H"),
    NODE_DL_COLLISION: (0x00, 0x04, 0x08, 0x0C, ">h"),
    NODE_DL_PRIMARY: (0x08, None, 0x04, 0x00, ">i"),
}

VERTEX_SIZE = 16
# A primary-only node's count is in groups of four vertices: in all five
# such nodes across the US ROM's model files, the lists load exactly
# four times the count.
_PRIMARY_ONLY_VERTICES_PER_COUNT = 4

_HEADER_MACRO = re.compile(r"^\s*MODELFILEHEADER\(\s*(\w+)\s*,([^\n]*)\)\s*$", re.M)
_HEADER_NUM_SWITCHES_ARG = 3
_HEADER_NUM_TEXTURES_ARG = 7
_HEADER_ARG_COUNT = 8


class GeModelError(ValueError):
    """Raised when a model image's node tree is malformed."""


@dataclass(frozen=True)
class ModelCounts:
    num_switches: int
    num_textures: int


@dataclass(frozen=True)
class DisplayListRecord:
    """A display-list node's lists and vertex array. `vertices_address` is
    the raw pointer, which `ModelImage.display_list_resolver` binds as
    segment 4. Offsets are into the image; `secondary` is None for a
    node with no second list. `num_vertices` is the count field as
    stored; `vertex_bytes` is the array size the node's lists load."""

    primary: Optional[int]
    secondary: Optional[int]
    vertices: Optional[int]
    vertices_address: int
    num_vertices: int
    vertex_bytes: int


@dataclass(frozen=True)
class ModelNode:
    offset: int
    node_type: int
    data: Optional[int]
    parent: Optional[int]
    next: Optional[int]
    prev: Optional[int]
    child: Optional[int]
    display_list: Optional[DisplayListRecord]


@dataclass(frozen=True)
class ModelTexture:
    texture_id: int
    width: int
    height: int
    mip_map_tiles: int
    type: int
    render_depth: int
    s_flags: int
    t_flags: int


@dataclass(frozen=True)
class ModelTree:
    root: int
    nodes: Dict[int, ModelNode]
    textures: List[ModelTexture]
    switches: List[Optional[int]]


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def _link(image: ModelImage, address: int, what: str) -> Optional[int]:
    if address == 0:
        return None
    offset = image.resolve(address)
    if offset is None:
        raise GeModelError(f"{what}: {address:#x} is not an address inside the image")
    return offset


def _read_display_list(image: ModelImage, node_type: int, record: int, where: str) -> DisplayListRecord:
    primary_at, secondary_at, vertices_at, count_at, count_format = _DL_LAYOUTS[node_type]
    record_end = max(vertices_at + POINTER_SIZE, count_at + struct.calcsize(count_format))
    if record + record_end > len(image.data):
        raise GeModelError(f"{where}: data record runs off the image")
    vertices_address = _u32(image.data, record + vertices_at)
    secondary = None
    if secondary_at is not None:
        secondary = _link(image, _u32(image.data, record + secondary_at), f"{where} secondary list")
    num_vertices = struct.unpack_from(count_format, image.data, record + count_at)[0]
    per_count = _PRIMARY_ONLY_VERTICES_PER_COUNT if node_type == NODE_DL_PRIMARY else 1
    return DisplayListRecord(
        primary=_link(image, _u32(image.data, record + primary_at), f"{where} primary list"),
        secondary=secondary,
        vertices=_link(image, vertices_address, f"{where} vertices"),
        vertices_address=vertices_address,
        num_vertices=num_vertices,
        vertex_bytes=max(num_vertices, 0) * per_count * VERTEX_SIZE,
    )


def _read_node(image: ModelImage, offset: int) -> ModelNode:
    where = f"node at {offset:#x}"
    if offset + NODE_SIZE > len(image.data):
        raise GeModelError(f"{where}: runs off the image")
    node_type = struct.unpack_from(">H", image.data, offset)[0] & 0xFF
    data = _link(image, _u32(image.data, offset + _NODE_DATA_OFFSET), f"{where} data")
    display_list = None
    if node_type in _DL_LAYOUTS:
        if data is None:
            raise GeModelError(f"{where}: type {node_type:#x} has no data record")
        display_list = _read_display_list(image, node_type, data, where)
    return ModelNode(
        offset=offset,
        node_type=node_type,
        data=data,
        parent=_link(image, _u32(image.data, offset + _NODE_PARENT_OFFSET), f"{where} parent"),
        next=_link(image, _u32(image.data, offset + _NODE_NEXT_OFFSET), f"{where} next"),
        prev=_link(image, _u32(image.data, offset + _NODE_PREV_OFFSET), f"{where} prev"),
        child=_link(image, _u32(image.data, offset + _NODE_CHILD_OFFSET), f"{where} child"),
        display_list=display_list,
    )


def _implied_children(image: ModelImage, node: ModelNode) -> List[int]:
    """Nodes a node reaches through its data record instead of (or as
    well as) its own child link. The game copies a LOD or switch node's
    target into its child link at load time, so the file's own link can
    be null. A BSP node's left and right pointers are not listed: they
    name the same nodes as its child link and that child's next link."""
    if node.data is None:
        return []
    if node.node_type == NODE_LOD:
        fields = [node.data + 0x08]
    elif node.node_type == NODE_SWITCH:
        fields = [node.data]
    else:
        return []
    found = []
    for field_at in fields:
        if field_at + POINTER_SIZE > len(image.data):
            raise GeModelError(f"node at {node.offset:#x}: data record runs off the image")
        target = _link(image, _u32(image.data, field_at), f"node at {node.offset:#x} data target")
        if target is not None:
            found.append(target)
    return found


def parse_model_tree(image: ModelImage, counts: ModelCounts) -> ModelTree:
    """Reads the switch array, texture table and every node reachable
    from the root. Raises `GeModelError` for a link that leaves the image
    or a node reached by two different links (a cycle or a shared node)."""
    data = image.data
    textures_at = counts.num_switches * POINTER_SIZE
    root = textures_at + counts.num_textures * TEXTURE_ENTRY_SIZE
    if root + NODE_SIZE > len(data):
        raise GeModelError("switch array and texture table run past the image")

    switches = [_link(image, _u32(data, i * POINTER_SIZE), f"switch {i}") for i in range(counts.num_switches)]
    textures = [
        ModelTexture(_u32(data, at), *struct.unpack_from(">7B", data, at + 4))
        for at in range(textures_at, root, TEXTURE_ENTRY_SIZE)
    ]

    nodes: Dict[int, ModelNode] = {}
    stack = [root]
    while stack:
        offset = stack.pop()
        node = _read_node(image, offset)
        nodes[offset] = node
        # An implied child equal to the child link is the same edge.
        for follow in (node.next, node.child, *(t for t in _implied_children(image, node) if t != node.child)):
            if follow is None:
                continue
            if follow in nodes or follow in stack:
                raise GeModelError(f"node at {follow:#x} is reached twice")
            stack.append(follow)
    return ModelTree(root=root, nodes=nodes, textures=textures, switches=switches)


def read_model_header_counts(decomp_dir: Path) -> Dict[str, ModelCounts]:
    """Counts for every model, keyed by the header name the game files
    use (`camguard`, `ak47`; the file is that name with a one-letter
    directory prefix and a `Z` suffix). Read from the decomp's
    `MODELFILEHEADER` tables."""
    counts: Dict[str, ModelCounts] = {}
    for path in (decomp_dir / "assets" / "obseg").rglob("*odelFileHeader*.inc.c"):
        for match in _HEADER_MACRO.finditer(path.read_text()):
            args = [arg.strip() for arg in match.group(2).split(",")]
            if len(args) != _HEADER_ARG_COUNT:
                raise GeModelError(f"{path.name}: {match.group(1)} has {len(args)} header arguments")
            counts[match.group(1)] = ModelCounts(
                int(args[_HEADER_NUM_SWITCHES_ARG], 0), int(args[_HEADER_NUM_TEXTURES_ARG], 0)
            )
    return counts
