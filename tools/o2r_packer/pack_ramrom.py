#!/usr/bin/env python3
"""Packs the attract-mode demos: the decomp's `ramrom_table` and each demo's
recorded file, unchanged.

The table lists the demos in the order `select_ramrom_to_play` picks from,
with the unlock threshold each needs. Layout: docs/formats/goldeneye-ramrom.md.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import List, Tuple, Union

from o2r_writer import O2rWriter

TABLE_PATH = "ramrom/table"
DEMO_PATH_PREFIX = "ramrom/"

_TABLE_RE = re.compile(r"struct\s+ramrom_struct\s+ramrom_table\s*\[\]\s*=\s*\{(.*?)\};", re.S)
_ENTRY_RE = re.compile(r"\{\s*&ramrom_(?P<name>\w+)\s*,\s*(?P<locked>\d+)\s*\}")


class RamromError(ValueError):
    """Raised when the decomp's table and its demo files don't line up."""


def parse_table(decomp_dir: Union[str, Path]) -> List[Tuple[str, int]]:
    """(name, locked) for every demo, in table order."""
    source = (Path(decomp_dir) / "src" / "game" / "ramromreplay.c").read_text()
    match = _TABLE_RE.search(source)
    if match is None:
        raise RamromError("no `ramrom_table` in ramromreplay.c")
    entries = [(m.group("name"), int(m.group("locked"))) for m in _ENTRY_RE.finditer(match.group(1))]
    if not entries:
        raise RamromError("`ramrom_table` has no entries")
    return entries


def build_table(entries: List[Tuple[str, int]]) -> bytes:
    return "".join(f"{name}\t{locked}\n" for name, locked in entries).encode()


def pack_ramrom(decomp_dir: Union[str, Path], writer: O2rWriter) -> List[str]:
    """Writes `ramrom/table` and one `ramrom/<name>` per demo. Returns the
    resource paths written."""
    decomp_dir = Path(decomp_dir)
    entries = parse_table(decomp_dir)
    demo_dir = decomp_dir / "assets" / "ramrom"
    written = [TABLE_PATH]
    writer.add_resource(TABLE_PATH, "OBLB", version=0, resource_id=0, payload=build_table(entries))
    for name, _ in entries:
        file = demo_dir / f"ramrom_{name}.bin"
        if not file.is_file():
            raise RamromError(f"no demo file {file}")
        path = DEMO_PATH_PREFIX + name
        writer.add_resource(path, "OBLB", version=0, resource_id=0, payload=file.read_bytes())
        written.append(path)
    return written
