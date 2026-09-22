#!/usr/bin/env python3
"""Packs GoldenEye's compressed object-segment binaries
(`obseg/{chr,gun,prop}`) into an O2R archive. Part of frigate issue #10.

Unlike pack_textures.py, this doesn't adapt any of goldeneye-pc-port's
model-parsing code (`generate_chr_c.py`/`generate_gun_c.py`/
`generate_prop_model_c.py`) -- those decode a model's binary into a C
scene-graph for compiling straight into the game binary, which is
exactly the step O2R packaging replaces. Per issue #10's stated scope
("runtime deserialization of the packed archives" is out of scope
here), this packs each object's already-extracted raw bytes as an
opaque Blob resource. Nothing reads them at runtime now that pack_model_resources.py
writes the decoded resources; dropping them is
https://github.com/liz-baker/frigate/issues/56.

Which files exist per category, and whether extract_baserom.u.sh
actually produces them by default, is read from the GoldenEye decomp's
own `scripts/filelist.u.csv` (its `offset,size,name,compressed,extract`
format) -- not reimplemented, just parsed, since it's the decomp's own
list of what its extraction step produces. That's a bare format/fact
carried over in fresh code, not copied prose, so it needs no
THIRD_PARTY_LICENSES entry.

`bg`/`stan`/`text`/font are handled separately by pack_geometry.py,
since their filelist.u.csv entries default to `extract=0`.
"""

from __future__ import annotations

from pathlib import Path
from typing import List, NamedTuple, Sequence, Union

from o2r_writer import O2rWriter

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


def pack_objects(
    filelist_csv_path: Union[str, Path],
    decomp_dir: Union[str, Path],
    writer: O2rWriter,
    *,
    categories: Sequence[str] = DEFAULT_CATEGORIES,
    virtual_prefix: str = "obseg",
    resource_type: str = "OBLB",
    version: int = 1,
) -> List[str]:
    """Reads every object named in `filelist_csv_path` (for `categories`)
    from `decomp_dir` and writes it into `writer` at
    `<virtual_prefix>/<category>/<name>`.

    Returns the relative paths of objects that were missing after
    extraction (and so were skipped) -- callers should treat a
    non-empty result as a build error once real assets are wired in.
    """
    decomp_dir = Path(decomp_dir)
    missing: List[str] = []

    for entry in parse_filelist_obseg(filelist_csv_path, categories):
        bin_path = decomp_dir / entry.relative_path
        if not bin_path.exists():
            missing.append(entry.relative_path)
            continue

        payload = bin_path.read_bytes()
        # See pack_textures.py: real resource IDs are a #11/runtime-
        # integration decision, not a packer one.
        writer.add_resource(
            f"{virtual_prefix}/{entry.category}/{entry.name}",
            resource_type,
            version=version,
            resource_id=0,
            payload=payload,
        )

    return missing


def main() -> None:
    import argparse

    parser = argparse.ArgumentParser(description="Pack GoldenEye's chr/gun/prop object segments into an O2R archive")
    parser.add_argument("filelist_csv", help="Path to external/007/scripts/filelist.u.csv")
    parser.add_argument("decomp_dir", help="Path to the external/007 checkout")
    parser.add_argument("output_o2r", help="Path to write the .o2r archive to")
    args = parser.parse_args()

    with O2rWriter(args.output_o2r) as writer:
        missing = pack_objects(args.filelist_csv, args.decomp_dir, writer)

    if missing:
        print(f"Warning: {len(missing)} objects missing after extraction, skipped:")
        for path in missing[:10]:
            print(f"  - {path}")
        if len(missing) > 10:
            print(f"  ... and {len(missing) - 10} more")


if __name__ == "__main__":
    main()
