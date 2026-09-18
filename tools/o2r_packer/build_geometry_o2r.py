#!/usr/bin/env python3
"""One-command driver: takes your own GoldenEye ROM and produces a
`.o2r` archive of its `obseg/{bg,stan,text}` segments and font data.

Same shape as build_textures_o2r.py/build_objects_o2r.py: these
segments are pulled straight from your own ROM, not from the GoldenEye
decomp's checked-in decompiled C source (see pack_geometry.py's module
docstring for why that distinction matters). It reuses the decomp's
own `tools/extractor` binary -- the same validated, byte-exact
extraction tool build_objects_o2r.py's underlying script already
depends on -- rather than reimplementing ROM decompression, by feeding
it a filtered copy of `scripts/filelist.u.csv` with `extract` forced
to 1 for exactly the bg/stan/text/font rows.

Usage:
    python3 tools/o2r_packer/build_geometry_o2r.py \\
        --rom /path/to/your/baserom.u.z64 \\
        --output goldeneye_geometry.o2r

Requires the GoldenEye decomp submodule checked out and a C compiler to
build `tools/extractor` -- the same requirements build_textures_o2r.py/
build_objects_o2r.py already have. No MIPS cross-toolchain needed.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

from o2r_writer import O2rWriter
from decomp_extract import run_extraction_mode
from pack_geometry import DEFAULT_CATEGORIES, parse_filelist_geometry, pack_geometry
from pack_model_resources import make_texture_lookup, pack_bg_resources
from pack_textures import parse_images_def


def _write_extraction_csv(entries, csv_path: Path) -> None:
    """Writes a filelist.u.csv-format CSV (offset,size,name,compressed,
    extract) for `entries`, with `extract` forced to 1 regardless of
    what the GoldenEye decomp's own filelist.u.csv says for these rows
    -- tools/extractor only extracts rows whose `extract` column is 1."""
    with open(csv_path, "w") as f:
        for entry in entries:
            f.write(f"{entry.offset},{entry.size},{entry.relative_path},{int(entry.compressed)},1\n")


def ensure_extractor_built(decomp_dir: Path) -> Path:
    """Builds the GoldenEye decomp's tools/extractor if it isn't already
    -- the same native (non-MIPS) host build build_objects_o2r.py's
    underlying script already performs."""
    extractor_dir = decomp_dir / "tools" / "extractor"
    binary = extractor_dir / "extractor"
    if not binary.exists():
        if not extractor_dir.exists():
            raise FileNotFoundError(
                f"{extractor_dir} not found -- is the GoldenEye decomp submodule checked out? "
                "(git submodule update --init external/007)"
            )
        subprocess.run(["make"], cwd=extractor_dir, check=True)
    return binary


def extract_geometry(decomp_dir: Path, rom_path: Path, filelist_csv_path: Path) -> None:
    """Runs the GoldenEye decomp's own tools/extractor against
    `rom_path`, naming exactly the bg/stan/text/font rows from
    `filelist_csv_path` (regardless of their own `extract` column)."""
    extractor = ensure_extractor_built(decomp_dir)
    entries = parse_filelist_geometry(filelist_csv_path, DEFAULT_CATEGORIES)

    with tempfile.NamedTemporaryFile(mode="w", suffix=".csv", dir=decomp_dir, delete=False) as tmp:
        tmp_csv_path = Path(tmp.name)
    try:
        _write_extraction_csv(entries, tmp_csv_path)
        subprocess.run(
            [str(extractor), str(rom_path.resolve()), str(tmp_csv_path)],
            cwd=decomp_dir,
            check=True,
        )
    finally:
        tmp_csv_path.unlink(missing_ok=True)


def build_geometry_o2r(decomp_dir: Path, rom_path: Path, output_path: Path) -> list[str]:
    """Extracts bg/stan/text/font segments from `rom_path` via the
    GoldenEye decomp's own tools/extractor, then packs them into an O2R
    archive at `output_path`. Returns the relative paths of any
    segments that ended up missing after extraction. Each bg room also
    gets its vertex and display-list resources."""
    filelist_csv = decomp_dir / "scripts" / "filelist.u.csv"
    extract_geometry(decomp_dir, rom_path, filelist_csv)
    run_extraction_mode(decomp_dir, rom_path, "images")

    images = parse_images_def(decomp_dir / "assets" / "images.def")
    textures = make_texture_lookup(images, decomp_dir / "assets" / "images" / "split")
    bg_files = [
        e.relative_path
        for e in parse_filelist_geometry(filelist_csv, ("bg",))
        if e.relative_path.endswith("_all_p.bin")
    ]

    with O2rWriter(output_path) as writer:
        missing = pack_geometry(filelist_csv, decomp_dir, writer)
        missing += pack_bg_resources(bg_files, decomp_dir, writer, textures)

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

    missing = build_geometry_o2r(args.decomp_dir, args.rom, args.output)

    print(f"Wrote {args.output}")
    if missing:
        print(f"Warning: {len(missing)} segments had no file after extraction:")
        for path in missing[:10]:
            print(f"  - {path}")
        if len(missing) > 10:
            print(f"  ... and {len(missing) - 10} more")

    return 0


if __name__ == "__main__":
    sys.exit(main())
