#!/usr/bin/env python3
"""Verification for the O2R packer, meant to be run against real assets
extracted from a user-supplied ROM (see README.md). Does NOT validate
that extraction itself was correct -- that's the GoldenEye decomp's own
job, via its pinned `ge007.*.sha1`/`scripts/test_files.sh`.

Textures don't get a byte-for-byte round-trip check like the packer's
other asset categories (see pack_objects.py/pack_geometry.py): frigate
issue #18 has `pack_textures.py` decode each image rather than copy it
opaquely, so there's no unchanged source-byte copy to diff against.
`verify_textures_roundtrip()` instead checks that every image decodes
without error and that its packed `Fast::Texture` payload's declared
`ImageDataSize` matches what its own width/height/texture_type imply --
catching truncated/misshaped output without needing pixel-exact
reference data this repo doesn't have.

Usage:
    python3 verify_roundtrip.py textures <images.def path> <split dir>

Exits non-zero and prints every failure.
"""

from __future__ import annotations

import argparse
import struct
import sys
import tempfile
import zipfile
from pathlib import Path

from o2r_writer import O2rWriter, OTR_HEADER_SIZE
from pack_textures import pack_textures, parse_images_def

# Bits per pixel per Fast::TextureType: docs/formats/goldeneye-texture-bitstream.md's
# "Formats" table. Kept independent of ge_texture.py's own copy so a bug
# there doesn't also corrupt this check.
_BITS_PER_PIXEL_BY_TEXTURE_TYPE = {
    1: 32,  # RGBA32bpp
    2: 16,  # RGBA16bpp
    3: 4,  # Palette4bpp
    4: 8,  # Palette8bpp
    5: 4,  # Grayscale4bpp
    6: 8,  # Grayscale8bpp
    7: 4,  # GrayscaleAlpha4bpp
    8: 8,  # GrayscaleAlpha8bpp
    9: 16,  # GrayscaleAlpha16bpp
}


def _expected_image_data_size(texture_type: int, width: int, height: int) -> int:
    bits_per_pixel = _BITS_PER_PIXEL_BY_TEXTURE_TYPE[texture_type]
    if bits_per_pixel < 8:
        pixels_per_byte = 8 // bits_per_pixel
        return ((width + pixels_per_byte - 1) // pixels_per_byte) * height
    return width * height * (bits_per_pixel // 8)


def verify_textures_roundtrip(images_def_path: str, split_dir: str) -> int:
    """Packs every image and checks each packed resource's declared
    size against what its own width/height/texture_type imply (see
    module docstring). Returns the number of failures (0 = clean)."""
    split_dir_path = Path(split_dir)
    entries = parse_images_def(images_def_path)
    print(f"Found {len(entries)} images in {images_def_path}")

    failures = 0
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

                packed = zf.read(f"textures/{entry.name}")
                body = packed[OTR_HEADER_SIZE:]
                texture_type, width, height = struct.unpack(">III", body[:12])
                data_size = struct.unpack(">I", body[24:28])[0]

                checked += 1
                expected = _expected_image_data_size(texture_type, width, height)
                if data_size != expected:
                    failures += 1
                    print(
                        f"  MISMATCH: {entry.name} -- {width}x{height} texture_type={texture_type} "
                        f"declares {data_size} image bytes, expected {expected}"
                    )

    print(f"Checked {checked} textures, {failures} failure(s).")
    return failures


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
