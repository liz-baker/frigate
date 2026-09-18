#!/usr/bin/env python3
"""Round-trip verification for the O2R packer, meant to be run against
real assets extracted from a user-supplied ROM (see README.md and
external/007's own README for how to run its extraction scripts first --
this repo doesn't wire that extraction step into its own build yet).

This does NOT validate that extraction itself was correct -- that's
`external/007`'s own job, via its pinned `ge007.*.sha1` (final rebuilt
ROM, byte-exact) and `scripts/test_files.sh` (per-object-section md5s
against `scripts/make/build_hashtable.sh`'s known-good hashes, for the
recompile-to-C pipeline). What this checks is narrower and specific to
this repo's own new code: that packing an already-extracted file into an
O2R archive and reading it back out again reproduces the exact same
bytes, byte for byte -- i.e. the packer/header logic doesn't corrupt or
mutate data in either direction.

Usage:
    python3 verify_roundtrip.py textures <images.def path> <split dir>

Exits non-zero and prints every mismatch if any resource's round-tripped
payload differs from its source file.
"""

from __future__ import annotations

import argparse
import sys
import tempfile
import zipfile
from pathlib import Path

from o2r_writer import O2rWriter, OTR_HEADER_SIZE, unpack_blob_payload
from pack_textures import pack_textures, parse_images_def


def verify_textures_roundtrip(images_def_path: str, split_dir: str) -> int:
    """Packs every image named in `images_def_path` and reads each one
    back out of the resulting archive, comparing bytes against the
    original `<split_dir>/<name>.bin` file. Returns the number of
    mismatches (0 = clean)."""
    split_dir_path = Path(split_dir)
    entries = parse_images_def(images_def_path)
    print(f"Found {len(entries)} images in {images_def_path}")

    mismatches = 0
    checked = 0

    with tempfile.TemporaryDirectory() as tmp:
        archive_path = Path(tmp) / "roundtrip.o2r"

        with O2rWriter(archive_path) as writer:
            missing = pack_textures(images_def_path, split_dir_path, writer)

        if missing:
            print(f"  (skipping {len(missing)} images with no split file present)")

        with zipfile.ZipFile(archive_path) as zf:
            for entry in entries:
                if entry.name in missing:
                    continue

                source_path = split_dir_path / f"{entry.name}.bin"
                original = source_path.read_bytes()

                packed = zf.read(f"textures/{entry.name}")
                roundtripped = unpack_blob_payload(packed[OTR_HEADER_SIZE:])

                checked += 1
                if roundtripped != original:
                    mismatches += 1
                    print(
                        f"  MISMATCH: {entry.name} -- source is {len(original)} bytes, "
                        f"round-tripped is {len(roundtripped)} bytes"
                    )

    print(f"Checked {checked} textures, {mismatches} mismatch(es).")
    return mismatches


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    subparsers = parser.add_subparsers(dest="command", required=True)

    textures_parser = subparsers.add_parser("textures", help="Verify the texture-bank packer round-trips cleanly")
    textures_parser.add_argument("images_def", help="Path to images.def")
    textures_parser.add_argument("split_dir", help="Directory of split <name>.bin texture files")

    args = parser.parse_args()

    if args.command == "textures":
        mismatches = verify_textures_roundtrip(args.images_def, args.split_dir)
        return 1 if mismatches else 0

    return 1


if __name__ == "__main__":
    sys.exit(main())
