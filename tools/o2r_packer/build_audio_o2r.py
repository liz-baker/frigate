#!/usr/bin/env python3
"""One-command driver: takes your own GoldenEye ROM and produces a `.o2r`
archive of its audio banks (the sound effects and the instruments, as `GEAB`
resources).

The banks are plain ROM regions listed in the GoldenEye decomp's
`scripts/filelist.u.csv`, so unlike the other drivers this needs no extractor
build -- only the decomp checkout for that list and for the sound-effect
names in `src/bondconstants.h`.

Usage:
    python3 tools/o2r_packer/build_audio_o2r.py \\
        --rom /path/to/your/baserom.u.z64 \\
        --output goldeneye_audio.o2r
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from o2r_writer import O2rWriter
from pack_audio import AudioPackError, pack_audio


def build_audio_o2r(decomp_dir: Path, rom_path: Path, output_path: Path) -> None:
    filelist_csv = decomp_dir / "scripts" / "filelist.u.csv"
    if not filelist_csv.exists():
        raise FileNotFoundError(
            f"{filelist_csv} not found -- is the GoldenEye decomp submodule checked out? "
            "(git submodule update --init external/007)"
        )
    with O2rWriter(output_path) as writer:
        pack_audio(rom_path.read_bytes(), filelist_csv, writer, decomp_dir / "src" / "bondconstants.h")


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
    try:
        build_audio_o2r(args.decomp_dir, args.rom, args.output)
    except (AudioPackError, FileNotFoundError) as err:
        print(err, file=sys.stderr)
        return 1
    print(f"Wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
