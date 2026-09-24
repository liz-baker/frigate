#!/usr/bin/env python3
"""One-command driver: takes your own GoldenEye ROM and produces a
`.o2r` archive of its chr/gun/prop object segments. Same shape as
build_textures_o2r.py -- shells out to the GoldenEye decomp's own
extraction tooling (reuse, not reimplementation), then hands the result
to pack_objects.py.

Usage:
    python3 tools/o2r_packer/build_objects_o2r.py \\
        --rom /path/to/your/baserom.u.z64 \\
        --output goldeneye_objects.o2r

Requires the GoldenEye decomp submodule checked out (`git submodule
update --init external/007`) and your own legally-owned ROM -- see the
top-level README.md's Legal section.

Covers `obseg/{chr,gun,prop}` only -- `bg`/`stan`/`text` need their own
follow-up (see pack_objects.py's module docstring for why).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from decomp_extract import run_extraction_mode
from o2r_writer import O2rWriter
from pack_chr_records import pack_chr_records
from pack_ramrom import pack_ramrom
from pack_model_resources import make_texture_lookup, pack_animation_resources, pack_model_resources
from pack_objects import DEFAULT_CATEGORIES, pack_objects
from pack_textures import parse_images_def

# pack_animation_resources() needs this segment, but its filelist.u.csv row
# defaults to extract=0, so extract_files()'s "files" mode skips it.
_ANIMATION_TABLE_PATH = "assets/animationtable_entries.bin"


def extract_files(decomp_dir: Path, rom_path: Path) -> None:
    """Runs the GoldenEye decomp's own file-extraction step against
    `rom_path` (scripts/extract_baserom.u.sh's "files" mode) -- not
    reimplemented here."""
    run_extraction_mode(decomp_dir, rom_path, "files")


def extract_animation_table(decomp_dir: Path, rom_path: Path) -> None:
    """Extracts `_ANIMATION_TABLE_PATH`'s bytes directly from `rom_path`,
    using the offset/size its filelist.u.csv row names. Reads the ROM
    itself rather than routing through the decomp's tools/extractor the
    way build_geometry_o2r.py's bg/stan/text/font rows do: that binary
    reliably segfaults on this specific row once `assets/` already holds
    the many thousands of files a real `just pack_archives` run
    accumulates before this point. Only valid while the row's
    `compressed` column is 0; raises if that ever changes."""
    filelist_csv = decomp_dir / "scripts" / "filelist.u.csv"
    entry = None
    with open(filelist_csv) as f:
        for line in f:
            parts = line.strip().split(",")
            if len(parts) == 5 and parts[2] == _ANIMATION_TABLE_PATH:
                offset, size, _name, compressed, _extract = parts
                entry = (int(offset), int(size), compressed != "0")
                break
    if entry is None:
        raise ValueError(f"{_ANIMATION_TABLE_PATH} not found in {filelist_csv}")

    offset, size, compressed = entry
    if compressed:
        raise NotImplementedError(
            f"{_ANIMATION_TABLE_PATH}'s filelist.u.csv row is now marked compressed -- "
            "the direct byte-slice extraction below no longer applies; route it through "
            "the decomp's own extractor/compression tooling instead"
        )

    with open(rom_path, "rb") as rom:
        rom.seek(offset)
        data = rom.read(size)

    out_path = decomp_dir / _ANIMATION_TABLE_PATH
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(data)


def build_objects_o2r(
    decomp_dir: Path,
    rom_path: Path,
    output_path: Path,
    *,
    categories=DEFAULT_CATEGORIES,
) -> list[str]:
    """Extracts chr/gun/prop object segments from `rom_path` via the
    GoldenEye decomp's own tooling, then packs them into an O2R archive
    at `output_path`. Returns the relative paths of any objects that
    ended up missing after extraction. Each model also gets its vertex,
    display-list and embedded-image resources; the textures those lists
    name are in the archive build_textures_o2r.py writes."""
    extract_animation_table(decomp_dir, rom_path)
    extract_files(decomp_dir, rom_path)
    run_extraction_mode(decomp_dir, rom_path, "images")

    filelist_csv = decomp_dir / "scripts" / "filelist.u.csv"
    images = parse_images_def(decomp_dir / "assets" / "images.def")
    textures = make_texture_lookup(images, decomp_dir / "assets" / "images" / "split")

    with O2rWriter(output_path) as writer:
        missing = pack_objects(filelist_csv, decomp_dir, writer, categories=categories)
        missing += pack_model_resources(filelist_csv, decomp_dir, writer, textures, categories=categories)
        pack_animation_resources(decomp_dir, writer)
        pack_chr_records(decomp_dir, writer)
        pack_ramrom(decomp_dir, writer)

    return missing


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rom", required=True, type=Path, help="Path to your own GoldenEye ROM")
    parser.add_argument(
        "--decomp-dir",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "external" / "007",
        help="Path to the external/007 checkout (default: this repo's external/007)",
    )
    parser.add_argument("--output", required=True, type=Path, help="Where to write the .o2r archive")
    args = parser.parse_args()

    if not args.rom.exists():
        print(f"ROM not found: {args.rom}", file=sys.stderr)
        return 1

    missing = build_objects_o2r(args.decomp_dir, args.rom, args.output)

    print(f"Wrote {args.output}")
    if missing:
        print(f"Warning: {len(missing)} objects had no file after extraction:")
        for path in missing[:10]:
            print(f"  - {path}")
        if len(missing) > 10:
            print(f"  ... and {len(missing) - 10} more")

    return 0


if __name__ == "__main__":
    sys.exit(main())
