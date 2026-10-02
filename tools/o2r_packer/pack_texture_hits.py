#!/usr/bin/env python3
"""Packs what a shot learns from the texture it hits: each image's surface
type for the impact it leaves and for its sound.

The GoldenEye decomp's `images.def` lists every image with two surface types
(`HIT_*`): the one the impact effect is chosen by (`hitTexture`) and the one
the hit sound is chosen by (`hitSound`). A room or model display list names
its texture by the image's resource path, and the game's own texture number is
the image's position in the list. Layout: docs/formats/goldeneye-texture-hits.md.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Dict, List, NamedTuple, Union

from o2r_writer import O2rWriter

RESOURCE_PATH = "textures/hits"

_HIT_ENUM_RE = re.compile(r"typedef\s+enum\s+HIT_TYPE\s*\{(.*?)\}\s*HIT_TYPE\s*;", re.S)


class TextureHitsError(ValueError):
    """Raised when the decomp's tables don't line up."""


class ImageHit(NamedTuple):
    name: str
    sound: int
    impact: int


def parse_hit_types(bondconstants: str) -> Dict[str, int]:
    """The `HIT_TYPE` enumerators and their values."""
    match = _HIT_ENUM_RE.search(bondconstants)
    if match is None:
        raise TextureHitsError("no `HIT_TYPE` enum in bondconstants.h")
    names = [item.strip() for item in match.group(1).split(",") if item.strip()]
    return {name: value for value, name in enumerate(names)}


def parse_image_hits(images_def_path: Union[str, Path], hit_types: Dict[str, int]) -> List[ImageHit]:
    """Every `IMAGE(NAME, SIZE, HITSOUND, HITTEXTURE, ...)` entry's surface types, in file order."""
    entries: List[ImageHit] = []
    with open(images_def_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line.startswith("IMAGE("):
                continue
            parts = [p.strip() for p in line[len("IMAGE(") : -1].split(",")]
            if len(parts) < 4:
                continue
            try:
                entries.append(ImageHit(parts[0], hit_types[parts[2]], hit_types[parts[3]]))
            except KeyError as error:
                raise TextureHitsError(f"image {parts[0]} has the unknown surface type {error}") from error
    return entries


def build_texture_hits(images: List[ImageHit]) -> bytes:
    """One line per image, in texture number order: `<name> <hit sound> <hit texture>`."""
    return "".join(f"{image.name} {image.sound} {image.impact}\n" for image in images).encode("ascii")


def pack_texture_hits(decomp_dir: Union[str, Path], writer: O2rWriter) -> str:
    decomp_dir = Path(decomp_dir)
    hit_types = parse_hit_types((decomp_dir / "src" / "bondconstants.h").read_text())
    images = parse_image_hits(decomp_dir / "assets" / "images.def", hit_types)
    writer.add_resource(RESOURCE_PATH, "OBLB", version=0, resource_id=0, payload=build_texture_hits(images))
    return RESOURCE_PATH
