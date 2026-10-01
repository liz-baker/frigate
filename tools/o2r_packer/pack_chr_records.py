#!/usr/bin/env python3
"""Packs the character model table: for each body and head, the model file it
loads from, the scale and root-motion scale its instances start with, the two
flags that decide how a body takes a head, and the model's bounding radius.
The hat placements (`chrHeadHats.inc.c`) go in a second resource.

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
from typing import Dict, List, NamedTuple, Tuple, Union

from ge_model import read_model_header_counts
from o2r_writer import O2rWriter

RESOURCE_PATH = "chr/records"
HATS_RESOURCE_PATH = "chr/hats"
# Six hat types (HATTYPE_BERRET through HATTYPE_MOON) for each male head.
HATS_PER_HEAD = 6

_ENUM_MARKERS = {
    "HEAD_START",
    "HEAD_F_START",
    "HEAD_BOND_START",
    "HEAD_END",
    "BODIES_MAX",
}
_RECORD_RE = re.compile(
    r'\{\s*&\w+\s*,\s*"(?P<file>\w+)"\s*,\s*(?P<scale>[-\d.e]+)f?\s*,\s*(?P<pov>[-\d.e]+)f?\s*,'
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


class ChrModelRecord(NamedTuple):
    """One `ChrModelFileRecord`: the model file, the scale and root-motion
    (`pov`) scale as the decomp writes them, and the two flags."""

    file: str
    scale: str
    pov: str
    is_male: int
    has_head: int


def parse_records(decomp_dir: Path) -> List[ChrModelRecord]:
    """The record files in `c_item_entries` order."""
    chr_dir = decomp_dir / "assets" / "obseg" / "chr"
    table = (chr_dir / "chrModelFileRecords.inc.c").read_text()
    records: List[ChrModelRecord] = []
    for include in _INCLUDE_RE.findall(table):
        text = (decomp_dir / include).read_text()
        match = _RECORD_RE.search(text)
        if match is None:
            raise ChrRecordsError(f"{include} holds no chr model record")
        records.append(
            ChrModelRecord(
                match.group("file"),
                match.group("scale"),
                match.group("pov"),
                int(match.group("male"), 0),
                int(match.group("head"), 0),
            )
        )
    return records


def _header_name(filename: str) -> str:
    """The `MODELFILEHEADER` name of a model file: `CcamguardZ` is `camguard`."""
    return filename[1:-1]


def build_records(decomp_dir: Union[str, Path]) -> bytes:
    """The resource's bytes: one line per slot,
    `<index>\\t<identifier,...>\\t<file>\\t<isMale>\\t<hasHead>\\t<scale>\\t<pov>\\t<radius>`."""
    decomp_dir = Path(decomp_dir)
    slots = parse_slot_names((decomp_dir / "src" / "bondconstants.h").read_text())
    records = parse_records(decomp_dir)
    if sorted(slots) != list(range(len(records))):
        raise ChrRecordsError(f"the enums name slots {min(slots)}..{max(slots)} but there are {len(records)} records")
    counts = read_model_header_counts(decomp_dir)
    lines = []
    for index, record in enumerate(records):
        names = ",".join(slots[index])
        header = _header_name(record.file)
        if header not in counts:
            raise ChrRecordsError(f"{record.file}: no model header for `{header}`")
        radius = repr(counts[header].radius)
        lines.append(
            f"{index}\t{names}\t{record.file}\t{record.is_male}\t{record.has_head}\t"
            f"{record.scale}\t{record.pov}\t{radius}"
        )
    return ("\n".join(lines) + "\n").encode("ascii")


_HAT_FLOAT = r"(-?[\d.]+(?:e-?\d+)?)"
_HAT_RE = re.compile(r"\{\s*" + r"\s*,\s*".join([_HAT_FLOAT] * 6) + r"\s*,?\s*\}")


def build_hats(decomp_dir: Union[str, Path]) -> bytes:
    """The hat placements (`headHat_array_8003E464`): one line per entry,
    `<index>\\t<xoffset>\\t<yoffset>\\t<zoffset>\\t<xsize>\\t<ysize>\\t<zsize>`. The
    entries are grouped by male head, `HATS_PER_HEAD` to a head."""
    decomp_dir = Path(decomp_dir)
    text = (decomp_dir / "assets" / "obseg" / "chr" / "chrHeadHats.inc.c").read_text()
    text = re.sub(r"//[^\n]*", "", text)
    entries = _HAT_RE.findall(text[text.index("{") + 1 :])
    if not entries or len(entries) % HATS_PER_HEAD:
        raise ChrRecordsError(f"chrHeadHats.inc.c holds {len(entries)} entries, not a multiple of {HATS_PER_HEAD}")
    lines = [f"{index}\t" + "\t".join(entry) for index, entry in enumerate(entries)]
    return ("\n".join(lines) + "\n").encode("ascii")


def pack_chr_records(decomp_dir: Union[str, Path], writer: O2rWriter) -> str:
    writer.add_resource(RESOURCE_PATH, "OBLB", version=0, resource_id=0, payload=build_records(decomp_dir))
    writer.add_resource(HATS_RESOURCE_PATH, "OBLB", version=0, resource_id=0, payload=build_hats(decomp_dir))
    return RESOURCE_PATH
