#!/usr/bin/env python3
"""Packs GoldenEye's `obseg/{stan,text,setup,brief}` segments and font data into
an O2R archive. Part of frigate issue #10.

Like chr/gun/prop, these are pulled directly from the user's own ROM --
NOT from the GoldenEye decomp's checked-in decompiled C source. That
checked-in source is what its own Makefiles (`Makefile.bg`/
`Makefile.stan`/`Makefile.text`) compile to reproduce these same
bytes, but building from it would mean this repo's own pipeline
depends on data someone else already committed to a vendored
decompilation project's git history -- see frigate issue #10's
discussion. The decomp's own `scripts/filelist.u.csv` already lists
every one of these segments' exact ROM offset/size, same as chr/gun/prop; the only difference is their
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

from ge_stan import pack_stan_file
from o2r_writer import O2rWriter

# category -> the scripts/filelist.u.csv path prefix its segments live under.
CATEGORY_PREFIXES = {
    "bg": "assets/obseg/bg/",
    "stan": "assets/obseg/stan/",
    "text": "assets/obseg/text/",
    "setup": "assets/obseg/setup/",
    "font": "assets/font/",
    "brief": "assets/obseg/brief/",
}
DEFAULT_CATEGORIES = tuple(CATEGORY_PREFIXES)

# Listed (so they are extracted) but not written as files: pack_bg_resources
# writes each bg file's header and rooms as decoded resources.
UNPACKED_CATEGORIES = frozenset({"bg"})

# category -> the O2R resource type its packed payload should carry. text/font
# use frigate's own GETX/GEFN types (src/resource/ResourceType.h) so
# TextFactory/FontFactory pick them up at runtime instead of treating them as
# opaque blobs; stan is decoded into GEST (ge_stan.py).
CATEGORY_RESOURCE_TYPES = {
    "stan": "GEST",
    "text": "GETX",
    "setup": "OBLB",
    "font": "GEFN",
    "brief": "OBLB",
}

# Matches each type's registered factory version (src/platform/Boot.cpp
# registers TextFactory/FontFactory/StanFactory as V0; OBLB keeps
# pack_objects.py's existing V1 convention unchanged; the setup files are read
# as V0 blobs like the ramrom demos).
CATEGORY_VERSIONS = {
    "stan": 0,
    "text": 0,
    "setup": 0,
    "font": 0,
    "brief": 0,
}

# Citadel's stan file is in the beta layout (float points, tiles named by
# pointer), which the game's stan code cannot read and whose stage cannot be
# loaded; it stays an opaque blob.
BETA_STAN_FILES = frozenset({"Tbg_cat_all_p_stanZ"})

# The order a font bank's ROM segment stores its two extracted files in --
# see pack_geometry()'s font handling below for why concatenation order
# matters (FontFactory reads a single contiguous kerning-table-then-
# chartable-then-pixels segment, split into two files only by extraction).
_FONT_PART_SUFFIXES = ("_kerning", "_fontchartable")
_FONT_PART_ORDER = {suffix.lstrip("_"): i for i, suffix in enumerate(_FONT_PART_SUFFIXES)}


def _split_font_part(virtual_name: str) -> tuple[str, str]:
    """Splits a font entry's virtual name into (bank, part), e.g.
    "fontBankGothic_kerning" -> ("fontBankGothic", "kerning")."""
    for suffix in _FONT_PART_SUFFIXES:
        if virtual_name.endswith(suffix):
            return virtual_name[: -len(suffix)], suffix[1:]
    raise ValueError(f"unrecognized font segment name: {virtual_name!r}")


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
) -> List[str]:
    """Reads every stan/text/setup/brief/font segment named via
    `parse_filelist_geometry` from `decomp_dir` (after extraction -- see
    build_geometry_o2r.py) and writes it into `writer` at
    `<virtual_prefix>/<category>/<name>` (font segments go under
    `font/<bank>` instead, with no `obseg` prefix, matching that they
    live at `assets/font/` rather than `assets/obseg/font/`).

    Each category is packed under the resource type
    `CATEGORY_RESOURCE_TYPES` names it: text/font use frigate's own
    GETX/GEFN types so TextFactory/FontFactory load them directly.
    A font bank's kerning table and char table + pixel data are two
    separate files after extraction but one contiguous segment in the
    ROM (FontFactory expects pixelDataOffset to be relative to the
    combined segment, not to the char-table file alone) -- this
    concatenates each bank's `_kerning` and `_fontchartable` files, in
    that order, into a single `font/<bank>` resource.

    Returns the relative paths of segments missing after extraction --
    callers should treat a non-empty result as a build error.
    """
    decomp_dir = Path(decomp_dir)
    missing: List[str] = []

    font_parts: dict[str, dict[str, GeometryEntry]] = {}

    for entry in parse_filelist_geometry(filelist_csv_path, categories):
        if entry.category in UNPACKED_CATEGORIES:
            continue
        if entry.category == "font":
            bank, part = _split_font_part(entry.virtual_name)
            font_parts.setdefault(bank, {})[part] = entry
            continue

        extracted_path = decomp_dir / entry.relative_path
        if not extracted_path.exists():
            missing.append(entry.relative_path)
            continue

        payload = extracted_path.read_bytes()
        resource_type = CATEGORY_RESOURCE_TYPES[entry.category]
        version = CATEGORY_VERSIONS[entry.category]
        if entry.category == "stan":
            if entry.virtual_name in BETA_STAN_FILES:
                resource_type, version = "OBLB", 1
            else:
                payload = pack_stan_file(payload)
        virtual_path = f"{virtual_prefix}/{entry.category}/{entry.virtual_name}"
        writer.add_resource(
            virtual_path,
            resource_type,
            version=version,
            resource_id=0,
            payload=payload,
        )

    for bank, parts in font_parts.items():
        part_entries = sorted(parts.items(), key=lambda item: _FONT_PART_ORDER[item[0]])

        payload = bytearray()
        bank_missing = False
        for _part, part_entry in part_entries:
            extracted_path = decomp_dir / part_entry.relative_path
            if not extracted_path.exists():
                missing.append(part_entry.relative_path)
                bank_missing = True
                continue
            payload += extracted_path.read_bytes()

        if bank_missing:
            continue

        writer.add_resource(
            f"font/{bank}",
            CATEGORY_RESOURCE_TYPES["font"],
            version=CATEGORY_VERSIONS["font"],
            resource_id=0,
            payload=bytes(payload),
        )

    return missing
