#!/usr/bin/env python3
"""Packs the character model table: for each body and head, the model file it
loads from and the two flags that decide how a body takes a head.

The GoldenEye decomp keeps the table (`c_item_entries`) as one array indexed by
the `BODIES` enum, with heads sharing the index space at `HEAD_START` and up
(the `HEADS` enum names the same slots). This reads both enums and the record
files and writes one text resource, keyed by the decomp's own identifiers so
the game code can name a body or head the way the decomp does. Layout:
docs/formats/goldeneye-chr-records.md.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Dict, List, Tuple, Union

from o2r_writer import O2rWriter

RESOURCE_PATH = "chr/records"

_ENUM_MARKERS = {
    "HEAD_START",
    "HEAD_F_START",
    "HEAD_BOND_START",
    "HEAD_END",
    "BODIES_MAX",
}
_RECORD_RE = re.compile(
    r'\{\s*&\w+\s*,\s*"(?P<file>\w+)"\s*,\s*[-\d.e]+f?\s*,\s*[-\d.e]+f?\s*,'
    r"\s*(?P<male>0x[0-9a-fA-F]+|\d+)\s*,\s*(?P<head>0x[0-9a-fA-F]+|\d+)\s*,"
)
_INCLUDE_RE = re.compile(r"#include\s*<(assets/obseg/chr/[^>]+)>")


class ChrRecordsError(ValueError):
    """Raised when the decomp's tables don't line up."""


def _strip_enum_body(text: str, name: str) -> str:
    """The body of `typedef enum <name> { ... } <name>;` with comments removed
    and every `#ifdef ALL_BONDS` block dropped (the US build doesn't define it)."""
    match = re.search(r"typedef\s+enum\s+" + name + r"\s*\{(.*?)\}\s*" + name + r"\s*;", text, re.S)
    if match is None:
        raise ChrRecordsError(f"no `{name}` enum in bondconstants.h")
    body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    return re.sub(r"#ifdef\s+ALL_BONDS.*?#endif", "", body, flags=re.S)


def _enum_values(body: str, known: Dict[str, int]) -> List[Tuple[str, int]]:
    """Every enumerator in `body` with its value, stopping at HEAD_END or
    BODIES_MAX (what follows is counts and sentinels)."""
    values: List[Tuple[str, int]] = []
    next_value = 0
    for item in body.split(","):
        item = re.sub(r"#\w+.*", "", item).strip()
        if not item:
            continue
        if "=" in item:
            name, expression = (part.strip() for part in item.split("=", 1))
            value = known[expression] if expression in known else int(expression, 0)
        else:
            name, value = item, next_value
        known[name] = value
        values.append((name, value))
        next_value = value + 1
        if name in ("HEAD_END", "BODIES_MAX"):
            break
    return values


def parse_slot_names(bondconstants: str) -> Dict[int, List[str]]:
    """Slot index -> the `BODY_*` and `HEAD_*` identifiers naming it."""
    known: Dict[str, int] = {}
    slots: Dict[int, List[str]] = {}
    for enum in ("BODIES", "HEADS"):
        for name, value in _enum_values(_strip_enum_body(bondconstants, enum), known):
            if name in _ENUM_MARKERS:
                continue
            slots.setdefault(value, []).append(name)
    return slots


def parse_records(decomp_dir: Path) -> List[Tuple[str, int, int]]:
    """The record files in `c_item_entries` order, as (model file name,
    isMale, hasHead)."""
    chr_dir = decomp_dir / "assets" / "obseg" / "chr"
    table = (chr_dir / "chrModelFileRecords.inc.c").read_text()
    records: List[Tuple[str, int, int]] = []
    for include in _INCLUDE_RE.findall(table):
        text = (decomp_dir / include).read_text()
        match = _RECORD_RE.search(text)
        if match is None:
            raise ChrRecordsError(f"{include} holds no chr model record")
        records.append((match.group("file"), int(match.group("male"), 0), int(match.group("head"), 0)))
    return records


def build_records(decomp_dir: Union[str, Path]) -> bytes:
    """The resource's bytes: one line per slot,
    `<index>\\t<identifier,...>\\t<file>\\t<isMale>\\t<hasHead>`."""
    decomp_dir = Path(decomp_dir)
    slots = parse_slot_names((decomp_dir / "src" / "bondconstants.h").read_text())
    records = parse_records(decomp_dir)
    if sorted(slots) != list(range(len(records))):
        raise ChrRecordsError(f"the enums name slots {min(slots)}..{max(slots)} but there are {len(records)} records")
    lines = []
    for index, (filename, is_male, has_head) in enumerate(records):
        names = ",".join(slots[index])
        lines.append(f"{index}\t{names}\t{filename}\t{is_male}\t{has_head}")
    return ("\n".join(lines) + "\n").encode("ascii")


def pack_chr_records(decomp_dir: Union[str, Path], writer: O2rWriter) -> str:
    writer.add_resource(RESOURCE_PATH, "OBLB", version=0, resource_id=0, payload=build_records(decomp_dir))
    return RESOURCE_PATH
