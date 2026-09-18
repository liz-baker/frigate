#!/usr/bin/env python3
"""One-command driver: takes your own GoldenEye ROM and produces a
`.o2r` archive of its texture bank. This is "our" pipeline -- it shells
out to the GoldenEye decomp's own extraction tooling (reuse, not
reimplementation) so you don't have to run those scripts yourself, then
hands the result to pack_textures.py.

Usage:
    python3 tools/o2r_packer/build_textures_o2r.py \\
        --rom /path/to/your/baserom.u.z64 \\
        --output goldeneye_textures.o2r

Requires the GoldenEye decomp submodule checked out (`git submodule
update --init external/007`) and your own legally-owned ROM -- see the
top-level README.md's Legal section. Extraction needs the tools this
repo's CI already installs for the decomp (a C compiler for
`tools/extractor`, `python3`); see the decomp's own docs for anything
more specific to your platform.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from decomp_extract import run_extraction_mode
from o2r_writer import O2rWriter
from pack_textures import pack_textures


def extract_images(decomp_dir: Path, rom_path: Path) -> None:
    """Runs the GoldenEye decomp's own image-extraction step against
    `rom_path` (scripts/extract_baserom.u.sh's "images" mode) -- not
    reimplemented here. See decomp_extract.py for why it's invoked as a
    bare "images" argument rather than "images <rom_path>"."""
    run_extraction_mode(decomp_dir, rom_path, "images")


def build_textures_o2r(decomp_dir: Path, rom_path: Path, output_path: Path) -> list[str]:
    """Extracts the texture bank from `rom_path` via the GoldenEye
    decomp's own tooling, then packs it into an O2R archive at
    `output_path`. Returns the names of any images that ended up
    missing their split file."""
    extract_images(decomp_dir, rom_path)

    images_def = decomp_dir / "assets" / "images.def"
    split_dir = decomp_dir / "assets" / "images" / "split"

    with O2rWriter(output_path) as writer:
        missing = pack_textures(images_def, split_dir, writer)

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

    missing = build_textures_o2r(args.decomp_dir, args.rom, args.output)

    print(f"Wrote {args.output}")
    if missing:
        print(f"Warning: {len(missing)} images had no split file after extraction:")
        for name in missing[:10]:
            print(f"  - {name}")
        if len(missing) > 10:
            print(f"  ... and {len(missing) - 10} more")

    return 0


if __name__ == "__main__":
    sys.exit(main())
