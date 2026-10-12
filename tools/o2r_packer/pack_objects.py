#!/usr/bin/env python3
"""Lists GoldenEye's object segments (`obseg/{chr,gun,prop}`) from the decomp's
`scripts/filelist.u.csv`. The models themselves are packed as decoded
resources by pack_model_resources.py; the raw files are not written to the
archive.

Which files exist per category, and whether extract_baserom.u.sh
actually produces them by default, is read from the GoldenEye decomp's
own `scripts/filelist.u.csv` (its `offset,size,name,compressed,extract`
format).

`bg`/`stan`/`text`/font are handled separately by pack_geometry.py,
since their filelist.u.csv entries default to `extract=0`.
"""

from __future__ import annotations

from pathlib import Path
from typing import List, NamedTuple, Sequence, Union

DEFAULT_CATEGORIES = ("chr", "gun", "prop")


class ObjectEntry(NamedTuple):
    category: str
    name: str  # e.g. "CarmourguardZ", the split filename's stem
    relative_path: str  # e.g. "assets/obseg/chr/CarmourguardZ.bin"


def parse_filelist_obseg(
    filelist_csv_path: Union[str, Path],
    categories: Sequence[str] = DEFAULT_CATEGORIES,
) -> List[ObjectEntry]:
    """Parses the GoldenEye decomp's scripts/filelist.u.csv for obseg entries in
    the given categories whose `extract` flag is set (the ones
    extract_baserom.u.sh's "files" step actually produces), in file
    order."""
    entries: List[ObjectEntry] = []
    prefixes = {category: f"assets/obseg/{category}/" for category in categories}

    with open(filelist_csv_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            parts = line.split(",")
            if len(parts) != 5:
                continue

            _offset, _size, name, _compressed, extract = parts
            if extract != "1":
                continue

            for category, prefix in prefixes.items():
                if name.startswith(prefix):
                    stem = Path(name).stem
                    entries.append(ObjectEntry(category=category, name=stem, relative_path=name))
                    break

    return entries
