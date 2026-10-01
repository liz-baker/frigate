#!/usr/bin/env python3
"""Packs the animation tables: which clip each animation id names.

The GoldenEye decomp's initanitable.c lists the clips as `PTR_ANIM_<name>`
entries of `animation_table_ptrs1` (the characters' animations, numbered by
`enum ANIMATION`) and `animation_table_ptrs2` (the aircraft's). The clips
themselves are `animations/<name>` (pack_model_resources.py); this resource
is the id -> name map. Layout: docs/formats/goldeneye-animation-table.md.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Union

from o2r_writer import O2rWriter

RESOURCE_PATH = "animations/table"

_ARRAY_RE = re.compile(r"(animation_table_ptrs[12])\s*\[\s*\]\s*=\s*\{(.*?)\};", re.DOTALL)
_ENTRY_RE = re.compile(r"PTR_ANIM_(\w+)|(\b0\b)")
_NULL_NAME = re.compile(r"^null\d+$")


class AnimationTableError(ValueError):
    """Raised when initanitable.c does not hold the tables."""


def parse_animation_tables(source: str) -> Dict[int, List[Optional[str]]]:
    """Table number (1 or 2) -> the clip name of each id, None for an id with
    no clip (the `null<N>` placeholders)."""
    tables: Dict[int, List[Optional[str]]] = {}
    for match in _ARRAY_RE.finditer(source):
        number = int(match.group(1)[-1])
        names: List[Optional[str]] = []
        for entry in _ENTRY_RE.finditer(match.group(2)):
            if entry.group(2) is not None:
                break
            name = entry.group(1)
            names.append(None if _NULL_NAME.match(name) else name)
        tables[number] = names
    if sorted(tables) != [1, 2] or not tables[1] or not tables[2]:
        raise AnimationTableError("initanitable.c does not define both animation tables")
    return tables


def build_animation_table(source: str, clips: Optional[set] = None) -> bytes:
    """The resource's bytes: one line per id, `<table>\\t<id>\\t<name>`, the
    name `-` for an id with no clip. `clips`, when given, is checked for every
    name."""
    tables = parse_animation_tables(source)
    lines: List[str] = []
    for number in (1, 2):
        for index, name in enumerate(tables[number]):
            if name is not None and clips is not None and name not in clips:
                raise AnimationTableError(f"animation {name} has no clip")
            lines.append(f"{number}\t{index}\t{name if name is not None else '-'}")
    return ("\n".join(lines) + "\n").encode("ascii")


def pack_animation_table(decomp_dir: Union[str, Path], writer: O2rWriter, clips: Optional[set] = None) -> str:
    source = (Path(decomp_dir) / "src" / "game" / "initanitable.c").read_text()
    writer.add_resource(RESOURCE_PATH, "OBLB", version=0, resource_id=0, payload=build_animation_table(source, clips))
    return RESOURCE_PATH
