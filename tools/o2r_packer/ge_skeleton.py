#!/usr/bin/env python3
"""Encodes GoldenEye's per-model joint tables as `GESK` resources (frigate
issue #70). Payload: docs/formats/goldeneye-model-skeleton-resource.md.

Unlike a model's own node tree, a skeleton is not reachable from any
model file's own bytes: the GoldenEye decomp's `ModelFileHeader.Skeleton`
is a compiled ROM data-segment pointer with no extractable address
without building the decomp. What the decomp's own asset extraction
(`make forceextractassets`) does produce is each skeleton's `JOINTLIST`
array as plain C source (`assets/embedded/skeletons/*.inc.c`) -- several
skeletons often share one file (e.g. every simple prop uses
`standard_object`) -- so this reads that literal data the same way
`ge_model.read_model_header_counts` already reads `MODELFILEHEADER`
tables, rather than resolving any pointer.
"""

from __future__ import annotations

import re
import struct
from pathlib import Path
from typing import Dict, List, NamedTuple, Union

from ge_model import GeModelError

SKELETON_RESOURCE_TYPE = "GESK"
SKELETON_RESOURCE_VERSION = 0

_JOINT_ARRAY_RE = re.compile(r"ModelJoint\s+JOINTLIST\((\w+)\)\s*\[\s*\w*\s*\]\s*=\s*\{(.*?)\};", re.S)
_JOINT_ENTRY_RE = re.compile(r"\{\s*(0[xX][0-9a-fA-F]+|\d+)\s*,\s*(0[xX][0-9a-fA-F]+|\d+)\s*,\s*(0[xX][0-9a-fA-F]+|\d+)\s*\}")
_SKELETON_MACRO_RE = re.compile(r"MODELSKELETON\(\s*(\w+)\s*,\s*([^,]+),\s*[^)]+\)")

_JOINT_STRUCT = struct.Struct(">HHH")
JOINT_SIZE = _JOINT_STRUCT.size


class SkeletonJoint(NamedTuple):
    node_type: int
    mtx_a: int
    mtx_b: int


def parse_skeletons(decomp_dir: Union[str, Path]) -> Dict[str, List[SkeletonJoint]]:
    """Every skeleton's joint list, keyed by the name a `MODELFILEHEADER`'s
    `SKELETON(name)` argument names. Raises `GeModelError` if a skeleton's
    `MODELSKELETON` joint count doesn't match its `JOINTLIST` array."""
    decomp_dir = Path(decomp_dir)
    skeletons: Dict[str, List[SkeletonJoint]] = {}
    counts: Dict[str, int] = {}
    for path in (decomp_dir / "assets" / "embedded" / "skeletons").glob("*.inc.c"):
        text = path.read_text()
        for match in _JOINT_ARRAY_RE.finditer(text):
            name, body = match.group(1), match.group(2)
            joints = [
                SkeletonJoint(int(a, 0), int(b, 0), int(c, 0)) for a, b, c in _JOINT_ENTRY_RE.findall(body)
            ]
            if name in skeletons:
                raise GeModelError(f"{path.name}: skeleton {name} is defined more than once")
            skeletons[name] = joints
        for match in _SKELETON_MACRO_RE.finditer(text):
            counts[match.group(1)] = int(match.group(2).strip(), 0)

    for name, joints in skeletons.items():
        if name in counts and counts[name] != len(joints):
            raise GeModelError(
                f"skeleton {name}: MODELSKELETON names {counts[name]} joints but JOINTLIST has {len(joints)}"
            )
    return skeletons


def encode_skeleton_payload(joints: List[SkeletonJoint]) -> bytes:
    out = bytearray(struct.pack(">I", len(joints)))
    for joint in joints:
        out += _JOINT_STRUCT.pack(joint.node_type, joint.mtx_a, joint.mtx_b)
    return bytes(out)
