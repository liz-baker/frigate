#!/usr/bin/env python3
"""Packs the object model table: for each `PROP` value, the model's name, the
scale the game gives its instances and the bounding box its collision and
placement use.

The GoldenEye decomp's `PitemZ_entries` lists one `PROPFILERECORD(name, scale)`
per prop, in `PROP` order; the model is the extracted `P<name>Z.bin`. The box
is the first bounding-box node `chrobjGetBboxFromObjFile` finds: among the
root's children, else among its first child's. This writes one text resource.
Layout: docs/formats/goldeneye-prop-models.md.
"""

from __future__ import annotations

import re
import struct
from pathlib import Path
from typing import List, Optional, Tuple, Union

from ge_container import ModelImage
from ge_model import GeModelError, ModelNode, ModelTree, parse_model_tree, read_model_header_counts
from o2r_writer import O2rWriter

RESOURCE_PATH = "prop/models"

NODE_BBOX = 0x0A
# The box follows the record's model number (`ModelRoData_BoundingBoxRecord`).
_BBOX_BOUNDS_OFFSET = 0x04

_DEFAULT_SCALE = "0.1"
_ENUM_RE = re.compile(r"typedef\s+enum\s+PROP\s*\{(.*?)\}\s*PROP\s*;", re.S)
_INCLUDE_RE = re.compile(r"#include\s*<(assets/obseg/prop/[^>]+/propFileRecord\.inc\.c)>")
_RECORD_RE = re.compile(r"^\s*PROPFILERECORD\(\s*(\w+)\s*,\s*([^)]*?)\s*\)", re.M)
_EXPLOSION_ENUM_RE = re.compile(r"typedef\s+enum\s+E_EXPLOSIONTYPE\s*\{(.*?)\}\s*E_EXPLOSIONTYPE\s*;", re.S)
_EXPLOSION_RECORD_RE = re.compile(r"\{\s*(EXPLOSION_\w+)\s*,\s*\{")

Bounds = Tuple[float, float, float, float, float, float]


class PropModelsError(ValueError):
    """Raised when the decomp's tables don't line up."""


def _prop_names(bondconstants: str) -> List[str]:
    match = _ENUM_RE.search(bondconstants)
    if match is None:
        raise PropModelsError("no `PROP` enum in bondconstants.h")
    body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.S)
    names = [item.split("=")[0].strip() for item in body.split(",")]
    return [name for name in names if name and name != "PROP_MAX"]


def parse_explosion_types(decomp_dir: Path, count: int) -> List[int]:
    """The explosion type (an index into the explosion table) each object model
    is destroyed with, from `object_explosion_details`."""
    constants = (decomp_dir / "src" / "bondconstants.h").read_text()
    match = _EXPLOSION_ENUM_RE.search(constants)
    if match is None:
        raise PropModelsError("no `E_EXPLOSIONTYPE` enum in bondconstants.h")
    body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.S)
    values = {item.split("=")[0].strip(): index for index, item in enumerate(body.split(",")) if item.strip()}
    text = (decomp_dir / "assets" / "obseg" / "prop" / "propExplosionDetailsRecords.inc.c").read_text()
    names = _EXPLOSION_RECORD_RE.findall(text)
    # The last record ends the table.
    if len(names) != count + 1:
        raise PropModelsError(f"the explosion details have {len(names)} records, expected {count + 1}")
    try:
        return [values[name] for name in names[:count]]
    except KeyError as error:
        raise PropModelsError(f"unknown explosion type {error}") from error


def parse_records(decomp_dir: Path) -> List[Tuple[str, str]]:
    """Each record's model name and scale text, in table order."""
    table = (decomp_dir / "assets" / "obseg" / "prop" / "propItemModelFileRecord.inc.c").read_text()
    records: List[Tuple[str, str]] = []
    for include in _INCLUDE_RE.findall(table):
        text = (decomp_dir / include).read_text()
        found = _RECORD_RE.findall(text)
        if len(found) != 1:
            raise PropModelsError(f"{include} holds {len(found)} records, expected 1")
        name, scale = found[0]
        records.append((name, scale or _DEFAULT_SCALE))
    return records


def find_bounds(image: ModelImage, tree: ModelTree) -> Optional[Bounds]:
    """chrobjGetBboxFromObjFile: the bounding-box node among the root's
    children, else among the first child's."""
    root = tree.nodes[tree.root]
    first_child = root.child
    if first_child is None:
        return None
    candidates: List[Optional[int]] = [first_child]
    if tree.nodes[first_child].child is not None:
        candidates.append(tree.nodes[first_child].child)
    for start in candidates:
        at = start
        while at is not None:
            node: ModelNode = tree.nodes[at]
            if node.node_type == NODE_BBOX:
                if node.data is None or node.data + _BBOX_BOUNDS_OFFSET + 24 > len(image.data):
                    raise GeModelError(f"node at {node.offset:#x}: bounding box runs off the image")
                return struct.unpack_from(">6f", image.data, node.data + _BBOX_BOUNDS_OFFSET)
            at = node.next
    return None


def build_prop_models(decomp_dir: Union[str, Path]) -> bytes:
    """The resource's bytes: one line per `PROP` value,
    `model\\t<index>\\t<name>\\t<scale>\\t<xmin>\\t<xmax>\\t<ymin>\\t<ymax>\\t<zmin>\\t<zmax>\\t<skeleton>\\t<explosion>`
    (`-` for each bound of a model with no box, and for a model with no
    skeleton). `name` is the record's model name (`alarm1`)."""
    decomp_dir = Path(decomp_dir)
    names = _prop_names((decomp_dir / "src" / "bondconstants.h").read_text())
    records = parse_records(decomp_dir)
    if len(records) != len(names):
        raise PropModelsError(f"PROP names {len(names)} models but the table has {len(records)} records")
    counts = read_model_header_counts(decomp_dir)
    explosions = parse_explosion_types(decomp_dir, len(names))
    lines = []
    for index, (model, scale) in enumerate(records):
        path = decomp_dir / "assets" / "obseg" / "prop" / f"P{model}Z.bin"
        if not path.exists():
            raise PropModelsError(f"{names[index]}: {path.name} is not extracted")
        if model not in counts:
            raise PropModelsError(f"{names[index]}: no model header for `{model}`")
        image = ModelImage(path.read_bytes())
        bounds = find_bounds(image, parse_model_tree(image, counts[model]))
        fields = ["-"] * 6 if bounds is None else [repr(value) for value in bounds]
        # `model` (the PROPFILERECORD name), not `names[index]` (the PROP_*
        # identifier): consumers derive the archived model's path from this
        # field the same way model_base_path does (`P<model>Z`).
        skeleton = counts[model].skeleton_name or "-"
        lines.append("\t".join(["model", str(index), model, scale, *fields, skeleton, str(explosions[index])]))
    return ("\n".join(lines) + "\n").encode("ascii")


def pack_prop_models(decomp_dir: Union[str, Path], writer: O2rWriter) -> str:
    writer.add_resource(RESOURCE_PATH, "OBLB", version=0, resource_id=0, payload=build_prop_models(decomp_dir))
    return RESOURCE_PATH
