#!/usr/bin/env python3
"""Packs GoldenEye's `obseg/{bg,stan,text}` segments and font data into
an O2R archive. Part of frigate issue #10.

Like chr/gun/prop, these are pulled directly from the user's own ROM --
NOT from the GoldenEye decomp's checked-in decompiled C source. That
checked-in source is what its own Makefiles (`Makefile.bg`/
`Makefile.stan`/`Makefile.text`) compile to reproduce these same
bytes, but building from it would mean this repo's own pipeline
depends on data someone else already committed to a vendored
decompilation project's git history -- see frigate issue #10's
discussion. The decomp's own `scripts/filelist.u.csv` already lists
every one of these segments' exact ROM offset/size (a bare fact, not
adapted code), same as chr/gun/prop; the only difference is their
`extract` column defaults to 0 there (extract_baserom.u.sh's own
default extraction skips them), which extract_geometry.py in
build_geometry_o2r.py overrides by feeding `tools/extractor`'s own
binary a filtered copy of that CSV with `extract` forced to 1 for
exactly these rows -- not by patching the vendored CSV or its
extraction script.
"""

from __future__ import annotations

from pathlib import Path
from typing import List, NamedTuple, Sequence, Union

from o2r_writer import O2rWriter

# category -> the scripts/filelist.u.csv path prefix its segments live under.
CATEGORY_PREFIXES = {
    "bg": "assets/obseg/bg/",
    "stan": "assets/obseg/stan/",
    "text": "assets/obseg/text/",
    "font": "assets/font/",
}
DEFAULT_CATEGORIES = tuple(CATEGORY_PREFIXES)


class GeometryEntry(NamedTuple):
    category: str
    virtual_name: str  # e.g. "bg_ame_all_p", "Tbg_ame_all_p_stanZ", "LarecJ"
    relative_path: str  # e.g. "assets/obseg/bg/bg_ame_all_p.bin"
    offset: int
    size: int
    compressed: bool


def parse_filelist_geometry(
    filelist_csv_path: Union[str, Path],
    categories: Sequence[str] = DEFAULT_CATEGORIES,
) -> List[GeometryEntry]:
    """Parses the GoldenEye decomp's scripts/filelist.u.csv for bg/stan/text/font
    entries in the given categories, regardless of their `extract`
    column -- unlike parse_filelist_obseg (pack_objects.py), which only
    covers chr/gun/prop and honors that flag, since those categories'
    entries already default to being extracted."""
    entries: List[GeometryEntry] = []
    prefixes = {category: CATEGORY_PREFIXES[category] for category in categories}

    with open(filelist_csv_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            parts = line.split(",")
            if len(parts) != 5:
                continue

            offset, size, name, compressed, _extract = parts

            for category, prefix in prefixes.items():
                if name.startswith(prefix):
                    virtual_name = Path(name).name
                    if virtual_name.endswith(".bin"):
                        virtual_name = virtual_name[: -len(".bin")]
                    entries.append(
                        GeometryEntry(
                            category=category,
                            virtual_name=virtual_name,
                            relative_path=name,
                            offset=int(offset),
                            size=int(size),
                            compressed=compressed != "0",
                        )
                    )
                    break

    return entries


def pack_geometry(
    filelist_csv_path: Union[str, Path],
    decomp_dir: Union[str, Path],
    writer: O2rWriter,
    *,
    categories: Sequence[str] = DEFAULT_CATEGORIES,
    virtual_prefix: str = "obseg",
    resource_type: str = "OBLB",
    version: int = 1,
) -> List[str]:
    """Reads every bg/stan/text/font segment named via
    `parse_filelist_geometry` from `decomp_dir` (after extraction -- see
    build_geometry_o2r.py) and writes it into `writer` at
    `<virtual_prefix>/<category>/<name>` (font segments go under
    `font/<name>` instead, with no `obseg` prefix, matching that they
    live at `assets/font/` rather than `assets/obseg/font/`).

    Returns the relative paths of segments missing after extraction --
    callers should treat a non-empty result as a build error.
    """
    decomp_dir = Path(decomp_dir)
    missing: List[str] = []

    for entry in parse_filelist_geometry(filelist_csv_path, categories):
        extracted_path = decomp_dir / entry.relative_path
        if not extracted_path.exists():
            missing.append(entry.relative_path)
            continue

        payload = extracted_path.read_bytes()
        if entry.category == "font":
            virtual_path = f"font/{entry.virtual_name}"
        else:
            virtual_path = f"{virtual_prefix}/{entry.category}/{entry.virtual_name}"
        writer.add_resource(
            virtual_path,
            resource_type,
            version=version,
            resource_id=0,
            payload=payload,
        )

    return missing
